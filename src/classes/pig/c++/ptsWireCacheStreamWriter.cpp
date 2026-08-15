/*
 * ptsWireCacheStreamWriter — pigwire キャッシュファイルの writer(ptsObject 派生)。
 *
 * 設計(pigwire_engine.txt §2.2 + 合意事項):
 *  - write は OS の blocking call を使い TS_THREAD 内でループする実装。
 *  - INI: ファイルオープン + ストリームヘッダ書き込み。
 *  - INI_ptsWireCacheStreamWriter_INIT gate: 派生がここで D_META を書く。
 *  - INI 完了後 parent へ TSE_ASSERT(errCode) を送る。
 *  - ACT_START: 基底は空。派生が ACT スレッドで上書きして本体書き込みを行う。
 *  - FIN: バッファフラッシュ + END 番兵 + close + parent へ TSE_RETURN。
 *
 *  protected API(派生 TS_THREAD が使う):
 *    write_d_text(str)          D_TEXT レコードを書く
 *    d_chunk(data, size)        D_CHUNK データをバッファリングして書く
 *    d_ref_input(...)           D_REF INPUT レコードを書く
 *    d_ref_output(...)          D_REF OUTPUT レコードを書く
 */
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/ptsApplication.h"   /* ptsApp 値メンバの完全型(ptsObject.h から移動・#3406 4.2) */
#include	"pig/c++/pigData.h"
#include	"pig/c++/pigwire.h"
#include	"pig/c++/osglue.h"
#include	"ts2/c++/stdEvent.h"
#include	"_ts2/c++/ptsWireCacheStreamWriter_.h"

#include	<fcntl.h>
#ifndef O_BINARY               /* POSIX には無い(text/binary 区別が無い)。Windows(MinGW)のみ実体を持つ */
#  define O_BINARY 0
#endif
#include	<unistd.h>
#include	<string.h>
#include	<errno.h>

CLASS_TINYSTATE(pig/c++/ptsWireCacheStreamWriter,pig/c++/ptsObject)

/* バッファ寸法は公開ヘッダ(ptsWireCacheStreamWriter.h)の PTS_WIRE_CHUNK_BUF_SIZE と一致 */
enum { CHUNK_BUF_SIZE = PTS_WIRE_CHUNK_BUF_SIZE };

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ptsWireCacheStreamWriter_(
		sPtr<ptsObject> parent,
		sPtr<stdString> _cacheFileName);

	sRptr<ptsObject,tinyState>		parent;

	/* -- protected API -- */
	void write_d_meta(const uint8_t *data, int size);   /* INIT gate で派生が書く */
	void write_d_text(sPtr<stdString> str);
	void d_chunk(const uint8_t *data, int size);
	void d_ref_input(sPtr<stdString> path, INTEGER64 size,
	                 INTEGER64 mtime, pHashKeyType chash);
	/* OUTPUT も INPUT と同形: path + size + mtime + content_hash(出力ファイル中身の hash)。
	 * (path,size,mtime) は安いゲート、content_hash が権威キー(mtime 偽装等の false-hit 回避)。 */
	void d_ref_output(sPtr<stdString> path, INTEGER64 size,
	                  INTEGER64 mtime, pHashKeyType chash);

protected:
	int	fd;
	int	errCode;
	sPtr<stdString>	cacheFileName;
	uint8_t	chunkBuf[PTS_WIRE_CHUNK_BUF_SIZE];
	int	chunkBufLen;

private:
	int  write_full(const void *buf, uint32_t len);
	void write_record(uint16_t type, uint16_t flags,
	                  const uint8_t *payload, uint32_t len);
	void flush_chunk();
	TS_DEFARGS
};

TS_END_IMPLEMENT

TS_BEGIN_INTERFACE
#include	"ts2/c++/sRptr.h"
/* D_CHUNK バッファ寸法。生成 _.h の chunkBuf[] が使うので _pb.h(本ブロック)へ出す
 * = 生成 _.h が公開ヘッダ経由で本定義を impl クラス定義より前に取り込む。 */
enum { PTS_WIRE_CHUNK_BUF_SIZE = 8192 };
class ptsObject;
class stdString;
TS_END_INTERFACE

#endif


ptsWireCacheStreamWriter_::ptsWireCacheStreamWriter_(TS_ARGS0)
        : ptsObject_(parent),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
    fd = -1; errCode = 0; chunkBufLen = 0;
    cacheFileName = _cacheFileName;
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

/* 全量書き込み: ::write は要求量より少なく書いて返りうる (EINTR・ENOSPC 部分書き等)。
 * 途中で欠けるとレコード framing がずれたまま W_END まで書かれ「完成品に見える壊れた
 * キャッシュ」が焼き付くので、ここで全量保証する。成功=0 / エラー=-1。 */
int
ptsWireCacheStreamWriter_::write_full(const void *buf, uint32_t len)
{
	const uint8_t *p = (const uint8_t *)buf;
	while ( len > 0 ) {
		ssize_t n = ::write(fd, p, len);
		if ( n < 0 ) {
			if ( errno == EINTR ) continue;
			return -1;
		}
		if ( n == 0 ) return -1;   /* 前進なし: 無限ループ回避 */
		p   += n;
		len -= (uint32_t)n;
	}
	return 0;
}

void
ptsWireCacheStreamWriter_::write_record(uint16_t type, uint16_t flags,
                                         const uint8_t *payload, uint32_t len)
{
	if ( fd < 0 || errCode ) return;   /* 一度失敗したら以降は全部スキップ */
	uint8_t hdr[WIRE_RECHDR_SIZE];
	wire_put_rechdr(hdr, type, flags, len);
	if ( write_full(hdr, WIRE_RECHDR_SIZE) < 0 ) { errCode = -2; return; }
	if ( payload && len > 0 )
		if ( write_full(payload, len) < 0 ) errCode = -2;
}

void
ptsWireCacheStreamWriter_::flush_chunk()
{
	if ( chunkBufLen <= 0 ) return;
	write_record(D_CHUNK, 0, chunkBuf, (uint32_t)chunkBufLen);
	chunkBufLen = 0;
}

void
ptsWireCacheStreamWriter_::write_d_meta(const uint8_t *data, int size)
{
	write_record(D_META, 0, data, (uint32_t)(size < 0 ? 0 : size));
}

void
ptsWireCacheStreamWriter_::write_d_text(sPtr<stdString> str)
{
	const char *s = str->get_str();
	uint32_t len  = (uint32_t)::strlen(s);
	write_record(D_TEXT, 0, (const uint8_t *)s, len);
}

void
ptsWireCacheStreamWriter_::d_chunk(const uint8_t *data, int size)
{
	int pos = 0;
	while ( pos < size ) {
		int room = CHUNK_BUF_SIZE - chunkBufLen;
		int copy = (size - pos) < room ? (size - pos) : room;
		::memcpy(chunkBuf + chunkBufLen, data + pos, copy);
		chunkBufLen += copy;
		pos         += copy;
		if ( chunkBufLen >= CHUNK_BUF_SIZE ) flush_chunk();
	}
}

void
ptsWireCacheStreamWriter_::d_ref_input(sPtr<stdString> path, INTEGER64 sz,
                                        INTEGER64 mtime, pHashKeyType chash)
{
	/* payload: kind(u8=1) + path(pascal) + size(i64) + mtime(i64) + chash(i64) */
	const char *ps = path->get_str(); uint32_t pl = (uint32_t)::strlen(ps);
	uint32_t paylen = 1 + 2 + pl + 8 + 8 + 8;
	sArray<uint8_t> buf; buf.length((int)paylen);
	int o = 0;
	buf[o++] = 1;                           /* D_REF_KIND INPUT */
	buf[o++] = (uint8_t)(pl);  buf[o++] = (uint8_t)(pl>>8);
	for (uint32_t i = 0; i < pl; ++i) buf[o++] = (uint8_t)ps[i];
	for (int s = 0; s < 8; ++s) buf[o++] = (uint8_t)(sz >> (s*8));
	for (int s = 0; s < 8; ++s) buf[o++] = (uint8_t)(mtime >> (s*8));
	for (int s = 0; s < 8; ++s) buf[o++] = (uint8_t)(chash >> (s*8));
	write_record(D_REF, 0, &buf[0], paylen);
}

void
ptsWireCacheStreamWriter_::d_ref_output(sPtr<stdString> path, INTEGER64 sz,
                                         INTEGER64 mtime, pHashKeyType chash)
{
	/* payload: kind(u8=2) + path(pascal) + size(i64) + mtime(i64) + content_hash(i64) */
	const char *ps = path->get_str(); uint32_t pl = (uint32_t)::strlen(ps);
	uint32_t paylen = 1 + 2 + pl + 8 + 8 + 8;
	sArray<uint8_t> buf; buf.length((int)paylen);
	int o = 0;
	buf[o++] = 2;                           /* D_REF_KIND OUTPUT */
	buf[o++] = (uint8_t)(pl); buf[o++] = (uint8_t)(pl>>8);
	for (uint32_t i = 0; i < pl; ++i) buf[o++] = (uint8_t)ps[i];
	for (int s = 0; s < 8; ++s) buf[o++] = (uint8_t)(sz >> (s*8));
	for (int s = 0; s < 8; ++s) buf[o++] = (uint8_t)(mtime >> (s*8));
	for (int s = 0; s < 8; ++s) buf[o++] = (uint8_t)(chash >> (s*8));
	write_record(D_REF, 0, &buf[0], paylen);
}


/*******************************************
	STATE MACHINE
********************************************/

TS_STATE(INI_ptsObject_START)
{
	/* ファイルを作成してストリームヘッダを書く */
	/* O_BINARY 必須(Windows): 無いと MinGW 既定のテキストモードで \n↔\r\n 変換が起き binary
	 * mesh キャッシュが破損する(サイズもずれる)。POSIX では O_BINARY は 0(下で定義)。 */
	fd = ::open(cacheFileName->get_str(), O_WRONLY|O_CREAT|O_TRUNC|O_BINARY, 0644);
	if ( fd < 0 ) { errCode = -1; return rDO|FIN_START; }
	uint8_t hdr[WIRE_STREAMHDR_SIZE];
	wire_put_streamhdr(hdr, osglue_getpid());
	if ( write_full(hdr, WIRE_STREAMHDR_SIZE) < 0 ) { errCode = -2; return rDO|FIN_START; }
	return rDO|INI_ptsWireCacheStreamWriter_INIT;
}
TS_STATE(INI_ptsWireCacheStreamWriter_INIT)   /* 派生がここで D_META 等を書く */
{
	return rDO|INI_ptsWireCacheStreamWriter_DONE;
}
TS_STATE(INI_ptsWireCacheStreamWriter_DONE)
{
	parent->eventHandler(thNEW(stdEvent,(TSE_ASSERT,ifThis,(INTEGER64)errCode)));
	if ( errCode ) return rDO|FIN_START;
	return rDO|ACT_START;
}
TS_STATE(ACT_START)   /* 基底は空。派生が ACT スレッドで上書き */
{
	return rDO|FIN_START;
}
TS_STATE(FIN_START)
{
	return rDO|FIN_ptsWireCacheStreamWriter_START;
}
TS_STATE(FIN_ptsWireCacheStreamWriter_START)
{
	if ( fd >= 0 ) {
		/* エラー時は W_END を書かない: 番兵なしファイルは reader が既存の仕組み
		 * (番兵+writer PID 生存確認) で「未完成」と扱う。書いてしまうと壊れた
		 * キャッシュが完成品に化けて焼き付く。 */
		if ( errCode == 0 ) {
			flush_chunk();
			write_record(W_END, 0, 0, 0);   /* END 番兵 */
		}
		::close(fd); fd = -1;
	}
	parent->eventHandler(thNEW(stdEvent,(TSE_RETURN,ifThis,(INTEGER64)errCode)));
	cacheFileName = thNULL;   /* §9 */
	return rDO|FIN_ptsObject_START;
}
