/*
 * octsAgent — OCCT (B-rep) モジュールの実行体 (#3437 P5)。
 * 状態機械は共通基底 ptsGenericAgent が持つので、ここは OPS 表と記述子だけ。
 */
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/ptsGenericAgent.h"
#include	"pig/c++/pigOpEntry.h"
#include	"pig/c++/pigModule.h"
#include	"pig/c++/ptsCalcBody.h"
#include	"oc/c++/ocShape.h"
#include	"oc/c++/ocaBox.h"
#include	"oc/c++/ocaSphere.h"
#include	"oc/c++/ocaUnion.h"
#include	"oc/c++/ocaIntersection.h"
#include	"oc/c++/ocaDifference.h"
#include	"oc/c++/ocaOffset.h"
#include	"oc/c++/ocaVolume.h"
#include	"oc/c++/ocaCylinder.h"
#include	"oc/c++/ocaTorus.h"
#include	"oc/c++/ocaFillet.h"
#include	"oc/c++/ocaChamfer.h"
#include	"oc/c++/ocaExport.h"
#include	"oc/c++/ocaImport.h"
#include	"oc/c++/ocaNfaces.h"
#include	"_ts2/c++/octsAgent_.h"

CLASS_TINYSTATE(oc/c++/octsAgent,pig/c++/ptsGenericAgent)

template <class T>
static sPtr<ptsCalcBody>
mkCalcT(sPtr<ptsObject> parent, sArray<sPtr<pigData> > *args, sPtr<stdString> target)
{
	return sPtr<ptsCalcBody>::d_cast(thNEW(T,(parent, args, target)));
}

static const pigArgKind SHAPE3_IN[]  = { AK_INLINE, AK_INLINE, AK_INLINE };  /* box(w,h,d) */
static const pigArgKind SHAPE2_IN[]  = { AK_INLINE, AK_INLINE };             /* sphere(r,seg) */
static const pigArgKind BINSHAPE_IN[]= { AK_CACHE, AK_CACHE };               /* 2 shape 入力 */
static const pigArgKind MEASURE_IN[] = { AK_CACHE };                         /* shape 1 個 */
static const pigArgKind OFFSET_IN[]  = { AK_CACHE, AK_INLINE, AK_INLINE };   /* offset(s,d,unused) */
static const pigArgKind SHAPE1_IN[]  = { AK_INLINE };                        /* import(path) */
static const pigArgKind EDIT_IN[]    = { AK_CACHE, AK_INLINE };              /* fillet(s,r) / chamfer(s,d) */
static const pigArgKind EXPORT_IN[]  = { AK_INLINE, AK_CACHE, AK_INLINE };   /* export(path, s, unit) */

static const pigOpEntry OPS[] = {
	/* 生成: ★どれも**解析曲面**で作る。box は 6 枚の平面 Face、sphere は**厳密な球面 1 枚**。 */
	{ "box",          SHAPE3_IN, 3, AK_CACHE,  OPWIRE(ocaBox),          0, "->" OC_TYPE },
	/* ★ sphere の第 2 引数 (分割数) は無視する — 近似しないので意味を持たない。
	 *   このため volume が 4/3·π·r³ ちょうどになり、内接多面体を作る他カーネルとは
	 *   **一致しない**。kernel_agree に素で入れてはいけない (それ自体が結果)。 */
	{ "sphere",       SHAPE2_IN, 2, AK_CACHE,  OPWIRE(ocaSphere),       0, "->" OC_TYPE },
	/* ★ cylinder / torus も**厳密**。cylinder は側面が円筒面 1 枚 (Face 3 枚)、torus は
	 *   トーラス面 1 枚 (Face 1 枚) でできる。どちらも分割数という概念を持たず、
	 *   volume は π r² h / 2π²R r² とちょうど一致する。
	 *   ★ torus は「メッシュ系では必ず近似になるが B-rep では厳密に持てる」形の代表で、
	 *     しかも fillet が稜に作る曲面そのものでもある。 */
	{ "cylinder",     SHAPE2_IN, 2, AK_CACHE,  OPWIRE(ocaCylinder),     0, "->" OC_TYPE },
	{ "torus",        SHAPE2_IN, 2, AK_CACHE,  OPWIRE(ocaTorus),        0, "->" OC_TYPE },
	/* ★ 入口: STEP / .brep を読む。**mesh → B-rep ではない** (どちらも解析曲面をそのまま
	 *   持つ形式なので、読むだけで B-rep が手に入る)。三角形から解析曲面を復元する
	 *   reverse engineering の入口は依然として作らない。 */
	{ "import",       SHAPE1_IN, 1, AK_CACHE,  OPWIRE(ocaImport),       0, "->" OC_TYPE },
	/* ブール: 自型どうしだけ。★OCCT は失敗しうるので、失敗は明示エラーにする。 */
	{ "union",        BINSHAPE_IN,2,AK_CACHE,  OPWIRE(ocaUnion, ocGeom, ocGeom),        1, "[" OC_TYPE "](*)->" OC_TYPE, 1 /* ★可換 */ },
	{ "intersection", BINSHAPE_IN,2,AK_CACHE,  OPWIRE(ocaIntersection, ocGeom, ocGeom), 1, "[" OC_TYPE "](*)->" OC_TYPE, 1 /* ★可換 */ },
	{ "difference",   BINSHAPE_IN,2,AK_CACHE,  OPWIRE(ocaDifference, ocGeom, ocGeom),   1, "[" OC_TYPE "](*)->" OC_TYPE },
	/* ★ **第 3 の offset 原理**: 解析曲面を直接オフセットし、稜に円筒パッチ・頂点に球パッチを
	 *   生成する (= Steiner の公式を構成的に実行)。nef の近似球 Minkowski 和とも
	 *   openvdb の格子等値面移動とも違う。入力型が disjoint なので他モジュールと衝突しない。 */
	{ "offset",       OFFSET_IN, 3, AK_CACHE,  OPWIRE(ocaOffset, ocGeom),       0, "(" OC_TYPE ")->" OC_TYPE },
	/* ★ triangulate (表現クラスをまたぐ唯一の出口) は **occt_mf.so へ移した**
	 *   (akira-project #3452)。ここに置くと occt.so が mf-mesh3d を名乗ることになり、
	 *   その実体は mfMesh ではない別クラス (旧 ocMesh) にならざるを得なかった。
	 *   境界 op は両側の本物のクラスを知っている境界モジュールが持つ (openvdb_mf と同じ)。
	 *   ★ 入口 (mesh → B-rep) は引き続き作らない。 */
	/* ★★ **B-rep でしか厳密に書けない加工**。転がり球の接触軌跡は解析曲面 (円筒・球・
	 *   トーラス) であって、三角形分割の上では**定義そのものが近似になる**。
	 *   メッシュ系にこの op が無いのは偶然ではない。★ 全ての稜に一律で適用する
	 *   (「この稜だけ」を指す語彙が srava に無いため。部分適用は将来)。 */
	{ "fillet",       EDIT_IN,   2, AK_CACHE,  OPWIRE(ocaFillet, ocGeom),       0, "(" OC_TYPE ")->" OC_TYPE },
	{ "chamfer",      EDIT_IN,   2, AK_CACHE,  OPWIRE(ocaChamfer, ocGeom),      0, "(" OC_TYPE ")->" OC_TYPE },
	/* ★ 出口 (B-rep のまま): STEP / .brep。三角形へ落とさずに外の CAD へ渡せる。
	 *   ⚠ 従来は export_exts に "brep" と申告しながら **export op を持っていなかった**
	 *     (= 記述子が嘘をついていた・#3437 で是正)。 */
	{ "export",       EXPORT_IN, 3, AK_CACHE,  OPWIRE(ocaExport, ocGeom),       0, "(" OC_TYPE ")->ref" },
	{ "volume",       MEASURE_IN,1, AK_INLINE, OPWIRE(ocaVolume, ocGeom),       0, "(" OC_TYPE ")->value" },
	/* ★ **三角形数ではなく Face 数**。円筒の側面は 1 面・トーラスは全体で 1 面なので
	 *   mesh 系の nfaces とは桁が違う値になる。その違いがこの表現の要点。 */
	{ "nfaces",       MEASURE_IN,1, AK_INLINE, OPWIRE(ocaNfaces, ocGeom),       0, "(" OC_TYPE ")->value" },
};
static const int N_OPS = (int)(sizeof(OPS) / sizeof(OPS[0]));

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	octsAgent_(
		sPtr<ptsObject> parent);

protected:
	virtual const pigOpEntry*	agent_ops();
	virtual int			agent_n_ops();
	virtual const char*		agent_name();
private:
	TS_DEFARGS
};

TS_END_IMPLEMENT

TS_BEGIN_INTERFACE
#include	"ts2/c++/sRptr.h"
class ptsObject;
struct pigOpEntry;
TS_END_INTERFACE

#endif


octsAgent_::octsAgent_(TS_ARGS0)
        : ptsGenericAgent_(parent)
{
    TS_CPARGS0
}

const pigOpEntry*	octsAgent_::agent_ops()	  { return OPS; }
int			octsAgent_::agent_n_ops() { return N_OPS; }
const char*		octsAgent_::agent_name()  { return OC_MODULE_NAME; }

static sPtr<ptsAgent>
mk_octsAgent(sPtr<ptsObject> parent)
{
	return sPtr<ptsAgent>::d_cast(thNEW(octsAgent,(parent)));
}

/* 自己申告記述子。
 *  - priority=2: 低いので**既定カーネルにはならない** (既定は cgal の 20)。ベンチで使うときは
 *    module("occt.so",{priority:99}) で明示的に上げる。
 *    ★ 現在の梯子 (2026-08-25): cgal 20 > manifold 10 > geogram 6 > nef 5 > pipe_proximity 4 >
 *      occt 2 > openvdb 1 > (テスト専用は負値: demo -1 / d3 -2 / d2 -3 / d4 -4 / d5 -5)。
 *      ★同点の勝敗は不定なので、同梱モジュールは互いに重複させない。
 *  - exec: PROCESS のみ。OCCT はプロセス全体のグローバル状態を持ち、in-proc の安全性は未検証。
 *    geogram / openvdb と同じ慎重な既定。in-proc 化は #3419 とまとめて扱う。
 */
extern const pigModuleType occt_provides[];
extern const srava_module_descriptor octsAgent_descriptor;
extern const srava_module_descriptor octsAgent_descriptor = {
	.abi_version   = SRAVA_MODULE_ABI,
	.name          = OC_MODULE_NAME,
	.priority      = 2,
	.make_agent    = &mk_octsAgent,
	.exec_caps     = (unsigned)EXEC_PROCESS,
	.exec_default  = EXEC_PROCESS,
	.ops           = OPS,
	.n_ops         = N_OPS,
	/* ★ import_exts は **型つき** CSV。STEP/BREP は解析曲面を持つ形式なので B-rep が直接出る。
	 *   ⚠ ここに mesh 系の拡張子 (stl/off) を足してはいけない — それは「入口を作らない」と
	 *     決めた reverse engineering そのものになる。 */
	.import_exts   = "step:" OC_TYPE ",stp:" OC_TYPE ",brep:" OC_TYPE,   /* */
	.export_exts   = "step,stp,brep",   /* (STEPControl_Writer / BRepTools::Write) */
	.provides      = occt_provides,   /* 階層 × 型名 × 4CC (ABI v16) */
	/* ★ #3452: **oc-brep3d だけ**を名乗る。mf-mesh3d は occt_mf.so が (本物の mfMesh で) 扱う。 */
	.hash_salt     = OC_SALT,
	.initialize    = 0,   /* 無し */
	.configure     = 0,   /* ★ v10 (#3441): opts フックは未使用(このモジュールは module() の
	                       *   opts を消費しない) */
};
