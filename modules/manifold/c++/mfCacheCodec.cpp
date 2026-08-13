/*
 * mfCacheCodec — Manifold カーネルのキャッシュコーデック定義 TU (#3406 / .so 化 Phase 4③')。
 * descriptor.codecs 配列 (mf_codecs) を extern 公開し、ローダが owner=manifold id で登録する。
 * cgCacheCodec.cpp のミラー。
 */
#include	"pig/c++/pigCacheCodec.h"
#include	"pig/c++/pigModule.h"
#include	"pig/c++/pigData.h"
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/ptsApplication.h"   /* ptsObject 派生 TU の作法 (ptsApp 完全型) */
#include	"mf/c++/mfMesh.h"
#include	"mf/c++/ptsmfWireCacheStreamReaderMesh.h"
#include	"mf/c++/ptsmfWireCacheStreamWriterMesh.h"

static sPtr<tinyState>
mf_mk_reader(sPtr<ptsObject> parent, sPtr<stdString> path)
{
	return sPtr<tinyState>::d_cast(thNEW(ptsmfWireCacheStreamReaderMesh,(parent, path)));
}

static sPtr<tinyState>
mf_mk_writer(sPtr<ptsObject> parent, sPtr<stdString> path, sPtr<pigData> body)
{
	return sPtr<tinyState>::d_cast(
	    thNEW(ptsmfWireCacheStreamWriterMesh,(parent, path, sPtr<mfGeom>::d_cast(body))));
}

static int
mf_match(sPtr<pigData> body)
{
	return sPtr<mfGeom>::d_cast(body).is_notNull();
}

/* 読取専用 codec 用の match: 書きは相手モジュールに任せ、この codec は writer を出さない。 */
static int
mf_match_never(sPtr<pigData>)
{
	return 0;
}

/* ★ descriptor.codecs が指す配列 (name==0 番兵終端)。mfatsAgent.cpp が extern 参照。 */
extern const pigModuleCodec mf_codecs[];
const pigModuleCodec mf_codecs[] = {
	{ "mf-mesh",        "MFM3,MFC2", "mf-mesh3d,mf-cross2d", &mf_match,       &mf_mk_reader, &mf_mk_writer },
	/* cg→mf downgrade 読み: CGAL の exact "MESH"(3D)/"PLY2"(2D) を double 化して mf 型へ
	 * (cast("mf-mesh3d", cgMesh) / cast("mf-cross2d", cgCross))。create_for_meta が MESH/PLY2 を検出し
	 * decode_mesh_exact / decode_cross_exact で有理数→double 化。読取専用 (writer=0)。 */
	{ "mf-cg-downgrade", "MESH,PLY2", "mf-mesh3d,mf-cross2d", &mf_match_never, &mf_mk_reader, 0             },
	{ 0, 0, 0, 0, 0, 0 },
};
