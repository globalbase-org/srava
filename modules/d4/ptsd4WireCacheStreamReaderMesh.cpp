/*
 * ptsd4WireCacheStreamReaderMesh — d4 mesh キャッシュ入力用 reader 派生 (rev4 Phase D-3)。
 * ptsmfWireCacheStreamReaderMesh のミラー。META gate で D_META タグ "D4M3" を検証、ACT_START で
 * d4Mesh を生成し decode が pull() でチャンク境界をまたいでバイトを取る。基底が open/番兵検出/
 * ポーリング(read-while-write)/TSE_RETURN を担う。
 */
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigwire.h"
#include	"d4/c++/d4Mesh.h"
#include	"_ts2/c++/ptsd4WireCacheStreamReaderMesh_.h"

#include	<string.h>   /* memcmp */

CLASS_TINYSTATE(d4/c++/ptsd4WireCacheStreamReaderMesh,pig/c++/ptsWireCacheStreamReader)


#if 0

TS_BEGIN_IMPLEMENT


class TS_THISCLASS : public TS_BASECLASS {
public:
	ptsd4WireCacheStreamReaderMesh_(
		sPtr<ptsObject> parent,
		sPtr<stdString> _cacheFileName);

	sRptr<ptsObject,tinyState>		parent;

	/* d4Mesh の Source 窓口: D_CHUNK ストリームから n バイトを境界跨ぎで取る。 */
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


ptsd4WireCacheStreamReaderMesh_::ptsd4WireCacheStreamReaderMesh_(TS_ARGS0)
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
ptsd4WireCacheStreamReaderMesh_::pull(uint8_t *dst, int n)
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
ptsd4WireCacheStreamReaderMesh_::more()
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

TS_STATE(INI_ptsWireCacheStreamReader_METADATA)   /* D_META タグから具体型(d4Mesh)を作れるか検証 */
{
	const uint8_t *m = ( meta.length() > 0 ) ? &meta[0] : (const uint8_t*)0;
	if ( d4Mesh::create_for_meta(m, meta.length()) == thNULL )
		errCode = -2;      /* d4 の対応形式ではない(未知タグ) */
	return rDO|INI_ptsWireCacheStreamReader_METADATA_FINISH;
}
TS_THREAD(ACT_START)                              /* D_CHUNK ストリームを d4Mesh へ decode */
{
	chunkPos = rec_payload.length();   /* INI の D_META を消費済みにし、最初の pull で D_CHUNK へ */
	pullErr  = 0;
	const uint8_t *mp = ( meta.length() > 0 ) ? &meta[0] : (const uint8_t*)0;
	sPtr<d4Mesh> mesh = d4Mesh::create_for_meta(mp, meta.length());
	if ( mesh == thNULL ) { errCode = -2; return rDO|FIN_START; }
	struct Src : d4ChunkSource {
		ptsd4WireCacheStreamReaderMesh_ *r;
		void pull(uint8_t *dst, int n) { r->pull(dst, n); }
		int  more() { return r->more(); }
	} src;
	src.r = this;
	mesh->decode(src);
	if ( pullErr ) { errCode = -1; return rDO|FIN_START; }
	result = mesh;
	return rDO|FIN_START;
}
