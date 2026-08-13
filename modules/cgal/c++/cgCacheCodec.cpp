/*
 * cgCacheCodec — CGAL カーネルのキャッシュコーデック定義 TU (#3406 / .so 化 Phase 4③')。
 *
 * ★ Phase 4③': 従来の「静的初期化で register_codec を自己登録」を廃し、**descriptor.codecs 配列**
 *   (cgal_codecs) を extern 公開する。cgatsAgent.cpp の記述子がこれを指し、ローダ (register_descriptor)
 *   が pigCacheCodec へ owner=cgal id 付きで登録する。これで reader が descriptor に接続され、
 *   owner 認識の reader_for_tag (Phase 4④) が効く。
 *   cgal は 2 種の codec を持つ:
 *     - "MESH"/"PLY2" = cgal ネイティブ (writer あり)
 *     - "MFM3"/"MFC2" = Manifold キャッシュの **EPECK 昇格読み** (旧 cgCacheCodecUpgrade を統合)。
 *       reader は同じ ptscgWireCacheStreamReaderMesh (create_for_meta がタグから型を選び昇格) で、
 *       書きは cgal ネイティブが担うので match/writer は never/0。
 */
#include	"pig/c++/pigCacheCodec.h"
#include	"pig/c++/pigModule.h"
#include	"pig/c++/pigData.h"
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/ptsApplication.h"   /* ptsObject 派生 TU の作法 (ptsApp 完全型) */
#include	"cg/c++/cgMesh.h"
#include	"cg/c++/ptscgWireCacheStreamReaderMesh.h"
#include	"cg/c++/ptscgWireCacheStreamWriterMesh.h"

static sPtr<tinyState>
cg_mk_reader(sPtr<ptsObject> parent, sPtr<stdString> path)
{
	/* create_for_meta がタグ (MESH/PLY2/MFM3/MFC2) から具体型を選ぶ。MFM3/MFC2 は EPECK へ昇格。 */
	return sPtr<tinyState>::d_cast(thNEW(ptscgWireCacheStreamReaderMesh,(parent, path)));
}

static sPtr<tinyState>
cg_mk_writer(sPtr<ptsObject> parent, sPtr<stdString> path, sPtr<pigData> body)
{
	return sPtr<tinyState>::d_cast(
	    thNEW(ptscgWireCacheStreamWriterMesh,(parent, path, sPtr<cgMesh>::d_cast(body))));
}

static int
cg_match(sPtr<pigData> body)
{
	return sPtr<cgMesh>::d_cast(body).is_notNull();
}

static int
cg_match_never(sPtr<pigData>)
{
	return 0;   /* 昇格読み専用 (書きは cgal ネイティブ MESH/PLY2 が担う) */
}

/* ★ descriptor.codecs が指す配列 (name==0 番兵終端)。cgatsAgent.cpp が extern 参照。 */
extern const pigModuleCodec cgal_codecs[];
const pigModuleCodec cgal_codecs[] = {
	/* out_types はタグと位置対応: MESH→cg-mesh3d / PLY2→cg-cross2d。cg-mf-upgrade は Manifold
	 *   キャッシュ (MFM3/MFC2) を EPECK 昇格して cgal 型 (cg-mesh3d/cg-cross2d) を出す。 */
	{ "cg-mesh",       "MESH,PLY2", "cg-mesh3d,cg-cross2d", &cg_match,       &cg_mk_reader, &cg_mk_writer },
	{ "cg-mf-upgrade", "MFM3,MFC2", "cg-mesh3d,cg-cross2d", &cg_match_never, &cg_mk_reader, 0             },  /* 昇格読み */
	{ 0, 0, 0, 0, 0, 0 },
};
