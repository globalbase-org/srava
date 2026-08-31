/*
 * ptsvdWireCacheStreamReaderGrid — vd (OpenVDB) キャッシュ入力用 reader 派生 (#3434 P2)。
 * gg 版のミラー。META gate で D_META タグを検証し (VD_TAG="VDB " = OpenVDB ネイティブ /
 * "MFM3" = 運搬用メッシュ / "MESH" = cgal 厳密メッシュの昇格読み)、ACT_START で具体型を
 * 生成して decode が pull() でチャンク境界を跨いでバイトを取る。
 */
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/ptsApplication.h"   /* ptsApp 値メンバの完全型(ptsObject.h から移動・#3406 4.2) */
#include	"pig/c++/pigwire.h"
#include	"vd/c++/vdGrid.h"
#include	"_ts2/c++/ptsvdWireCacheStreamReaderGrid_.h"

#include	<string.h>   /* memcmp */

CLASS_TINYSTATE(vd/c++/ptsvdWireCacheStreamReaderGrid,pig/c++/ptsWireCacheStreamReader)


#if 0

TS_BEGIN_IMPLEMENT


class TS_THISCLASS : public TS_BASECLASS {
public:
	ptsvdWireCacheStreamReaderGrid_(
		sPtr<ptsObject> parent,
		sPtr<stdString> _cacheFileName);

	sRptr<ptsObject,tinyState>		parent;

	/* vdGrid の Source 窓口: D_CHUNK ストリームから n バイトを境界跨ぎで取る。 */
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


ptsvdWireCacheStreamReaderGrid_::ptsvdWireCacheStreamReaderGrid_(TS_ARGS0)
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
ptsvdWireCacheStreamReaderGrid_::pull(uint8_t *dst, int n)
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
ptsvdWireCacheStreamReaderGrid_::more()
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

TS_STATE(INI_ptsWireCacheStreamReader_METADATA)   /* D_META タグから具体型(vdGrid/mfCross)を作れるか検証 */
{
	const uint8_t *m = ( meta.length() > 0 ) ? &meta[0] : (const uint8_t*)0;
	if ( vdGeom::create_for_meta(m, meta.length()) == thNULL )
		errCode = -2;      /* vd の対応形式ではない(未知タグ) */
	return rDO|INI_ptsWireCacheStreamReader_METADATA_FINISH;
}
TS_THREAD(ACT_START)                              /* D_CHUNK ストリームを vdGeom(grid / mesh)へ decode */
{
	chunkPos = rec_payload.length();   /* INI の D_META を消費済みにし、最初の pull で D_CHUNK へ */
	pullErr  = 0;
	const uint8_t *mp = ( meta.length() > 0 ) ? &meta[0] : (const uint8_t*)0;
	sPtr<vdGeom> geom = vdGeom::create_for_meta(mp, meta.length());   /* タグで具体型を生成 */
	if ( geom == thNULL ) { errCode = -2; return rDO|FIN_START; }
	struct Src : vdChunkSource {
		ptsvdWireCacheStreamReaderGrid_ *r;
		void pull(uint8_t *dst, int n) { r->pull(dst, n); }
		int  more() { return r->more(); }
	} src;
	src.r = this;
	geom->decode(src);
	if ( pullErr ) { errCode = -1; return rDO|FIN_START; }
	/* ★ 形式は読めたが中身を受け取れない (FloatGrid 以外の .vdb 等)。空を黙って返すと
	 *   volume が 0 になるので、ここでエラーにする。 */
	if ( geom->decode_failed() ) { errCode = -2; return rDO|FIN_START; }
	result = geom;
	return rDO|FIN_START;
}
