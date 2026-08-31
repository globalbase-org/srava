/*
 * ocCacheCodec — OCCT モジュールのキャッシュコーデック定義 TU (#3437 P5)。
 * vdCacheCodec.cpp のミラー。
 */
#include	"pig/c++/pigCacheCodec.h"
#include	"pig/c++/pigModule.h"
#include	"pig/c++/pigData.h"
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/ptsApplication.h"
#include	"oc/c++/ocShape.h"
#include	"oc/c++/ptsocWireCacheStreamReaderShape.h"
#include	"oc/c++/ptsocWireCacheStreamWriterShape.h"

static sPtr<tinyState>
oc_mk_reader(sPtr<ptsObject> parent, sPtr<stdString> path)
{
	return sPtr<tinyState>::d_cast(thNEW(ptsocWireCacheStreamReaderShape,(parent, path)));
}

static sPtr<tinyState>
oc_mk_writer(sPtr<ptsObject> parent, sPtr<stdString> path, sPtr<pigData> body)
{
	return sPtr<tinyState>::d_cast(
	    thNEW(ptsocWireCacheStreamWriterShape,(parent, path, sPtr<ocGeom>::d_cast(body))));
}

/* ★ 書き手の選択は **ocGeom 全体**で受ける。形式の決定は本文の meta_tag() が行う。
 * ⚠ akira-project #3452 で ocMesh を撤去したので、現在 occt が書くのは ocShape (BREP) だけ。
 *   ocGeom で受ける形は据え置く (将来 B-rep 系の派生が増えてもここを触らずに済む)。 */
static int oc_match_geom(sPtr<pigData> body) { return sPtr<ocGeom>::d_cast(body).is_notNull(); }

/* ★ 2026-08-28 (ABI v12): この階層への配線先。reader は下の codec 行が使うものと同一 —
 *   どの行 (自型読み / foreign 昇格読み) でも reader は 1 本で、階層に帰属するため。 */
PIG_WIRE_DEF(ocGeom, oc_mk_reader, oc_mk_writer);

/* ★ 2026-08-28 (ひさ設計・ABI v16): このモジュールが提供するもの。
 *   1 行 = (本体クラス階層, その階層について名乗る型名, 扱う 4CC)。
 *   ⚠ **types と tags は位置対応しない** (独立した 2 本・個数も一致しない)。どのタグがどの型に
 *     なるかは申告せず、wire->create に通して訊く (pigModule.h の pigModuleType 参照)。
 *   ⚠ tags は **診断専用** — 読めるかを答えるのは wire->create 一本で、この欄は
 *     `srava --module-info` が列挙するための候補にすぎない (実行時の判断に使わない)。 */
extern const pigModuleType occt_provides[];
const pigModuleType occt_provides[] = {
	{ &ocGeom::WIRE, OC_TYPE,
	  OC_TAG },
	{ 0, 0, 0 },
};

