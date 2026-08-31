/*
 * ptsnfWireCacheStreamReaderMesh — nf(Nef)mesh キャッシュ入力用 reader 派生 (#3433 P1)。
 * cg/mf 版のミラー。META gate で D_META タグを検証し (自型 "NEF3" と cg の "MESH" を受理 =
 * ★MESH→nf の昇格読みはフレーミングが同一なので同じ decode 経路)、ACT_START で nfMesh を
 * 生成して decode が pull() でチャンク境界をまたいでバイトを取り Nef を再構成する。
 */
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigwire.h"
#include	"nf/c++/nfMesh.h"
#include	"_ts2/c++/ptsnfWireCacheStreamReaderMesh_.h"

#include	<string.h>   /* memcmp */

CLASS_TINYSTATE(nf/c++/ptsnfWireCacheStreamReaderMesh,pig/c++/ptsWireCacheStreamReader)


#if 0

TS_BEGIN_IMPLEMENT


class TS_THISCLASS : public TS_BASECLASS {
public:
	ptsnfWireCacheStreamReaderMesh_(
		sPtr<ptsObject> parent,
		sPtr<stdString> _cacheFileName);

	sRptr<ptsObject,tinyState>		parent;

	/* nfMesh の Source 窓口: D_CHUNK ストリームから n バイトを境界跨ぎで取る。 */
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


ptsnfWireCacheStreamReaderMesh_::ptsnfWireCacheStreamReaderMesh_(TS_ARGS0)
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
ptsnfWireCacheStreamReaderMesh_::pull(uint8_t *dst, int n)
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
ptsnfWireCacheStreamReaderMesh_::more()
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

TS_STATE(INI_ptsWireCacheStreamReader_METADATA)   /* D_META タグが nf の受理形式か検証 */
{
	const uint8_t *m = ( meta.length() > 0 ) ? &meta[0] : (const uint8_t*)0;
	if ( nfGeom::create_for_meta(m, meta.length()) == thNULL )
		errCode = -2;      /* nf の対応形式ではない(未知タグ) */
	return rDO|INI_ptsWireCacheStreamReader_METADATA_FINISH;
}
TS_THREAD(ACT_START)                              /* D_CHUNK ストリームを nfGeom へ decode */
{
	chunkPos = rec_payload.length();   /* INI の D_META を消費済みにし、最初の pull で D_CHUNK へ */
	pullErr  = 0;
	const uint8_t *mp = ( meta.length() > 0 ) ? &meta[0] : (const uint8_t*)0;
	sPtr<nfGeom> geom = nfGeom::create_for_meta(mp, meta.length());
	if ( geom == thNULL ) { errCode = -2; return rDO|FIN_START; }
	struct Src : nfChunkSource {
		ptsnfWireCacheStreamReaderMesh_ *r;
		void pull(uint8_t *dst, int n) { r->pull(dst, n); }
		int  more() { return r->more(); }
	} src;
	src.r = this;
	geom->decode(src);
	if ( pullErr ) { errCode = -1; return rDO|FIN_START; }
	/* ★ 境界メッシュから Nef を作れなかった (自己交差など Nef の前提を満たさない入力)。
	 *   黙って空集合を返すと volume が 0 になるので、ここでエラーにする。
	 *   ★これを入れる前は CGAL の assertion で **agent プロセスごと落ちて**いた
	 *   ("agent closed unexpectedly" としか出ず原因が分からなかった)。 */
	{
		sPtr<nfMesh> m3 = sPtr<nfMesh>::d_cast(geom);
		if ( m3.is_notNull() && m3->build_failed() ) { errCode = -2; return rDO|FIN_START; }
	}
	result = geom;
	return rDO|FIN_START;
}
