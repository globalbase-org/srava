/*
 * d4CacheCodec — 第4(in-proc mesh 消費)モジュール "d4" のキャッシュコーデック定義 TU
 *   (⑤ cross-module 変換・P4)。descriptor.codecs 配列 (d4_codecs) を extern 公開し、ローダが
 *   owner=d4 id で登録する。cgCacheCodec.cpp (cg-mf-upgrade) のミラー:
 *     - "d4-mesh" = d4 ネイティブ (D4M3・writer あり)
 *     - "d4-mf-upgrade" = Manifold キャッシュ (MFM3) を **自型 (d4-mesh3d) として読む** cross reader
 *       (writer は無し=読み専用)。mfMesh の MFM3 framing は d4Mesh と同一なので同じ reader で読める。
 */
#include	"pig/c++/pigCacheCodec.h"
#include	"pig/c++/pigModule.h"
#include	"pig/c++/pigData.h"
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/ptsApplication.h"
#include	"d4/c++/d4Mesh.h"
#include	"d4/c++/ptsd4WireCacheStreamReaderMesh.h"
#include	"d4/c++/ptsd4WireCacheStreamWriterMesh.h"

static sPtr<tinyState>
d4_mk_reader(sPtr<ptsObject> parent, sPtr<stdString> path)
{
	return sPtr<tinyState>::d_cast(thNEW(ptsd4WireCacheStreamReaderMesh,(parent, path)));
}

static sPtr<tinyState>
d4_mk_writer(sPtr<ptsObject> parent, sPtr<stdString> path, sPtr<pigData> body)
{
	return sPtr<tinyState>::d_cast(
	    thNEW(ptsd4WireCacheStreamWriterMesh,(parent, path, sPtr<d4Mesh>::d_cast(body))));
}

static int
d4_match(sPtr<pigData> body)
{
	return sPtr<d4Mesh>::d_cast(body).is_notNull();
}

static int
d4_match_never(sPtr<pigData>)
{
	return 0;   /* 昇格読み専用 (書きは d4 ネイティブ D4M3 が担う・cgCacheCodec の cg_match_never と同じ) */
}

/* ★ descriptor.codecs が指す配列 (name==0 番兵終端)。d4atsAgent.cpp が extern 参照。
 *   d4-mf-upgrade は MFM3 (Manifold mesh) を d4-mesh3d として読む cross reader = ⑤ 変換の実体。
 *   reader_for(MFM3, "d4-mesh3d") がこれを選ぶ (out_type でフィルタ・mf 自身の MFM3 読みは非干渉)。 */
extern const pigModuleCodec d4_codecs[];
const pigModuleCodec d4_codecs[] = {
	{ "d4-mesh",       "D4M3", "d4-mesh3d", &d4_match,       &d4_mk_reader, &d4_mk_writer },
	{ "d4-mf-upgrade", "MFM3", "d4-mesh3d", &d4_match_never, &d4_mk_reader, 0             },  /* MFM3→d4 昇格読み */
	{ 0, 0, 0, 0, 0, 0 },
};
