/*
 * d2CacheCodec — 第2(2D)カーネル "d2" のキャッシュコーデック定義 TU (rev4 次元分担デモ)。
 * d3CacheCodec.cpp のミラー。descriptor.codecs 配列 (d2_codecs) を extern 公開。
 */
#include	"pig/c++/pigCacheCodec.h"
#include	"pig/c++/pigModule.h"
#include	"pig/c++/pigData.h"
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/ptsApplication.h"
#include	"d2/c++/d2Shape.h"
#include	"d2/c++/ptsd2WireCacheStreamReaderShape.h"
#include	"d2/c++/ptsd2WireCacheStreamWriterShape.h"

static sPtr<tinyState>
d2_mk_reader(sPtr<ptsObject> parent, sPtr<stdString> path)
{
	return sPtr<tinyState>::d_cast(thNEW(ptsd2WireCacheStreamReaderShape,(parent, path)));
}

static sPtr<tinyState>
d2_mk_writer(sPtr<ptsObject> parent, sPtr<stdString> path, sPtr<pigData> body)
{
	return sPtr<tinyState>::d_cast(
	    thNEW(ptsd2WireCacheStreamWriterShape,(parent, path, sPtr<d2Shape>::d_cast(body))));
}

static int
d2_match(sPtr<pigData> body)
{
	return sPtr<d2Shape>::d_cast(body).is_notNull();
}

/* ★ 2026-08-28 (ABI v12): この階層への配線先。reader は下の codec 行が使うものと同一 —
 *   どの行 (自型読み / foreign 昇格読み) でも reader は 1 本で、階層に帰属するため。 */
PIG_WIRE_DEF(d2Shape, d2_mk_reader, d2_mk_writer);

/* ★ 2026-08-28 (ひさ設計・ABI v16): このモジュールが提供するもの。
 *   1 行 = (本体クラス階層, その階層について名乗る型名, 扱う 4CC)。
 *   ⚠ **types と tags は位置対応しない** (独立した 2 本・個数も一致しない)。どのタグがどの型に
 *     なるかは申告せず、wire->create に通して訊く (pigModule.h の pigModuleType 参照)。
 *   ⚠ tags は **診断専用** — 読めるかを答えるのは wire->create 一本で、この欄は
 *     `srava --module-info` が列挙するための候補にすぎない (実行時の判断に使わない)。 */
extern const pigModuleType d2_provides[];
const pigModuleType d2_provides[] = {
	{ &d2Shape::WIRE, "d2-shape2d",
	  "D2S2" },
	{ 0, 0, 0 },
};

