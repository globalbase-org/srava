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
#include	"pig/c++/ptsApplication.h"   /* ptsApp 値メンバの完全型(ptsObject.h から移動・#3406 4.2) */
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
	/* ★ 終了条件 (2026-08-02 メモ §7.1): **送信終端と受信終端の両方**が揃って初めて FIN する。
	 *   sentEnd     : parent が wend() を呼んだ (自分の送信が終わった)
	 *   gotPeerEnd  : 相手の W_END 番兵を読んだ (相手の送信が終わった)
	 * 片方だけで FIN すると、受信終端後もまだ応答を書く側 (agent) が書けなくなる。
	 * 異常 (read/write エラー・EOF) はこの限りでなく即 FIN。
	 *   assertSent/retSent : TSE_ASSERT / TSE_RETURN を **必ず 1 回・2 回以上送らない**ための番人 (§1)。 */
	int		sentEnd;
	int		gotPeerEnd;
	int		assertSent;
	int		retSent;
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
	void	send_assert_once();
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
    sentEnd = 0;
    gotPeerEnd = 0;
    assertSent = 0;
    retSent = 0;
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
    sentEnd = 0;
    gotPeerEnd = 0;
    assertSent = 0;
    retSent = 0;
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
	/* ★ 既に終了に入っていれば何もしない (§7.2)。FIN/ZOM 後に書こうとしても wio は使えず、
	 * 中途半端なレコードを吐くだけなので黙って捨てる (呼び元は上位でエラーを判断する)。 */
	if ( C_TEST(tinyState_::state(), C_ZOM|C_FIN) )
		return;
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

/* ★ TSE_ASSERT は成功・失敗を問わず **必ず 1 回だけ** parent へ送る (§7.3)。
 * 失敗時は errCode を msg_int に載せる → parent には TSE_ASSERT と TSE_RETURN が続けて届く。 */
void
ptsWirePipe_::send_assert_once()
{
	if ( assertSent )
		return;
	assertSent = 1;
	parent->eventHandler(thNEW(stdEvent,(TSE_ASSERT,ifThis,(INTEGER64)errCode)));
}

/* 番兵を送って「自分の送信は終わり」を立てる。受信終端が既に来ていれば FIN へ進めるよう起こす。
 * parent の状態関数から呼ばれるので、ここで状態遷移は返せない (wakeup で DRAIN を再走させる)。 */
void
ptsWirePipe_::wend()
{
	if ( sentEnd )
		return;              /* 番兵は 1 回だけ */
	write_record(W_END, 0, 0, 0);
	sentEnd = 1;
	wakeup();
}


/*******************************************
	STATE MACHINE
********************************************/

TS_STATE(INI_ptsObject_START)
{
	/* 自分の streamhdr を送る(writer_pid = 自プロセス pid) */
	wire_put_streamhdr(shdr, osglue_getpid(), osglue_pid_starttime(osglue_getpid()));
	if ( wio->write_c(shdr, WIRE_STREAMHDR_SIZE) != WIRE_STREAMHDR_SIZE ) {
		errCode = -1;
		send_assert_once();   /* 失敗でも ASSERT は必ず 1 回 (§7.3) */
		return rDO|FIN_START;
	}
	return rDO|ACT_ptsWirePipe_HELLO;
}
TS_STATE(ACT_ptsWirePipe_HELLO)
{
	/* 相手の streamhdr を読む(1 ステート = read_c 1 回。yield 再実行で read_c が再開) */
	if ( rio->read_c(shdr, WIRE_STREAMHDR_SIZE) != WIRE_STREAMHDR_SIZE ) {
		errCode = -1;
		send_assert_once();                   /* EOF/error: 相手が hdr を送る前に閉じた */
		return rDO|FIN_START;
	}
	if ( wire_check_streamhdr(shdr, &peerPid) != WIRE_OK ) {
		errCode = -1;
		send_assert_once();                   /* magic/version/endian 不一致 */
		return rDO|FIN_START;
	}
	send_assert_once();                           /* ready 通知 (errCode=0) */
	return rDO|ACT_ptsWirePipe_HDR;
}
TS_STATE(ACT_ptsWirePipe_HDR)
{
	int r = rio->read_c(rhdr, WIRE_RECHDR_SIZE);
	if ( r != (int)WIRE_RECHDR_SIZE ) {
		/* ★ ここへ来るのは **異常** だけ (§7.1)。正常な相手の終端は W_END 番兵で来る。
		 * 番兵を見ずに EOF(0) を踏んだ = 相手が黙って閉じた/落ちた。read error(<0) と同じく
		 * errCode を立てて即 FIN する (正常終了 errCode=0 と区別できるようにする)。 */
		errCode = -1;
		return rDO|FIN_START;
	}
	wire_get_rechdr(rhdr, &rtype, &rflags, &rlen);
	if ( rtype == W_END && rlen == 0 ) {
		/* 相手の送信完了。**自分の送信 (wend) が済むまでは FIN しない** — 済んでいなければ
		 * DRAIN で待つ。ここで FIN すると、受信終端後に応答を書く側 (agent) が書けなくなる。 */
		gotPeerEnd = 1;
		return rDO|ACT_ptsWirePipe_DRAIN;
	}
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
		errCode = -1;   /* payload 途中での EOF/error は常に異常 */
		return rDO|FIN_START;
	}
	parent->eventHandler(thNEW(stdEvent,
		(TSE_PACKET, ifThis, thNEW(ptsWirePacket,(rtype, rflags, &rpayload[0], rlen)))));
	return rDO|ACT_ptsWirePipe_HDR;
}
TS_STATE(ACT_ptsWirePipe_DRAIN)   /* 受信終端後: 自分の送信終端 (wend) が済むのを待つ */
{
	if ( sentEnd )
		return rDO|FIN_START;   /* 送受信とも終端 → 正常終了 */
	return 0;                       /* wend() の wakeup で再走する */
}

TS_STATE(FIN_START)
{
	return rDO|FIN_ptsWirePipe_START;
}
TS_STATE(FIN_ptsWirePipe_START)
{
	/* ★ TSE_ASSERT / TSE_RETURN は理由を問わず **必ず 1 回・2 回以上送らない** (§1)。
	 * ASSERT を出す前に落ちた経路 (INI の write 失敗など) でも、ここで必ず 1 回は出す。 */
	send_assert_once();
	if ( ! retSent ) {
		retSent = 1;
		parent->eventHandler(thNEW(stdEvent,(TSE_RETURN,ifThis,(INTEGER64)errCode)));
	}
	/* ★ 終了時点で自分が握る sPtr を手放す (§9)。rio/wio の close は parent (ptsMediatorExternal /
	 * ptsAgentApplication) が TSE_RETURN 受信後に行う — 所有者がそちらのため。
	 * wrHolder は write_record 保持中の caller で、中断時に掴んだままになり得るので必ず落とす。 */
	rio      = thNULL;
	wio      = thNULL;
	wlock    = thNULL;
	wrHolder = thNULL;
	return rDO|FIN_ptsObject_START;
}
