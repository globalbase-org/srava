/*
 * mfatsAgent — Manifold モジュールの実行体 (ptsGenericAgent 派生)。
 *   ★ 状態機械 (WAIT/STARTCALC/CALC/ERROR/FIN) は共通基底 ptsGenericAgent に集約済み。この派生は
 *   **OPS[] 表と記述子だけ**を持ち、agent_ops()/agent_n_ops()/agent_name() を override して基底に渡す。
 *
 * 配線: 通信は自分では持たず親 (ptsObject) に委ねる。agent process では parent=ptsAgentApplication、
 *   planner 内 thread では parent=ptsMediatorInternal。結果もエラーも pigData のまま set_result() して
 *   FIN へ抜けるだけ (ワイヤ符号化は親の役割)。cgatsAgent 版と同一構造 (ともに ptsGenericAgent 派生)。
 */
#include	"pig/c++/ptsObject.h"
#include	"mf/c++/mfMesh.h"
#include	"pig/c++/ptsApplication.h"    /* ptsApp 値メンバの完全型 */
#include	"pig/c++/ptsAgent.h"
#include	"pig/c++/ptsGenericAgent.h"   /* 共通基底 (状態機械) */
#include	"pig/c++/pigAgentRegistry.h"
#include	"pig/c++/pigModuleRegistry.h"
#include	"pig/c++/pigModule.h"
#include	"pig/c++/pigData.h"
#include	"pig/c++/pigOpEntry.h"
#include	"pig/c++/ptsCalcBody.h"
#include	"mf/c++/mfaBox.h"
#include	"mf/c++/mfaSphere.h"
#include	"mf/c++/mfaIcosphere.h"
#include	"mf/c++/mfaUnion.h"
#include	"mf/c++/mfaIntersection.h"
#include	"mf/c++/mfaDifference.h"
#include	"mf/c++/mfaExport.h"
#include	"mf/c++/mfaCast.h"
#include	"mf/c++/mfaPolygon.h"
#include	"mf/c++/mfaPrism.h"
#include	"mf/c++/mfaRevolve.h"
#include	"mf/c++/mfaVolume.h"
#include	"mf/c++/mfaBbox.h"
#include	"mf/c++/mfaTranslate.h"
#include	"mf/c++/mfaRotate.h"
#include	"mf/c++/mfaScale.h"
#include	"mf/c++/mfaMirror.h"
#include	"mf/c++/mfaTransform.h"
#include	"mf/c++/mfaArea.h"
#include	"mf/c++/mfaNverts.h"
#include	"mf/c++/mfaNfaces.h"
#include	"mf/c++/mfaValid.h"
#include	"mf/c++/mfaCentroid.h"
#include	"mf/c++/mfaImport.h"
#include	"mf/c++/mfaRect.h"
#include	"mf/c++/mfaCircle.h"
#include	"mf/c++/mfaNgon.h"
#include	"mf/c++/mfaExtrude.h"
#include	"mf/c++/mfaCombine.h"
#include	"mf/c++/mfaSection.h"
#include	"mf/c++/mfaEmpty2D.h"
#include	"mf/c++/mfaEmpty3D.h"
#include	"mf/c++/mfaOffset.h"
#include	"mf/c++/mfaTube.h"
#include	"mf/c++/mfaColor.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/mfatsAgent_.h"

#include	<string.h>

CLASS_TINYSTATE(mf/c++/mfatsAgent,pig/c++/ptsGenericAgent)

/* ---- ディスパッチテーブル (ファイルスコープ・pig 層の共通型) ---- */

static const pigArgKind SHAPE3_IN[] = { AK_INLINE, AK_INLINE, AK_INLINE };  /* box/prism/pyramid */
static const pigArgKind SHAPE2_IN[] = { AK_INLINE, AK_INLINE };             /* rect(w,h) 2D */
static const pigArgKind SHAPE1_IN[] = { AK_INLINE };                        /* sphere(r) / boxa([..]) */
static const pigArgKind EXPORT_IN[] = { AK_INLINE, AK_CACHE, AK_INLINE };  /* export(path, mesh, unit) */
static const pigArgKind BINMESH_IN[] = { AK_CACHE, AK_CACHE };  /* 2 mesh 入力(cache ハンドル→reader 読み) */
static const pigArgKind CAST_IN[] = { AK_INLINE, AK_CACHE };  /* cast(type_string, mesh): type=inline, mesh=cache(reader) */
static const pigArgKind MEASURE_IN[] = { AK_CACHE };  /* 計測(値返し): mesh 1 個入力 */
static const pigArgKind REVOLVE_IN[] = { AK_CACHE, AK_INLINE, AK_INLINE };  /* revolve(cross, angle, segs) / rotate(m,axis,deg) */
static const pigArgKind SECTION_IN[] = { AK_CACHE, AK_INLINE, AK_INLINE, AK_INLINE };  /* section(m,P,N,mode) */
static const pigArgKind MESH1ARG_IN[] = { AK_CACHE, AK_INLINE };  /* translate/scale/mirror/transform(m, param) */
/* transform 系: 入力 mesh(cache)1 個 + スカラ/構造(inline)。mesh は reader、残りは value-parse。 */
static const pigOpEntry OPS[] = {
	{ "box",          SHAPE3_IN, 3, AK_CACHE, OPWIRE(mfaBox),          0, "->mf-mesh3d" },  /* leaf 3D */
	{ "boxa",         SHAPE1_IN, 1, AK_CACHE, OPWIRE(mfaBox), 0, "->mf-mesh3d" },  /* 寸法を array(構造 inline)で */
	{ "sphere",       SHAPE2_IN, 2, AK_CACHE, OPWIRE(mfaSphere), 0, "->mf-mesh3d" },  /* sphere(r, seg): seg=円周分割数(既定 32 相当) */
	{ "icosphere",    SHAPE2_IN, 2, AK_CACHE, OPWIRE(mfaIcosphere), 0, "->mf-mesh3d" },  /* icosphere(r, subdiv): subdiv=細分回数(既定0=20面) */
	{ "union",        BINMESH_IN,2, AK_CACHE, OPWIRE(mfaUnion, mfGeom, mfGeom),        1, "[mf-mesh3d](*)->mf-mesh3d;[mf-cross2d](*)->mf-cross2d", 1 /* ★可換 */ },
	{ "intersection", BINMESH_IN,2, AK_CACHE, OPWIRE(mfaIntersection, mfGeom, mfGeom), 1, "[mf-mesh3d](*)->mf-mesh3d;[mf-cross2d](*)->mf-cross2d", 1 /* ★可換 */ },
	{ "difference",   BINMESH_IN,2, AK_CACHE, OPWIRE(mfaDifference, mfGeom, mfGeom), 1, "[mf-mesh3d](*)->mf-mesh3d;[mf-cross2d](*)->mf-cross2d" },
	{ "export",       EXPORT_IN, 3, AK_CACHE, OPWIRE(mfaExport, mfGeom), 0, "(mf-mesh3d)->ref;(mf-cross2d)->ref" },  /* 出力=D_REF キャッシュ */
	{ "cast",         CAST_IN,   2, AK_CACHE, OPWIRE(mfaCast, mfGeom),         0, "(mf-mesh3d)->mf-mesh3d;(mf-cross2d)->mf-cross2d"
	                                                  ";(cg-mesh3d)->mf-mesh3d;(cg-cross2d)->mf-cross2d"   /* mf-cg-downgrade: MESH/PLY2 */
	                                                  ";(gg-mesh3d)->mf-mesh3d;(ch-mesh3d)->mf-mesh3d"     /* geogram/cherchi は MFM3 を名乗る */
	                                                  ";(nfb-mesh3d)->mf-mesh3d"                            /* mf-nf-downgrade: NEFB のみ (NEF3 は読めない) */ },  /* 変換 op: identity。P2c: cast は sig 出力型で routing → manifold の全出力型 (mf-mesh3d/mf-cross2d) を列挙。cg→mf downgrade は mf_codecs の mf-cg-downgrade codec (MESH→mf-mesh3d / PLY2→mf-cross2d) が担う */
	{ "polygon",      SHAPE1_IN, 1, AK_CACHE, OPWIRE(mfaPolygon), 0, "->mf-cross2d" },  /* polygon([[x,y]...]): 2D 断面 */
	{ "prism",        SHAPE3_IN, 3, AK_CACHE, OPWIRE(mfaPrism), 0, "->mf-mesh3d" },  /* prism(n,h,r) */
	{ "revolve",      REVOLVE_IN,3, AK_CACHE, OPWIRE(mfaRevolve, mfGeom), 0, "(mf-cross2d)->mf-mesh3d" },  /* revolve(cross,angle,segs): 2D→3D */
	{ "volume",       MEASURE_IN,1, AK_INLINE,OPWIRE(mfaVolume, mfGeom),       0, "(mf-mesh3d)->value" },
	{ "bbox",         MEASURE_IN,1, AK_INLINE,OPWIRE(mfaBbox, mfGeom), 0, "(mf-mesh3d)->value" },  /* bbox(mesh): 値返し */
	{ "translate",    MESH1ARG_IN,2, AK_CACHE,OPWIRE(mfaTranslate, mfGeom), 0, "(mf-mesh3d)->mf-mesh3d;(mf-cross2d)->mf-cross2d" },  /* translate(m, [x,y,z]) */
	{ "rotate",       REVOLVE_IN, 3, AK_CACHE,OPWIRE(mfaRotate, mfGeom), 0, "(mf-mesh3d)->mf-mesh3d;(mf-cross2d)->mf-cross2d" },  /* rotate(m, axis, deg) */
	{ "scale",        MESH1ARG_IN,2, AK_CACHE,OPWIRE(mfaScale, mfGeom), 0, "(mf-mesh3d)->mf-mesh3d;(mf-cross2d)->mf-cross2d" },  /* scale(m, s | [sx,sy,sz]) */
	{ "mirror",       MESH1ARG_IN,2, AK_CACHE,OPWIRE(mfaMirror, mfGeom), 0, "(mf-mesh3d)->mf-mesh3d;(mf-cross2d)->mf-cross2d" },  /* mirror(m, axis) */
	{ "transform",    MESH1ARG_IN,2, AK_CACHE,OPWIRE(mfaTransform, mfGeom), 0, "(mf-mesh3d)->mf-mesh3d;(mf-cross2d)->mf-cross2d" },  /* transform(m, matrix12/16) */
	/* ★ #3443: 頂点数 / 面数 (planner の表示を op へ移した)。 */
	{ "nverts",       MEASURE_IN,1, AK_INLINE,OPWIRE(mfaNverts, mfGeom), 0, "(mf-mesh3d)->value;(mf-cross2d)->value" },
	{ "nfaces",       MEASURE_IN,1, AK_INLINE,OPWIRE(mfaNfaces, mfGeom), 0, "(mf-mesh3d)->value;(mf-cross2d)->value" },
	/* ★ 2026-08-19: (mf-cross2d) を申告に追加。実装は元から 2D 断面の面積を返せていたが sig に
	 *   書かれておらず、routing の fallback (入力型の home module へ配送) に助けられて動いていた。
	 *   fallback を撤去したら「sig に無い = 実行できない」で落ちた = **宣言漏れ**が露出した。 */
	{ "area",         MEASURE_IN,1, AK_INLINE,OPWIRE(mfaArea, mfGeom), 0, "(mf-mesh3d)->value;(mf-cross2d)->value" },  /* area(mesh|cross): 値返し */
	{ "valid",        MEASURE_IN,1, AK_INLINE,OPWIRE(mfaValid, mfGeom), 0, "(mf-mesh3d)->value" },  /* valid(mesh): 値返し(Status==NoError) */
	{ "centroid",     MEASURE_IN,1, AK_INLINE,OPWIRE(mfaCentroid, mfGeom), 0, "(mf-mesh3d)->value" },  /* centroid(mesh): 配列返し */
	{ "import",       SHAPE1_IN, 1, AK_CACHE, OPWIRE(mfaImport), 0, "->mf-mesh3d" },  /* import(path): STL/OFF */
	{ "rect",         SHAPE2_IN, 2, AK_CACHE, OPWIRE(mfaRect),         0, "->mf-cross2d" },  /* leaf 2D */
	{ "circle",       SHAPE2_IN, 2, AK_CACHE, OPWIRE(mfaCircle), 0, "->mf-cross2d" },  /* circle(r,segs): 2D */
	{ "ngon",         SHAPE2_IN, 2, AK_CACHE, OPWIRE(mfaNgon), 0, "->mf-cross2d" },  /* ngon(n,r): 2D */
	{ "extrude",      MESH1ARG_IN,2, AK_CACHE,OPWIRE(mfaExtrude, mfGeom),      0, "(mf-cross2d)->mf-mesh3d" },  /* 2D→3D */
	{ "combine",      BINMESH_IN,2, AK_CACHE, OPWIRE(mfaCombine, mfGeom, mfGeom), 0, "[mf-mesh3d](2)->mf-mesh3d;[mf-cross2d](2)->mf-cross2d", 1 /* ★可換 */ },  /* combine(a,b) */
	{ "section",      SECTION_IN,4, AK_CACHE, OPWIRE(mfaSection, mfGeom), 0, "(mf-mesh3d)->mf-cross2d" },  /* section(mesh,P,N,mode): 3D→2D(Z) */
	{ "empty2d",      0,         0, AK_CACHE, OPWIRE(mfaEmpty2D), 0, "->mf-cross2d" },  /* 空集合(2D)。{} は中立元なので別物 */
	{ "empty3d",      0,         0, AK_CACHE, OPWIRE(mfaEmpty3D), 0, "->mf-mesh3d" },   /* 空集合(3D) */
	{ "offset",       REVOLVE_IN,3, AK_CACHE, OPWIRE(mfaOffset, mfGeom),       0, "(mf-cross2d)->mf-cross2d" },  /* ★2D 専用 */
	{ "tube",         SHAPE2_IN, 2, AK_CACHE, OPWIRE(mfaTube),        0, "->mf-mesh3d;->mf-cross2d" },  /* tube(path, segs): 折れ線まわりの掃引管。次元は path 頂点の長さで決まる (#3415・掃引は cgal と共通の common/tube.h) */
	{ "color",        MESH1ARG_IN,2, AK_CACHE,OPWIRE(mfaColor, mfGeom),       0, "(mf-mesh3d)->mf-mesh3d" },  /* color(m, c): 頂点プロパティ ch3..5 に RGB。3D 専用 (2D は cgal 同様エラー)。色つき export は 3MF/AMF */
};
static const int N_OPS = (int)(sizeof(OPS) / sizeof(OPS[0]));

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	mfatsAgent_(
		sPtr<ptsObject> parent);

protected:
	/* 基底 ptsGenericAgent の generic 状態機械へ OPS 表 / 名前を渡すだけ (状態機械は書かない)。 */
	virtual const pigOpEntry*	agent_ops();
	virtual int			agent_n_ops();
	virtual const char*		agent_name();
private:
	TS_DEFARGS
};

TS_END_IMPLEMENT

TS_BEGIN_INTERFACE
#include	"pig/c++/pigOpEntry.h"
class ptsObject;
TS_END_INTERFACE

#endif


mfatsAgent_::mfatsAgent_(TS_ARGS0)
        : ptsGenericAgent_(parent)
{
    TS_CPARGS0
}

const pigOpEntry*	mfatsAgent_::agent_ops()   { return OPS; }
int			mfatsAgent_::agent_n_ops() { return N_OPS; }
const char*		mfatsAgent_::agent_name()  { return "manifold"; }

/* この実行体を "manifold" として登録する。root は具体クラスを知らず enable()/make_agent で起こす。 */
static sPtr<ptsAgent>
mk_mfatsAgent(sPtr<ptsObject> med)
{
	return thNEW(mfatsAgent,(med));
}

/* 自己申告記述子。ops は上の実 OPS[] を再エクスポート (単一ソース)。
 * ★ priority=10。**既定カーネルは cgal (20)** であって manifold ではない
 *   (2026-08-06 に一時 manifold を既定にしたが、その後 cgal 20 > manifold 10 に戻した。
 *    manifold を使うなら cast("manifold",..) / module("manifold.so",{priority}) で明示選択)。
 * namespace scope の const は既定で内部リンケージなので manifest.cpp から extern 参照するため extern 明示。 */
extern const pigModuleType manifold_provides[];
extern const srava_module_descriptor mfatsAgent_descriptor;
extern const srava_module_descriptor mfatsAgent_descriptor = {
	.abi_version   = SRAVA_MODULE_ABI,
	.name          = "manifold",
	.priority      = 10,
	.make_agent    = &mk_mfatsAgent,
	.exec_caps     = (unsigned)(EXEC_THREAD | EXEC_PROCESS),
	.exec_default  = EXEC_THREAD,
	.ops           = OPS,
	.n_ops         = N_OPS,
	.import_exts   = "stl:mf-mesh3d,off:mf-mesh3d",   /* rev4 Phase C: 型付き (3D) */
	.export_exts   = "stl,off,3mf,amf",   /* 色つき 3MF/AMF も (共通ライタ common/mesh3mf.h) */
	.provides      = manifold_provides,   /* 階層 × 型名 × 4CC (ABI v16) */
	.hash_salt     = "\x01" "MFM",   /* キャッシュキー弁別 (#3427 で manifest.cpp から移動) */
	.initialize    = 0,   /* 無し */
	.configure     = 0,   /* ★ v10 (#3441): opts フックは未使用(このモジュールは module() の
	                       *   opts を消費しない) */
};
/* ★ #3427 ③: 旧・静的初期化の register_descriptor は撤去。登録は dlopen 経路
 * (pigModuleRegistry::load_file → register_descriptor) の 1 本 = app 所有レジストリへ。 */
