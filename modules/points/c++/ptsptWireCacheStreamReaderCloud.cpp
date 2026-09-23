/*
 * ptsptWireCacheStreamReaderCloud — 点群キャッシュ入力用 reader 派生 (#3528)。
 *   ptsggWireCacheStreamReaderMesh のミラー。META gate で D_META タグ ("PTC2"/"PTC3") を検証し、
 *   ACT_START で ptCloud を生成して decode が pull() でチャンク境界をまたいでバイトを取る。
 *
 * ★ この reader は **モジュール共通** (libsrava_pt)。点群を消費するモジュール (cgal / geogram / …)
 *   は自前の reader を書かず、codec 行で ptCloud::WIRE をそのまま並べる。
 */
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigwire.h"
#include	"pt/c++/ptCloud.h"
#include	"_ts2/c++/ptsptWireCacheStreamReaderCloud_.h"

#include	<string.h>
#include	<stdio.h>

CLASS_TINYSTATE(pt/c++/ptsptWireCacheStreamReaderCloud,pig/c++/ptsWireCacheStreamReader)


#if 0

TS_BEGIN_IMPLEMENT


class TS_THISCLASS : public TS_BASECLASS {
public:
	ptsptWireCacheStreamReaderCloud_(
		sPtr<ptsObject> parent,
		sPtr<stdString> _cacheFileName);

	sRptr<ptsObject,tinyState>		parent;

	void	pull(uint8_t *dst, int n);
	int	more();
protected:
	int	chunkPos;
	int	pullErr;
private:
	TS_DEFARGS
};

TS_END_IMPLEMENT

TS_BEGIN_INTERFACE
#include	"ts2/c++/sRptr.h"
#include	<stdint.h>
class ptsObject;
class stdString;
TS_END_INTERFACE

#endif


ptsptWireCacheStreamReaderCloud_::ptsptWireCacheStreamReaderCloud_(TS_ARGS0)
        : ptsWireCacheStreamReader_(parent, _cacheFileName),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
    chunkPos = 0;
    pullErr  = 0;
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
ptsptWireCacheStreamReaderCloud_::pull(uint8_t *dst, int n)
{
	int got = 0;
	while ( got < n ) {
		while ( chunkPos >= rec_payload.length() ) {
			int r = next_record();
			if ( r <= 0 ) {
				pullErr = 1;
				while ( got < n ) dst[got++] = 0;
				return;
			}
			chunkPos = 0;
			if ( rec_type != D_CHUNK )
				chunkPos = rec_payload.length();
		}
		int avail = rec_payload.length() - chunkPos;
		int take  = ( n - got < avail ) ? (n - got) : avail;
		for ( int k = 0 ; k < take ; ++k ) dst[got + k] = rec_payload[chunkPos + k];
		got      += take;
		chunkPos += take;
	}
}

int
ptsptWireCacheStreamReaderCloud_::more()
{
	while ( chunkPos >= rec_payload.length() ) {
		int r = next_record();
		if ( r <= 0 )
			return 0;
		chunkPos = 0;
		if ( rec_type != D_CHUNK )
			chunkPos = rec_payload.length();
	}
	return 1;
}


/*******************************************
	STATE MACHINE
********************************************/

TS_STATE(INI_ptsWireCacheStreamReader_METADATA)   /* D_META タグから ptCloud を作れるか検証 */
{
	const uint8_t *m = ( meta.length() > 0 ) ? &meta[0] : (const uint8_t*)0;
	if ( ptCloud::create_for_meta(m, meta.length()) == thNULL ) {
		/* ★ #3479: どの形式を誰が読めなかったのかを言う。 */
		char b[128];
		::snprintf(b, sizeof b, "module '%s' has no reader for format '%.4s'",
		           PT_MODULE_NAME, ( meta.length() >= 4 ) ? (const char*)m : "????");
		set_err(-2, b);
	}
	return rDO|INI_ptsWireCacheStreamReader_METADATA_FINISH;
}
TS_THREAD(ACT_START)                              /* D_CHUNK ストリームを ptCloud へ decode */
{
	chunkPos = rec_payload.length();   /* INI の D_META を消費済みにし、最初の pull で D_CHUNK へ */
	pullErr  = 0;
	const uint8_t *mp = ( meta.length() > 0 ) ? &meta[0] : (const uint8_t*)0;
	sPtr<ptCloud> pc = ptCloud::create_for_meta(mp, meta.length());   /* タグで次元が決まる */
	if ( pc == thNULL ) {
		char b[128];
		::snprintf(b, sizeof b, "module '%s' has no reader for format '%.4s'",
		           PT_MODULE_NAME, ( meta.length() >= 4 ) ? (const char*)mp : "????");
		set_err(-2, b);
		return rDO|FIN_START;
	}
	struct Src : ptChunkSource {
		ptsptWireCacheStreamReaderCloud_ *r;
		void pull(uint8_t *dst, int n) { r->pull(dst, n); }
		int  more() { return r->more(); }
	} src;
	src.r = this;
	pc->decode(src);
	if ( pullErr ) { set_err(-1, "the cache stream ended or could not be read while decoding"); return rDO|FIN_START; }
	if ( pc->decode_failed() ) { set_err(-2, pc->decode_why()); return rDO|FIN_START; }
	result = pc;
	return rDO|FIN_START;
}
