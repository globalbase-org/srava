/*
 * d5CacheCodec — 第5(in-proc mesh 消費)モジュール "d5" のキャッシュコーデック定義 TU
 *   (⑤ cross-module 変換・P4)。descriptor.codecs 配列 (d5_codecs) を extern 公開し、ローダが
 *   owner=d5 id で登録する。cgCacheCodec.cpp (cg-mf-upgrade) のミラー:
 *     - "d5-mesh" = d5 ネイティブ (D5M3・writer あり)
 *     - "d5-mf-upgrade" = Manifold キャッシュ (MFM3) を **自型 (d5-mesh3d) として読む** cross reader
 *       (writer は無し=読み専用)。mfMesh の MFM3 framing は d5Mesh と同一なので同じ reader で読める。
 */
#include	"pig/c++/pigCacheCodec.h"
#include	"pig/c++/pigModule.h"
#include	"pig/c++/pigData.h"
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/ptsApplication.h"
#include	"d5/c++/d5Mesh.h"
#include	"d5/c++/ptsd5WireCacheStreamReaderMesh.h"
#include	"d5/c++/ptsd5WireCacheStreamWriterMesh.h"

static sPtr<tinyState>
d5_mk_reader(sPtr<ptsObject> parent, sPtr<stdString> path)
{
	return sPtr<tinyState>::d_cast(thNEW(ptsd5WireCacheStreamReaderMesh,(parent, path)));
}

static sPtr<tinyState>
d5_mk_writer(sPtr<ptsObject> parent, sPtr<stdString> path, sPtr<pigData> body)
{
	return sPtr<tinyState>::d_cast(
	    thNEW(ptsd5WireCacheStreamWriterMesh,(parent, path, sPtr<d5Mesh>::d_cast(body))));
}

static int
d5_match(sPtr<pigData> body)
{
	return sPtr<d5Mesh>::d_cast(body).is_notNull();
}

static int
d5_match_never(sPtr<pigData>)
{
	return 0;   /* 昇格読み専用 (書きは d5 ネイティブ D5M3 が担う・cgCacheCodec の cg_match_never と同じ) */
}

/* ★ descriptor.codecs が指す配列 (name==0 番兵終端)。d5atsAgent.cpp が extern 参照。
 *   d5-mf-upgrade は MFM3 (Manifold mesh) を d5-mesh3d として読む cross reader = ⑤ 変換の実体。
 *   reader_for(MFM3, "d5-mesh3d") がこれを選ぶ (out_type でフィルタ・mf 自身の MFM3 読みは非干渉)。 */
extern const pigModuleCodec d5_codecs[];
const pigModuleCodec d5_codecs[] = {
	{ "d5-mesh",       "D5M3", "d5-mesh3d", &d5_match,       &d5_mk_reader, &d5_mk_writer },
	{ "d5-mf-upgrade", "MFM3", "d5-mesh3d", &d5_match_never, &d5_mk_reader, 0             },  /* MFM3→d5 昇格読み */
	{ 0, 0, 0, 0, 0, 0 },
};
