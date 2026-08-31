/*
 * ocmCacheCodec — occt_mf.so のキャッシュコーデック (akira-project #3452)。
 *   vmCacheCodec.cpp (openvdb_mf) のミラー。
 *
 * ★ このモジュールは **自分の型を持たない**が、process 実行では agent プロセスに
 *   occt_mf.so しか load されないので、**入力 (BREP) を読み、出力 (MFM3) を書く**
 *   codec を自分で申告する必要がある。
 * ★★ 実体は **libsrava_oc / libsrava_mf の公開クラスをそのまま使う** —
 *   新しい wire 形式もクラスも作らない (それが今回の是正の眼目)。
 *   かつて occt.so が「mf-mesh3d を名乗る内部クラス ocMesh」を作っていたのが元の誤り。
 */
#include	"pig/c++/pigCacheCodec.h"
#include	"pig/c++/pigModule.h"
#include	"pig/c++/pigData.h"
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/ptsApplication.h"
#include	"oc/c++/ocShape.h"
#include	"oc/c++/ptsocWireCacheStreamReaderShape.h"
#include	"oc/c++/ptsocWireCacheStreamWriterShape.h"
#include	"mf/c++/mfMesh.h"
#include	"mf/c++/ptsmfWireCacheStreamReaderMesh.h"
#include	"mf/c++/ptsmfWireCacheStreamWriterMesh.h"

static sPtr<tinyState> ocm_mk_oc_reader(sPtr<ptsObject> p, sPtr<stdString> path)
{ return sPtr<tinyState>::d_cast(thNEW(ptsocWireCacheStreamReaderShape,(p, path))); }
static sPtr<tinyState> ocm_mk_oc_writer(sPtr<ptsObject> p, sPtr<stdString> path, sPtr<pigData> b)
{ return sPtr<tinyState>::d_cast(thNEW(ptsocWireCacheStreamWriterShape,(p, path, sPtr<ocGeom>::d_cast(b)))); }
static int ocm_match_oc(sPtr<pigData> b) { return sPtr<ocShape>::d_cast(b).is_notNull(); }

static sPtr<tinyState> ocm_mk_mf_reader(sPtr<ptsObject> p, sPtr<stdString> path)
{ return sPtr<tinyState>::d_cast(thNEW(ptsmfWireCacheStreamReaderMesh,(p, path))); }
static sPtr<tinyState> ocm_mk_mf_writer(sPtr<ptsObject> p, sPtr<stdString> path, sPtr<pigData> b)
{ return sPtr<tinyState>::d_cast(thNEW(ptsmfWireCacheStreamWriterMesh,(p, path, sPtr<mfGeom>::d_cast(b)))); }
static int ocm_match_mf(sPtr<pigData> b) { return sPtr<mfMesh>::d_cast(b).is_notNull(); }

/* ★ 2026-08-28 (ひさ設計・ABI v16): このモジュールが提供するもの。
 *   1 行 = (本体クラス階層, その階層について名乗る型名, 扱う 4CC)。
 *   ⚠ **types と tags は位置対応しない** (独立した 2 本・個数も一致しない)。どのタグがどの型に
 *     なるかは申告せず、wire->create に通して訊く (pigModule.h の pigModuleType 参照)。
 *   ⚠ tags は **診断専用** — 読めるかを答えるのは wire->create 一本で、この欄は
 *     `srava --module-info` が列挙するための候補にすぎない (実行時の判断に使わない)。 */
extern const pigModuleType occt_mf_provides[];
const pigModuleType occt_mf_provides[] = {
	{ &ocGeom::WIRE, OC_TYPE,
	  OC_TAG },
	{ &mfGeom::WIRE, "mf-mesh3d",
	  "MFM3" },
	{ 0, 0, 0 },
};

