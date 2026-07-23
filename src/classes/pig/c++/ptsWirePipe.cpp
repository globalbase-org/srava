/*
 * ptsWirePipe — プロセス間ハンドシェイク用の双方向メッセージパイプ(ptsObject 派生)。
 *
 * 設計(pigwire_engine.txt §1 + codec_design):
 *  - 既にオープン済みの ts2IO を受け取り、pigwire レコード列を送受信する。
 *    read 用 rio / write 用 wio を分離して保持する:
 *      - 双方向 1 本(socketpair 等)        → ctor(parent, io)     で rio=wio=io
 *      - read/write 別 fd(ts2System rfd/wfd) → ctor(parent, rfd, wfd) で別々に直結
 *    プランナ側は子の stdout=rio / stdin=wio、エージェント側は自 stdin=rio / stdout=wio。
 *    rio/wio が揃えば送受信は対称(pty を介さないので raw バイナリ安全)。
 *  - cache stream の TS_THREAD ブロッキングと対照的に、**イベント駆動コルーチン**で読む:
 *    ts2IO::read_c は EAGAIN 時に sException で yield し、fd が読めるようになると状態関数が
 *    先頭から再実行される(read_c は ps_read_c に部分読みを保持して再開を担保)。
 *    → ルール: 1 ステートに read_c は 1 回。読み先バッファは**メンバ**(rhdr/rpayload/shdr。
 *      stack ローカルだと再実行で別アドレスになり read_c の bp が無効化する)。
 *
 *  状態:
 *    INI            : 自分の streamhdr を write → ACT_HELLO
 *    ACT_HELLO      : 相手の streamhdr を read+検証(peerPid 取得) → parent へ TSE_ASSERT(ready) → ACT_HDR
 *    ACT_HDR        : レコードヘッダ read。W_END 番兵/EOF なら FIN。payload あれば ACT_PAYLOAD
 *    ACT_PAYLOAD    : payload read → parent へ TSE_PACKET(ptsWirePacket) → ACT_HDR(ループ)
 *    FIN            : parent へ TSE_RETURN(errCode)。io は parent 所有なので close しない
 *
 *  送信(public, parent が呼ぶ):
 *    write(type, payload, len) / write_str(type, str) / wend()(W_END 番兵)
 *  受信は parent へ TSE_PACKET stdEvent で返す(msg_obj=ptsWirePacket)。
 *
 *  送信のバックプレッシャ: write_record は **yield-safe**。write_c は送信バッファ不足で
 *  sException を投げて yield し、呼び出し元の状態関数が先頭から再走される。write_record は
 *  sPicoState(psINI=ヘッダ / psDO=ペイロード / psDO2=release)で続きから resume し、hbuf を
 *  メンバ化して yield 跨ぎの bp を安定化、wlock(count=1)+wrHolder で再入時の混線を防ぐ。
 *  これにより **任意サイズ(例: 巨大インライン引数 ~157KB)のペイロードでも詰まらず送れる**。
 *  (旧コメントは v1 の「小メッセージ前提=yield しない」だったが、後の yield-safe 化で不正確に
 *   なったため更新。set_divisible された wfd に write_c で書くのが前提。)
 *  NB: 呼び出し元(parent)が 1 状態関数の中で write_record を複数回呼ぶ二重書きは別問題(未対応)。
 */
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/pigwire.h"
#include	"pig/c++/ptsWirePacket.h"
#include	"pig/c++/osglue.h"
#include	"ts2/c++/ts2IO.h"
#include	"ts2/c++/stdEvent.h"
#include	"ts2/c++/sPicoState.h"   /* write_record の psINI(hdr)/psDO(payload)/psDO2(release) 分割 */
#include	"ts2/c++/stdSemaphore.h" /* write_record 不可分化(count=1 ロック) */
#include	"ts2/c++/sCallSection.h" /* 呼び出し元 tinyState 識別(再入判定) */
#include	"_ts2/c++/ptsWirePipe_.h"

#include	<string.h>

/* write_record の release 用に pico state を 1 つ増やす(psINI=0, psDO=1 の次)。 */
enum { psDO2 = psDO + 1 };

CLASS_TINYSTATE(pig/c++/ptsWirePipe,pig/c++/ptsObject)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	// 双方向 1 本(socketpair 等): rio=wio=_io
	ptsWirePipe_(
		sPtr<tinyState> parent,
		sPtr<ts2IO> _io);
	// read/write 別 fd(ts2System rfd/wfd): _rio=相手の出力読み, _wio=相手の入力書き
	ptsWirePipe_(
		sPtr<tinyState> parent,
		sPtr<ts2IO> _rio,
		sPtr<ts2IO> _wio);

	sRptr<tinyState,tinyState>		parent;

	/* -- 送信 API(parent が呼ぶ) -- */
	void	write(int type, const uint8_t *payload, int len);
	void	write_str(int type, sPtr<stdString> str);
	/* C_ARG_PATH/C_ARG_INLINE 用: payload = [arg_index(u32 LE)][str]。
	 * 引数を順不同(パイプライン)で送れるよう各引数に番号を載せる(pigwire.h の定義どおり)。 */
	void	write_arg(int type, uint32_t idx, sPtr<stdString> str);
	void	wend();

protected:
	sPtr<ts2IO>	rio;      // read_c 用
	sPtr<ts2IO>	wio;      // write_c 用
	int		errCode;
	uint32_t	peerPid;
	sArray<uint8_t>	abuf;     /* write_arg の payload 組み立て(yield 安全のためメンバ) */
	sPtr<stdSemaphore>	wlock;     /* write_record 不可分化(count=1) */
	sPtr<tinyState>		wrHolder;  /* 現在 write_record を保持中の caller(再入判定) */

	uint8_t		shdr[WIRE_STREAMHDR_SIZE];   /* streamhdr 送受信バッファ */
	uint8_t		rhdr[WIRE_RECHDR_SIZE];      /* 受信レコードヘッダ */
	uint16_t	rtype;
	uint16_t	rflags;
	uint32_t	rlen;
	sArray<uint8_t>	rpayload;

private:
	void	write_record(uint16_t type, uint16_t flags, const uint8_t *payload, uint32_t len);
	/* write_record の pico_state。hbuf は write_c の bp 安定のためメンバ化(yield 再入で stale 化させない)。 */
	struct {
		PS_PRESET
		uint8_t	hbuf[WIRE_RECHDR_SIZE];
	} ps_write_record;
	TS_DEFARGS
};

TS_END_IMPLEMENT

TS_BEGIN_INTERFACE
#include	"ts2/c++/sRptr.h"
#include	"ts2/c++/sPicoState.h" /* PS_PRESET を _.h のメンバ struct 寸法に */
#include	"pig/c++/pigwire.h"    /* WIRE_STREAMHDR_SIZE / WIRE_RECHDR_SIZE を _.h のメンバ寸法に */
class tinyState;
class ts2IO;
class stdString;
class stdSemaphore;
TS_END_INTERFACE

#endif


/* 双方向 1 本(socketpair 等): read も write も同じ io。 */
ptsWirePipe_::ptsWirePipe_(TS_ARGS0)
        : ptsObject_(parent),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
    rio = _io;
    wio = _io;
    errCode = 0;
    peerPid = 0;
    ps_write_record.__state = psINI;
    ps_write_record.__recursive = 0;
    wlock = thNEW(stdSemaphore,(1));
    wrHolder = thNULL;
}

/* read/write 別 fd(ts2System の rfd/wfd を直結): 子の stdout を rio、stdin を wio。 */
ptsWirePipe_::ptsWirePipe_(TS_ARGS1)
        : ptsObject_(parent),
	  parent(tinyState_::parent)
{
    TS_CPARGS1
    rio = _rio;
    wio = _wio;
    errCode = 0;
    peerPid = 0;
    ps_write_record.__state = psINI;
    ps_write_record.__recursive = 0;
    wlock = thNEW(stdSemaphore,(1));
    wrHolder = thNULL;
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

/* pico_state でヘッダ(psINI)とペイロード(psDO)を分けて書く。
 * io->write_c は送信バッファ不足時に sException で yield し、呼び出し元の状態関数が
 * 先頭から再実行される。素朴に write_c を 2 回呼ぶとペイロード側 yield の再入で
 * ヘッダを二重書きしてしまうため、__state を psDO へ進めて再入時はヘッダを飛ばす。
 * hbuf はメンバ(ps_write_record 内)なので write_c の保持 bp が yield 跨ぎで安定。
 * NB: 呼び出し元(parent)が 1 状態で write_record を複数回呼ぶ場合の二重書きは別問題
 *     (前段 write の完了後に後段 write が yield 再入すると前段が再走)。これは送信側を
 *     状態分割するか pigfAgent 実装時に大規模データで対応(現状テストは小データで未発生)。 */
void
ptsWirePipe_::write_record(uint16_t type, uint16_t flags,
                            const uint8_t *payload, uint32_t len)
{
	/* 不可分化: 1 レコード(ヘッダ+ペイロード)を他の write_record と混線させない。
	 * count=1 の wlock で直列化する。ps_write_record(pico)は単一メンバで全 caller 共有なので、
	 * ロックを pico の switch 内(psINI)で取ると、A が psDO で yield 中に B が入ると B は
	 * __state=psDO に飛んで get() を素通りし破損する。→ get() は switch より前で取り、A 自身の
	 * 再入(yield 再走)では自己デッドロックしないよう wrHolder(保持中 caller)で判定する。 */
	sPtr<tinyState> me = sCallSection::key->caller();
	if ( wrHolder != me ) {
		wlock->get();        /* 取得(他 caller 保持中なら sException で yield → 再走で再取得) */
		wrHolder = me;       /* 以後この write_record 完了(psDO2)まで自分が保持 */
	}

	PS_STATEMENT(ps_write_record,
		PS_DEF(hbuf)
	,
	PS_STATE(psINI)
		wire_put_rechdr(hbuf, type, flags, len);
		wio->write_c(hbuf, WIRE_RECHDR_SIZE);
	PS_STATE(psDO)
		if ( payload && len > 0 )
			wio->write_c((void *)payload, (int)len);
	PS_STATE(psDO2)
		/* ★ 再入 panic 根治(粗技): PS_RETURN() を使わず、__state を psINI に戻すだけで
		 * PS_STATEMENT ブロックを自然脱出させる。release() は下の PS_STATEMENT 外で呼ぶ。
		 * こうすると release() が wlock 待ちの兄弟 worker を **同期 resume** して write_record に
		 * 再入しても、その時点で __sFlag は破棄済み(__recursive=0)なので
		 * 「sPicoState function cannot recursive call」で落ちない。
		 * (本来は「__state を psINI にして return しない」専用マクロを tinyState 側で用意すべき。) */
		wrHolder = thNULL;
		__state = psINI;
	);
	if ( wrHolder == thNULL )   /* この呼びで 1 レコード完了(psDO2 通過)した時だけ解放 */
		wlock->release();
}

void
ptsWirePipe_::write(int type, const uint8_t *payload, int len)
{
	write_record((uint16_t)type, 0, payload, (uint32_t)(len < 0 ? 0 : len));
}

void
ptsWirePipe_::write_str(int type, sPtr<stdString> str)
{
	const char *s = str->get_str();
	write_record((uint16_t)type, 0, (const uint8_t *)s, (uint32_t)::strlen(s));
}

void
ptsWirePipe_::write_arg(int type, uint32_t idx, sPtr<stdString> str)
{
	const char *s = str->get_str();
	uint32_t slen = (uint32_t)::strlen(s);
	abuf.length((int)(4 + slen));
	abuf[0] = (uint8_t)idx;         abuf[1] = (uint8_t)(idx >> 8);
	abuf[2] = (uint8_t)(idx >> 16); abuf[3] = (uint8_t)(idx >> 24);
	if ( slen )
		::memcpy(&abuf[4], s, slen);
	write_record((uint16_t)type, 0, &abuf[0], 4 + slen);
}

void
ptsWirePipe_::wend()
{
	write_record(W_END, 0, 0, 0);
}


/*******************************************
	STATE MACHINE
********************************************/

TS_STATE(INI_ptsObject_START)
{
	/* 自分の streamhdr を送る(writer_pid = 自プロセス pid) */
	wire_put_streamhdr(shdr, osglue_getpid());
	if ( wio->write_c(shdr, WIRE_STREAMHDR_SIZE) != WIRE_STREAMHDR_SIZE ) {
		errCode = -1; return rDO|FIN_START;
	}
	return rDO|ACT_ptsWirePipe_HELLO;
}
TS_STATE(ACT_ptsWirePipe_HELLO)
{
	/* 相手の streamhdr を読む(1 ステート = read_c 1 回。yield 再実行で read_c が再開) */
	if ( rio->read_c(shdr, WIRE_STREAMHDR_SIZE) != WIRE_STREAMHDR_SIZE ) {
		errCode = -1; return rDO|FIN_START;   /* EOF/error: 相手が hdr を送る前に閉じた */
	}
	if ( wire_check_streamhdr(shdr, &peerPid) != WIRE_OK ) {
		errCode = -1; return rDO|FIN_START;   /* magic/version/endian 不一致 */
	}
	parent->eventHandler(thNEW(stdEvent,(TSE_ASSERT,ifThis,(INTEGER64)errCode)));   /* ready 通知 */
	return rDO|ACT_ptsWirePipe_HDR;
}
TS_STATE(ACT_ptsWirePipe_HDR)
{
	int r = rio->read_c(rhdr, WIRE_RECHDR_SIZE);
	if ( r != (int)WIRE_RECHDR_SIZE ) {           /* EOF(0)=相手が閉じた / <0=error */
		errCode = (r < 0) ? -1 : 0;
		return rDO|FIN_START;
	}
	wire_get_rechdr(rhdr, &rtype, &rflags, &rlen);
	if ( rtype == W_END && rlen == 0 )            /* 番兵 = 相手の送信完了 */
		return rDO|FIN_START;
	if ( rlen == 0 ) {                            /* payload 無しレコード: 即 packet 化 */
		parent->eventHandler(thNEW(stdEvent,
			(TSE_PACKET, ifThis, thNEW(ptsWirePacket,(rtype, rflags, (const uint8_t*)0, 0)))));
		return rDO|ACT_ptsWirePipe_HDR;
	}
	return rDO|ACT_ptsWirePipe_PAYLOAD;
}
TS_STATE(ACT_ptsWirePipe_PAYLOAD)
{
	rpayload.length((int)rlen);   /* 同サイズ再要求は no-op(再実行で &rpayload[0] 安定) */
	int r = rio->read_c(&rpayload[0], (int)rlen);
	if ( r != (int)rlen ) {
		errCode = (r < 0) ? -1 : 0;
		return rDO|FIN_START;
	}
	parent->eventHandler(thNEW(stdEvent,
		(TSE_PACKET, ifThis, thNEW(ptsWirePacket,(rtype, rflags, &rpayload[0], rlen)))));
	return rDO|ACT_ptsWirePipe_HDR;
}
TS_STATE(FIN_START)
{
	return rDO|FIN_ptsWirePipe_START;
}
TS_STATE(FIN_ptsWirePipe_START)
{
	parent->eventHandler(thNEW(stdEvent,(TSE_RETURN,ifThis,(INTEGER64)errCode)));
	return rDO|FIN_ptsObject_START;
}
