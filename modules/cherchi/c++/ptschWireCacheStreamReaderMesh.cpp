/*
 * ptschWireCacheStreamReaderMesh — cherchi mesh キャッシュ入力用 reader 派生 (#3438 P6)。
 * mf 版のミラー。META gate で D_META タグ (CH_TAG="MFM3"。manifold と共有する形式) を検証し、
 * ACT_START で chMesh を生成して decode が pull() でチャンク境界を跨いでバイトを取る。
 */
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/ptsApplication.h"   /* ptsApp 値メンバの完全型(ptsObject.h から移動・#3406 4.2) */
#include	"pig/c++/pigwire.h"
#include	"ch/c++/chMesh.h"
#include	"_ts2/c++/ptschWireCacheStreamReaderMesh_.h"

#include	<string.h>   /* memcmp */

CLASS_TINYSTATE(ch/c++/ptschWireCacheStreamReaderMesh,pig/c++/ptsWireCacheStreamReader)


#if 0

TS_BEGIN_IMPLEMENT


class TS_THISCLASS : public TS_BASECLASS {
public:
	ptschWireCacheStreamReaderMesh_(
		sPtr<ptsObject> parent,
		sPtr<stdString> _cacheFileName);

	sRptr<ptsObject,tinyState>		parent;

	/* chMesh の Source 窓口: D_CHUNK ストリームから n バイトを境界跨ぎで取る。 */
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


ptschWireCacheStreamReaderMesh_::ptschWireCacheStreamReaderMesh_(TS_ARGS0)
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
ptschWireCacheStreamReaderMesh_::pull(uint8_t *dst, int n)
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
ptschWireCacheStreamReaderMesh_::more()
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

TS_STATE(INI_ptsWireCacheStreamReader_METADATA)   /* D_META タグから具体型(chMesh/mfCross)を作れるか検証 */
{
	const uint8_t *m = ( meta.length() > 0 ) ? &meta[0] : (const uint8_t*)0;
	if ( chGeom::create_for_meta(m, meta.length()) == thNULL )
		errCode = -2;      /* mf の対応形式ではない(未知タグ) */
	return rDO|INI_ptsWireCacheStreamReader_METADATA_FINISH;
}
TS_THREAD(ACT_START)                              /* D_CHUNK ストリームを chGeom(3D mesh / 2D cross)へ decode */
{
	chunkPos = rec_payload.length();   /* INI の D_META を消費済みにし、最初の pull で D_CHUNK へ */
	pullErr  = 0;
	const uint8_t *mp = ( meta.length() > 0 ) ? &meta[0] : (const uint8_t*)0;
	sPtr<chGeom> geom = chGeom::create_for_meta(mp, meta.length());   /* タグで具体型を生成 */
	if ( geom == thNULL ) { errCode = -2; return rDO|FIN_START; }
	struct Src : chChunkSource {
		ptschWireCacheStreamReaderMesh_ *r;
		void pull(uint8_t *dst, int n) { r->pull(dst, n); }
		int  more() { return r->more(); }
	} src;
	src.r = this;
	geom->decode(src);
	if ( pullErr ) { errCode = -1; return rDO|FIN_START; }
	/* ★ #3433: 形式は読めたが mf の表現力で受け取れない (SNC 形式の NEF3 等)。空 mesh を黙って
	 *   返すと volume が 0 になるので、ここでエラーにする。 */
	if ( geom->decode_failed() ) { errCode = -2; return rDO|FIN_START; }
	result = geom;
	return rDO|FIN_START;
}
