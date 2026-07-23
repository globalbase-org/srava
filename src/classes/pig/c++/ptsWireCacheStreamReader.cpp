/*
 * ptsWireCacheStreamReader — pigwire キャッシュファイルの reader(ptsObject 派生)。
 *
 * 設計(pigwire_engine.txt §2.4 + cache_streaming_protocol):
 *  - read は OS の blocking call(pread)を使い TS_THREAD 内でループする実装。
 *  - INI: open + streamhdr 検証(magic/ver/endian + writer pid) + 先頭 D_META 読込。
 *  - INI_ptsWireCacheStreamReader_METADATA gate: 派生が meta を検証(対応形式か)。
 *  - INI_ptsWireCacheStreamReader_METADATA_FINISH: parent へ TSE_ASSERT(errCode)。
 *  - ACT_START: 基底は空。派生が ACT スレッドで next_record() を回す。
 *  - FIN: close + parent へ TSE_RETURN(result があれば result、無ければ errCode)。
 *
 *  ストリーミング読み: pread + 自前の論理 offset(consumed) + サイズ先読みで torn record を
 *  読まない。終了は番兵(W_END)オンリー。番兵未着のまま writer pid が消えていたら破損とみなす。
 *  同期はポーリング一択(固定小間隔)。
 *
 *  protected API(派生 TS_THREAD が使う):
 *    next_record()   次の 1 レコードをポーリング読込。1=取得(rec_* 充填), 0=W_END, -1=エラー
 *    rec_type/rec_flags/rec_payload   直近に読んだレコード
 *    meta            先頭 D_META の payload(METADATA gate で派生が検証)
 *    result          派生が ACT 終了時にセット(FIN で parent へ返す)
 */
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/pigwire.h"
#include	"pig/c++/osglue.h"
#include	"ts2/c++/stdEvent.h"
#include	"_ts2/c++/ptsWireCacheStreamReader_.h"

#include	<fcntl.h>
#ifndef O_BINARY               /* POSIX には無い。Windows(MinGW)で binary mesh キャッシュの \r\n 変換を防ぐ */
#  define O_BINARY 0
#endif
#include	<unistd.h>
#include	<sys/stat.h>

CLASS_TINYSTATE(pig/c++/ptsWireCacheStreamReader,pig/c++/ptsObject)

enum { READ_POLL_USEC = 500 };   /* ポーリング固定小間隔(谷間なし前提なのでバックオフ不要) */

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ptsWireCacheStreamReader_(
		sPtr<ptsObject> parent,
		sPtr<stdString> _cacheFileName);

	sRptr<ptsObject,tinyState>		parent;

	/* -- 派生スレッドが使う API。戻り値は下の関数定義のヘッダコメント参照 -- */
	int	next_record();

protected:
	int	fd;
	int	errCode;
	sPtr<stdString>	cacheFileName;
	INTEGER64	consumed;        /* 論理読み出し offset(pread 用) */
	uint32_t	writerPid;       /* streamhdr 由来。死活監視ハンドル */
	sArray<uint8_t>	meta;            /* 先頭 D_META payload(派生が検証) */
	sPtr<stdObject>	result;          /* 派生が ACT 終了時にセット */

	uint16_t	rec_type;
	uint16_t	rec_flags;
	sArray<uint8_t>	rec_payload;

private:
	int	wait_avail(INTEGER64 need);   /* size-consumed>=need まで待つ。1=ok, -1=writer 死亡/エラー */
	TS_DEFARGS
};

TS_END_IMPLEMENT

TS_BEGIN_INTERFACE
#include	"ts2/c++/sRptr.h"
class ptsObject;
class stdString;
TS_END_INTERFACE

#endif


ptsWireCacheStreamReader_::ptsWireCacheStreamReader_(TS_ARGS0)
        : ptsObject_(parent),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
    fd = -1; errCode = 0; consumed = 0; writerPid = 0;
    cacheFileName = _cacheFileName;
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

/* size-consumed が need 分そろうまでポーリング待ち。
 * 谷間なし前提だが、writer が番兵を出さず死亡したら破損とみなして -1。 */
int
ptsWireCacheStreamReader_::wait_avail(INTEGER64 need)
{
	for ( ; ; ) {
		long long fsz = osglue_fsize(fd);   /* Windows で書込中の最新 EOF を得る(CRT fstat は stale) */
		if ( fsz < 0 ) { errCode = -1; return -1; }
		INTEGER64 avail = (INTEGER64)fsz - consumed;
		if ( avail >= need ) return 1;
		/* まだ足りない: writer 生存確認(EOF は終了を意味しないので番兵+PID で判定) */
		if ( osglue_pid_exists(writerPid) == 0 ) {
			/* ★ レース窓: writer が「最後のデータ + W_END を書いて → 即終了」した瞬間、
			 * 上の fstat(古いサイズ)の直後に writer が死ぬと、書き終えたデータを見落として
			 * 破損と誤判定する。プロセス終了で write は durable になっているので、ここで
			 * **もう一度 fstat** し(数回・短い間隔で)、それでも足りなければ初めて破損とする。 */
			for ( int retry = 0 ; retry < 8 ; ++retry ) {
				long long fsz2 = osglue_fsize(fd);
				if ( fsz2 < 0 ) { errCode = -1; return -1; }
				if ( (INTEGER64)fsz2 - consumed >= need ) return 1;   /* 最終書込が見えた */
				::usleep(READ_POLL_USEC);
			}
			errCode = -1; return -1;   /* writer 死亡後も不足 = 本当に不完全(W_END 未着) */
		}
		::usleep(READ_POLL_USEC);
	}
}

/* 次の 1 レコードを読む。torn record を読まないようサイズ先読みしてから pread。
 *   1 : レコード取得(rec_type/rec_flags/rec_payload 充填、consumed 前進)
 *   0 : W_END 番兵に達した(正常終了)
 *  -1 : 読み込みエラー / 番兵未着のまま writer 死亡(errCode セット) */
int
ptsWireCacheStreamReader_::next_record()
{
	uint8_t hdr[WIRE_RECHDR_SIZE];
	if ( wait_avail(WIRE_RECHDR_SIZE) < 0 ) return -1;
	if ( osglue_pread(fd, hdr, WIRE_RECHDR_SIZE, (long long)consumed) != (long)WIRE_RECHDR_SIZE ) {
		errCode = -1; return -1;
	}
	uint16_t type, flags; uint32_t len;
	wire_get_rechdr(hdr, &type, &flags, &len);

	if ( type == W_END && len == 0 ) {     /* 番兵 */
		consumed += WIRE_RECHDR_SIZE;
		return 0;
	}

	INTEGER64 need = (INTEGER64)WIRE_RECHDR_SIZE + (INTEGER64)len;
	if ( wait_avail(need) < 0 ) return -1;  /* 本体未着 → 揃うまで待つ */

	rec_type = type; rec_flags = flags;
	rec_payload.length((int)len);
	if ( len > 0 ) {
		if ( osglue_pread(fd, &rec_payload[0], len, (long long)(consumed + WIRE_RECHDR_SIZE))
		     != (ssize_t)len ) {
			errCode = -1; return -1;
		}
	}
	consumed += need;
	return 1;
}


/*******************************************
	STATE MACHINE
********************************************/

TS_STATE(INI_ptsObject_START)
{
	/* open + streamhdr 検証(magic/ver/endian + writer pid) + 先頭 D_META 読込。
	 * NB: 現状この読込は通常ステート(mtx 下)で行うので writer が hdr/meta を書き終えてから
	 *     reader を起動する前提(逐次/ASSERT トリガ)。完全な早期 attach 対応は後段で。 */
	fd = ::open(cacheFileName->get_str(), O_RDONLY|O_BINARY);   /* O_BINARY: Windows の \r\n 変換で mesh 破損を防ぐ */
	if ( fd < 0 ) { errCode = -1; return rDO|FIN_START; }

	uint8_t shdr[WIRE_STREAMHDR_SIZE];
	if ( wait_avail(WIRE_STREAMHDR_SIZE) < 0 ) return rDO|FIN_START;
	if ( osglue_pread(fd, shdr, WIRE_STREAMHDR_SIZE, 0) != (long)WIRE_STREAMHDR_SIZE ) {
		errCode = -1; return rDO|FIN_START;
	}
	if ( wire_check_streamhdr(shdr, &writerPid) != WIRE_OK ) {
		errCode = -1; return rDO|FIN_START;    /* magic/version/endian 不一致 = 別世代キャッシュ */
	}
	consumed = WIRE_STREAMHDR_SIZE;

	/* 先頭レコードは D_META(writer の INIT gate が書く) */
	int r = next_record();
	if ( r < 0 ) return rDO|FIN_START;
	if ( r == 0 || rec_type != D_META ) { errCode = -1; return rDO|FIN_START; }
	meta.length(rec_payload.length());
	for ( int i = 0; i < rec_payload.length(); ++i ) meta[i] = rec_payload[i];

	return rDO|INI_ptsWireCacheStreamReader_METADATA;
}
TS_STATE(INI_ptsWireCacheStreamReader_METADATA)   /* 派生が meta を検証(対応形式か判定) */
{
	return rDO|INI_ptsWireCacheStreamReader_METADATA_FINISH;
}
TS_STATE(INI_ptsWireCacheStreamReader_METADATA_FINISH)
{
	parent->eventHandler(thNEW(stdEvent,(TSE_ASSERT,ifThis,(INTEGER64)errCode)));
	if ( errCode ) return rDO|FIN_START;
	return rDO|ACT_START;
}
TS_STATE(ACT_START)   /* 基底は空。派生が ACT スレッドで本体読込を回す */
{
	return rDO|FIN_START;
}
TS_STATE(FIN_START)
{
	return rDO|FIN_ptsWireCacheStreamReader_START;
}
TS_STATE(FIN_ptsWireCacheStreamReader_START)
{
	if ( fd >= 0 ) { ::close(fd); fd = -1; }
	if ( result.is_notNull() )
		parent->eventHandler(thNEW(stdEvent,(TSE_RETURN,ifThis,result)));
	else
		parent->eventHandler(thNEW(stdEvent,(TSE_RETURN,ifThis,(INTEGER64)errCode)));
	return rDO|FIN_ptsObject_START;
}
