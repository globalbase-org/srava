/*
 * vgCacheCodec — openvdb_gg.so のキャッシュコーデック (#3434・2026-08-22)。
 *
 * ★ このモジュールは **自分の型を持たない**が、process 実行では agent プロセスに
 *   openvdb_gg.so しか load されないので、**入力 (MFM3) を読み、出力 (VDB ) を書く**
 *   codec を自分で申告する必要がある。
 * ★★ 実体は **libsrava_gg / libsrava_vd の公開クラスをそのまま使う** —
 *   新しい wire 形式もクラスも作らない (それが今回の是正の眼目)。
 */
#include	"pig/c++/pigCacheCodec.h"
#include	"pig/c++/pigModule.h"
#include	"pig/c++/pigData.h"
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/ptsApplication.h"
#include	"gg/c++/ggMesh.h"
#include	"gg/c++/ptsggWireCacheStreamReaderMesh.h"
#include	"gg/c++/ptsggWireCacheStreamWriterMesh.h"
#include	"vd/c++/vdGrid.h"
#include	"vd/c++/ptsvdWireCacheStreamReaderGrid.h"
#include	"vd/c++/ptsvdWireCacheStreamWriterGrid.h"

static sPtr<tinyState> vg_mk_gg_reader(sPtr<ptsObject> p, sPtr<stdString> path)
{ return sPtr<tinyState>::d_cast(thNEW(ptsggWireCacheStreamReaderMesh,(p, path))); }
static sPtr<tinyState> vg_mk_gg_writer(sPtr<ptsObject> p, sPtr<stdString> path, sPtr<pigData> b)
{ return sPtr<tinyState>::d_cast(thNEW(ptsggWireCacheStreamWriterMesh,(p, path, sPtr<ggGeom>::d_cast(b)))); }
static int vg_match_gg(sPtr<pigData> b) { return sPtr<ggMesh>::d_cast(b).is_notNull(); }

static sPtr<tinyState> vg_mk_vd_reader(sPtr<ptsObject> p, sPtr<stdString> path)
{ return sPtr<tinyState>::d_cast(thNEW(ptsvdWireCacheStreamReaderGrid,(p, path))); }
static sPtr<tinyState> vg_mk_vd_writer(sPtr<ptsObject> p, sPtr<stdString> path, sPtr<pigData> b)
{ return sPtr<tinyState>::d_cast(thNEW(ptsvdWireCacheStreamWriterGrid,(p, path, sPtr<vdGeom>::d_cast(b)))); }
static int vg_match_vd(sPtr<pigData> b) { return sPtr<vdGrid>::d_cast(b).is_notNull(); }

/* ★ 2026-08-28 (ひさ設計・ABI v16): このモジュールが提供するもの。
 *   1 行 = (本体クラス階層, その階層について名乗る型名, 扱う 4CC)。
 *   ⚠ **types と tags は位置対応しない** (独立した 2 本・個数も一致しない)。どのタグがどの型に
 *     なるかは申告せず、wire->create に通して訊く (pigModule.h の pigModuleType 参照)。
 *   ⚠ tags は **診断専用** — 読めるかを答えるのは wire->create 一本で、この欄は
 *     `srava --module-info` が列挙するための候補にすぎない (実行時の判断に使わない)。 */
extern const pigModuleType openvdb_gg_provides[];
const pigModuleType openvdb_gg_provides[] = {
	{ &ggGeom::WIRE, "gg-mesh3d",
	  "MFM3" },
	{ &vdGeom::WIRE, VD_TYPE,
	  VD_TAG },
	{ 0, 0, 0 },
};

