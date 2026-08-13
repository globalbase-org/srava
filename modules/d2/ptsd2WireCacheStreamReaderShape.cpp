/*
 * ptsd2WireCacheStreamReaderShape — d2 (2D) キャッシュ入力用 reader 派生 (rev4 次元分担デモ)。
 * ptsd3WireCacheStreamReaderMesh のミラー。META gate で D_META タグ "D2S2" を検証、ACT_START で
 * d2Shape を生成し decode が pull() でチャンク境界をまたいでバイトを取る。
 */
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigwire.h"
#include	"d2/c++/d2Shape.h"
#include	"_ts2/c++/ptsd2WireCacheStreamReaderShape_.h"

#include	<string.h>

CLASS_TINYSTATE(d2/c++/ptsd2WireCacheStreamReaderShape,pig/c++/ptsWireCacheStreamReader)


#if 0

TS_BEGIN_IMPLEMENT


class TS_THISCLASS : public TS_BASECLASS {
public:
	ptsd2WireCacheStreamReaderShape_(
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


ptsd2WireCacheStreamReaderShape_::ptsd2WireCacheStreamReaderShape_(TS_ARGS0)
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
ptsd2WireCacheStreamReaderShape_::pull(uint8_t *dst, int n)
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
ptsd2WireCacheStreamReaderShape_::more()
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

TS_STATE(INI_ptsWireCacheStreamReader_METADATA)   /* D_META タグから具体型(d2Shape)を作れるか検証 */
{
	const uint8_t *m = ( meta.length() > 0 ) ? &meta[0] : (const uint8_t*)0;
	if ( d2Shape::create_for_meta(m, meta.length()) == thNULL )
		errCode = -2;
	return rDO|INI_ptsWireCacheStreamReader_METADATA_FINISH;
}
TS_THREAD(ACT_START)                              /* D_CHUNK ストリームを d2Shape へ decode */
{
	chunkPos = rec_payload.length();
	pullErr  = 0;
	const uint8_t *mp = ( meta.length() > 0 ) ? &meta[0] : (const uint8_t*)0;
	sPtr<d2Shape> shape = d2Shape::create_for_meta(mp, meta.length());
	if ( shape == thNULL ) { errCode = -2; return rDO|FIN_START; }
	struct Src : d2ChunkSource {
		ptsd2WireCacheStreamReaderShape_ *r;
		void pull(uint8_t *dst, int n) { r->pull(dst, n); }
		int  more() { return r->more(); }
	} src;
	src.r = this;
	shape->decode(src);
	if ( pullErr ) { errCode = -1; return rDO|FIN_START; }
	result = shape;
	return rDO|FIN_START;
}
