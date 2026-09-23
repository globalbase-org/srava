/*
 * ptCacheCodec — 点群階層の配線 (WIRE) と points.so の provides (#3528)。
 *
 * ★ WIRE (階層 → reader/writer/create/match) は **libsrava_pt** に置く。点群を消費する
 *   モジュールは vgCacheCodec が ggGeom::WIRE / vdGeom::WIRE を並べるのと同じく、
 *   自分の provides に &ptCloud::WIRE を書くだけでよい (新しいクラスも wire 形式も作らない)。
 */
#include	"pig/c++/pigCacheCodec.h"
#include	"pig/c++/pigModule.h"
#include	"pig/c++/pigData.h"
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/ptsApplication.h"
#include	"pt/c++/ptCloud.h"
#include	"pt/c++/ptsptWireCacheStreamReaderCloud.h"
#include	"pt/c++/ptsptWireCacheStreamWriterCloud.h"

static sPtr<tinyState>
pt_mk_reader(sPtr<ptsObject> parent, sPtr<stdString> path)
{
	/* create_for_meta がタグ (PTC2/PTC3) から次元を決める。 */
	return sPtr<tinyState>::d_cast(thNEW(ptsptWireCacheStreamReaderCloud,(parent, path)));
}

static sPtr<tinyState>
pt_mk_writer(sPtr<ptsObject> parent, sPtr<stdString> path, sPtr<pigData> body)
{
	return sPtr<tinyState>::d_cast(
	    thNEW(ptsptWireCacheStreamWriterCloud,(parent, path, sPtr<ptCloud>::d_cast(body))));
}

/* ★ ABI v12: この階層への配線先。reader/writer/create/match はすべて階層に帰属する。 */
PIG_WIRE_DEF(ptCloud, pt_mk_reader, pt_mk_writer);

/* ★ ABI v16: このモジュールが提供するもの。1 行 = (本体クラス階層, 名乗る型名, 扱う 4CC)。
 *   ⚠ types と tags は位置対応しない (ここでは偶然 2 対 2 だが、対応を申告しているのではない。
 *     どのタグがどの型になるかは wire->create に通して訊く)。
 *   ⚠ tags は診断専用 — `srava --module-info` が列挙するための候補にすぎない。 */
extern const pigModuleType points_provides[];
const pigModuleType points_provides[] = {
	{ &ptCloud::WIRE, PT_TYPE_2D "," PT_TYPE_3D,
	  PT_TAG_2D "," PT_TAG_3D },
	{ 0, 0, 0 },
};
