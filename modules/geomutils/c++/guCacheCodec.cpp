/*
 * guCacheCodec — 中立幾何階層の配線 (WIRE) と geomutils.so の provides (#3527)。
 *
 * ★ WIRE (階層 → reader/writer/create/match) は **libsrava_gu** に置く。この型を消費する
 *   モジュールは自分の provides に &guGeom::WIRE を書くだけでよい (points.so と同じ作法)。
 */
#include	"pig/c++/pigCacheCodec.h"
#include	"pig/c++/pigModule.h"
#include	"pig/c++/pigData.h"
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/ptsApplication.h"
#include	"gu/c++/guGeom.h"
#include	"pt/c++/ptCloud.h"   /* ★ #3527 段 5: 値の器を借りる (#3528) */
#include	"gu/c++/ptsguWireCacheStreamReaderGeom.h"
#include	"gu/c++/ptsguWireCacheStreamWriterGeom.h"

static sPtr<tinyState>
gu_mk_reader(sPtr<ptsObject> parent, sPtr<stdString> path)
{
	/* create_for_meta がタグ (MFM3/MFC2) から 3D か 2D かを決める。 */
	return sPtr<tinyState>::d_cast(thNEW(ptsguWireCacheStreamReaderGeom,(parent, path)));
}

static sPtr<tinyState>
gu_mk_writer(sPtr<ptsObject> parent, sPtr<stdString> path, sPtr<pigData> body)
{
	return sPtr<tinyState>::d_cast(
	    thNEW(ptsguWireCacheStreamWriterGeom,(parent, path, sPtr<guGeom>::d_cast(body))));
}

/* ★ ABI v12: この階層への配線先。 */
PIG_WIRE_DEF(guGeom, gu_mk_reader, gu_mk_writer);

/* ★ ABI v16: このモジュールが提供するもの。1 行 = (本体クラス階層, 名乗る型名, 扱う 4CC)。
 *   ⚠ types と tags は位置対応しない (どのタグがどの型になるかは wire->create に訊く)。
 *   ★★ 4CC は manifold / geogram / cherchi と **共有**している。同じタグを複数モジュールが
 *     名乗るのは既に前例がある (geogram と cherchi が現に MFM3 を共有)。 */
extern const pigModuleType geomutils_provides[];
const pigModuleType geomutils_provides[] = {
	{ &guGeom::WIRE, GU_TYPE_3D "," GU_TYPE_2D "," GU_TYPE_2DP,
	  GU_TAG_3D "," GU_TAG_2D },
	/* ★ #3527 段 5: **自前のクラスを作らず libsrava_pt のクラスをそのまま並べる**
	 *   (cgal / occt_mf が同じ作法)。verts(v) が pt-cloud を書くので、agent プロセスに
	 *   geomutils.so しか load されない process 実行でも codec がここから届く必要がある。
	 *   ⚠ 借りているのは **値の器** だけで幾何の機能ではない (境界の約束①に触れない)。 */
	{ &ptCloud::WIRE, PT_TYPE_2D "," PT_TYPE_3D, PT_TAG_2D "," PT_TAG_3D },
	{ 0, 0, 0 },
};
