/*
 * d3CacheCodec — 第3(mesh)カーネル "d3" のキャッシュコーデック定義 TU (rev4 Phase D-3)。
 * descriptor.codecs 配列 (d3_codecs) を extern 公開し、ローダが owner=d3 id で登録する。
 * mfCacheCodec.cpp のミラー。
 */
#include	"pig/c++/pigCacheCodec.h"
#include	"pig/c++/pigModule.h"
#include	"pig/c++/pigData.h"
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/ptsApplication.h"
#include	"d3/c++/d3Mesh.h"
#include	"d3/c++/ptsd3WireCacheStreamReaderMesh.h"
#include	"d3/c++/ptsd3WireCacheStreamWriterMesh.h"

static sPtr<tinyState>
d3_mk_reader(sPtr<ptsObject> parent, sPtr<stdString> path)
{
	return sPtr<tinyState>::d_cast(thNEW(ptsd3WireCacheStreamReaderMesh,(parent, path)));
}

static sPtr<tinyState>
d3_mk_writer(sPtr<ptsObject> parent, sPtr<stdString> path, sPtr<pigData> body)
{
	return sPtr<tinyState>::d_cast(
	    thNEW(ptsd3WireCacheStreamWriterMesh,(parent, path, sPtr<d3Mesh>::d_cast(body))));
}

static int
d3_match(sPtr<pigData> body)
{
	return sPtr<d3Mesh>::d_cast(body).is_notNull();
}

/* ★ descriptor.codecs が指す配列 (name==0 番兵終端)。d3atsAgent.cpp が extern 参照。 */
/* ★ 2026-08-28 (ABI v12): この階層への配線先。reader は下の codec 行が使うものと同一 —
 *   どの行 (自型読み / foreign 昇格読み) でも reader は 1 本で、階層に帰属するため。 */
PIG_WIRE_DEF(d3Mesh, d3_mk_reader, d3_mk_writer);

/* ★ 2026-08-28 (ひさ設計・ABI v16): このモジュールが提供するもの。
 *   1 行 = (本体クラス階層, その階層について名乗る型名, 扱う 4CC)。
 *   ⚠ **types と tags は位置対応しない** (独立した 2 本・個数も一致しない)。どのタグがどの型に
 *     なるかは申告せず、wire->create に通して訊く (pigModule.h の pigModuleType 参照)。
 *   ⚠ tags は **診断専用** — 読めるかを答えるのは wire->create 一本で、この欄は
 *     `srava --module-info` が列挙するための候補にすぎない (実行時の判断に使わない)。 */
extern const pigModuleType d3_provides[];
const pigModuleType d3_provides[] = {
	{ &d3Mesh::WIRE, "d3-mesh3d",
	  "D3M3" },
	{ 0, 0, 0 },
};

