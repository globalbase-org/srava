/*
 * ptsguWireCacheStreamReaderGeom — 中立幾何キャッシュ入力用 reader 派生 (#3527)。
 *   ptsptWireCacheStreamReaderCloud のミラー。META gate で D_META タグを検証し、
 *   ACT_START で guGeom を生成して decode が pull() でチャンク境界をまたいでバイトを取る。
 */
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigwire.h"
#include	"gu/c++/guGeom.h"
#include	"_ts2/c++/ptsguWireCacheStreamReaderGeom_.h"

#include	<string.h>
#include	<stdio.h>

CLASS_TINYSTATE(gu/c++/ptsguWireCacheStreamReaderGeom,pig/c++/ptsWireCacheStreamReader)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ptsguWireCacheStreamReaderGeom_(
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

ptsguWireCacheStreamReaderGeom_::ptsguWireCacheStreamReaderGeom_(TS_ARGS0)
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
ptsguWireCacheStreamReaderGeom_::pull(uint8_t *dst, int n)
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
ptsguWireCacheStreamReaderGeom_::more()
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

TS_STATE(INI_ptsWireCacheStreamReader_METADATA)   /* D_META タグから guGeom を作れるか検証 */
{
	const uint8_t *m = ( meta.length() > 0 ) ? &meta[0] : (const uint8_t*)0;
	if ( guGeom::create_for_meta(m, meta.length()) == thNULL ) {
		/* ★ #3479: どの形式を誰が読めなかったのかを言う。 */
		char b[128];
		::snprintf(b, sizeof b, "module '%s' has no reader for format '%.4s'",
		           GU_MODULE_NAME, ( meta.length() >= 4 ) ? (const char*)m : "????");
		set_err(-2, b);
	}
	return rDO|INI_ptsWireCacheStreamReader_METADATA_FINISH;
}
TS_THREAD(ACT_START)                              /* D_CHUNK ストリームを guGeom へ decode */
{
	chunkPos = rec_payload.length();   /* INI の D_META を消費済みにする */
	pullErr  = 0;
	const uint8_t *mp = ( meta.length() > 0 ) ? &meta[0] : (const uint8_t*)0;
	sPtr<guGeom> g = guGeom::create_for_meta(mp, meta.length());
	if ( g == thNULL ) {
		char b[128];
		::snprintf(b, sizeof b, "module '%s' has no reader for format '%.4s'",
		           GU_MODULE_NAME, ( meta.length() >= 4 ) ? (const char*)mp : "????");
		set_err(-2, b);
		return rDO|FIN_START;
	}
	struct Src : guChunkSource {
		ptsguWireCacheStreamReaderGeom_ *r;
		void pull(uint8_t *dst, int n) { r->pull(dst, n); }
		int  more() { return r->more(); }
	} src;
	src.r = this;
	g->decode(src);
	if ( pullErr ) { set_err(-1, "the cache stream ended or could not be read while decoding"); return rDO|FIN_START; }
	if ( g->decode_failed() ) { set_err(-2, g->decode_why()); return rDO|FIN_START; }
	result = g;
	return rDO|FIN_START;
}
