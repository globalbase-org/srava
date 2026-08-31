/*
 * vdCacheCodec — OpenVDB ボリュームモジュールのキャッシュコーデック定義 TU (#3434 P2)。
 * descriptor.codecs (vd_codecs) を extern 公開し、ローダが owner=openvdb id で登録する。
 * ggCacheCodec.cpp / mfCacheCodec.cpp のミラー。
 */
#include	"pig/c++/pigCacheCodec.h"
#include	"pig/c++/pigModule.h"
#include	"pig/c++/pigData.h"
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/ptsApplication.h"
#include	"vd/c++/vdGrid.h"
#include	"vd/c++/ptsvdWireCacheStreamReaderGrid.h"
#include	"vd/c++/ptsvdWireCacheStreamWriterGrid.h"

static sPtr<tinyState>
vd_mk_reader(sPtr<ptsObject> parent, sPtr<stdString> path)
{
	return sPtr<tinyState>::d_cast(thNEW(ptsvdWireCacheStreamReaderGrid,(parent, path)));
}

static sPtr<tinyState>
vd_mk_writer(sPtr<ptsObject> parent, sPtr<stdString> path, sPtr<pigData> body)
{
	return sPtr<tinyState>::d_cast(
	    thNEW(ptsvdWireCacheStreamWriterGrid,(parent, path, sPtr<vdGeom>::d_cast(body))));
}

static int
vd_match_grid(sPtr<pigData> body)
{
	return sPtr<vdGrid>::d_cast(body).is_notNull();
}


/* 読取専用 codec 用の match: 書きは相手モジュールに任せ、この codec は writer を出さない。 */
static int
vd_match_never(sPtr<pigData>)
{
	return 0;
}

/* ★ 2026-08-28 (ABI v12): この階層への配線先。reader は下の codec 行が使うものと同一 —
 *   どの行 (自型読み / foreign 昇格読み) でも reader は 1 本で、階層に帰属するため。 */
PIG_WIRE_DEF(vdGeom, vd_mk_reader, vd_mk_writer);

/* ★ 2026-08-28 (ひさ設計・ABI v16): このモジュールが提供するもの。
 *   1 行 = (本体クラス階層, その階層について名乗る型名, 扱う 4CC)。
 *   ⚠ **types と tags は位置対応しない** (独立した 2 本・個数も一致しない)。どのタグがどの型に
 *     なるかは申告せず、wire->create に通して訊く (pigModule.h の pigModuleType 参照)。
 *   ⚠ tags は **診断専用** — 読めるかを答えるのは wire->create 一本で、この欄は
 *     `srava --module-info` が列挙するための候補にすぎない (実行時の判断に使わない)。 */
extern const pigModuleType openvdb_provides[];
const pigModuleType openvdb_provides[] = {
	{ &vdGeom::WIRE, VD_TYPE,
	  VD_TAG },
	{ 0, 0, 0 },
};

