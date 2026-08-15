/*
 * cgatsAgent — CGAL カーネルの実行体(ptsAgent 派生 = 演算を実際に実行するクラス)。
 *   ptsAgentStub(echo)を拡張し、ディスパッチ
 * テーブルで演算子ごとの計算本体(ptsCalcBody 派生)を起動する。
 *
 * 配線(#3406 段階 4.2 / 2026-08-02 メモ §1): 通信は自分では持たず、親 (ptsObject) に委ねる。
 *   - agent process では parent = ptsApplication(自 stdin/stdout の ptsWirePipe を内包)
 *   - planner 内 thread では parent = ptsMediatorInternal(pigData 直渡し・4.3)
 *   結果もエラーも **pigData のまま** set_result() して FIN へ抜けるだけ (§5/§6)。ワイヤ形への
 *   符号化も A_SAVE_BEGIN/DONE の組み立ても親の判断・役割。着信は parent からの TSE_PACKET。
 *   (旧構成では自分が実態元祖で s2IOstd + ptsWirePipe を直に抱えていた。)
 *
 * 流れ:
 *   INI      : ディスパッチ状態の初期化(通信は parent が確立済み)
 *   WAIT     : C_OP で OPS 検索(無→A_ERROR)。C_ARG_* を型リストと照合して収集(狂い→A_ERROR)。
 *              pigDataCache 入力は reader を開始(Stage2; box は無し)。C_ARG_END で計算本体起動。
 *   CALC     : 計算本体の TSE_RETURN を待ち、結果を引く。Writer を起こす
 *   WRITING〜: cache(mesh)出力は保存 helper の TSE_ASSERT(header+meta 書込済)で **A_SAVE_BEGIN を先に**
 *              送り(下流が書込中 attach 可=同時ストリーミング)、保存完了(TSE_DESTROY)を待って
 *              A_SAVE_DONE。値(インライン)出力は全書込完了後に本文相乗りの A_SAVE_BEGIN。/BYE/wend
 *              (ev 非依存・1 状態 1 write)
 *   ERROR    : A_ERROR + wend
 *
 * 出力シリアライズ(確認①): 値(インライン)・mesh(cache)いずれも calc の get_result()(#3406
 *   2026-07-30: get_body 統合)で本文を受け取り、出力 pigDataCache へ set_body する。保存 helper
 *   (ptsDataCache)が本文の型で codec を選び、mesh は D_META "MESH" + D_CHUNK でストリーム書き込み。
 *
 * ディスパッチ: 演算子名→{入力型リスト, 出力型, 入力 reader 生成子, 計算本体生成子}。生成子は
 *   テンプレート thunk(各クラスに static New 不要)。型は当面 {INLINE, CACHE} の 2 値。
 */
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/ptsApplication.h"   /* ptsApp 値メンバの完全型(ptsObject.h から移動・#3406 4.2) */
#include	"pig/c++/ptsAgent.h"         /* 基底(演算実行体) */
#include	"pig/c++/ptsGenericAgent.h"  /* 共通基底 (状態機械を集約) */
#include	"pig/c++/pigAgentRegistry.h" /* 自分を「この実行ファイルの実行体」として登録 */
#include	"pig/c++/pigData.h"
#include	"pig/c++/pigOpEntry.h"     /* 共通 op エントリ型 (Phase1-4) */
#include	"pig/c++/pigModule.h"      /* srava_module_descriptor (cgal.so 記述子・Phase3b) */
#include	"pig/c++/pigwire.h"
#include	"pig/c++/ptsMediatorPacket.h"     /* Internal 経路の pigData 直渡しパケット (#3406 4.3) */
#include	"pig/c++/ptsDataCache.h"          /* 保存/読出 helper の source 同定 (d_cast・2026-07-29) */
#include	"pig/c++/ptsWireCacheStreamWriterText.h"
#include	"cg/c++/ptscgWireCacheStreamWriterMesh.h"       /* 出力保存 helper(ptscgWireCacheStreamWriterMesh)用 */   /* 値(インライン)出力の保存 */
#include	"cg/c++/cgMesh.h"           /* set_body へ渡す本文の d_cast(完全型) */
#include	"pig/c++/ptsCalcBody.h"
#include	"cg/c++/cgaBox.h"
#include	"cg/c++/cgaPrism.h"
#include	"cg/c++/cgaPyramid.h"
#include	"cg/c++/cgaSphere.h"
#include	"cg/c++/cgaIcosphere.h"
#include	"cg/c++/cgaUnion.h"
#include	"cg/c++/cgaCombine.h"
#include	"cg/c++/cgaIntersection.h"
#include	"cg/c++/cgaDifference.h"
#include	"cg/c++/cgaExport.h"
#ifdef SRAVA_HAVE_HDF5
#include	"cg/c++/cgaVoxelize.h"
#endif
#include	"cg/c++/cgaImport.h"
#include	"cg/c++/cgaTranslate.h"   /* transform 系: 1 mesh + スカラ */
#include	"cg/c++/cgaRotate.h"
#include	"cg/c++/cgaMirror.h"
#include	"cg/c++/cgaScale.h"
#include	"cg/c++/cgaTransform.h"
#include	"cg/c++/cgaColor.h"       /* color(mesh, c): 面色 f:color */
#include	"cg/c++/cgaRect.h"        /* 2D プリミティブ */
#include	"cg/c++/cgaNgon.h"
#include	"cg/c++/cgaCircle.h"
#include	"cg/c++/cgaPolygon.h"
#include	"cg/c++/cgaLine.h"
#include	"cg/c++/cgaSection.h"
#include	"cg/c++/cgaEmpty2D.h"
#include	"cg/c++/cgaEmpty3D.h"
#include	"cg/c++/cgaExtrude.h"     /* 2D→3D */
#include	"cg/c++/cgaTube.h"        /* 3D 折れ線まわりの掃引管 */
#include	"cg/c++/cgaRevolve.h"
#include	"cg/c++/cgaOffset.h"
#include	"cg/c++/cgaArea.h"        /* 計測(値返し op): area(m) */
#include	"cg/c++/cgaValid.h"       /* 検査(値返し op): valid(m) */
#include	"cg/c++/cgaRepair.h"      /* 修復(mesh 返し op): repair(m) */
#include	"cg/c++/cgaVolume.h"      /* 計測(値返し op): volume(m) */
#include	"cg/c++/cgaPerimeter.h"   /* 計測(値返し op): perimeter(m) */
#include	"cg/c++/cgaCentroid.h"    /* 計測(配列返し op): centroid(m) */
#include	"cg/c++/cgaBbox.h"        /* 計測(入れ子配列返し op): bbox(m) */
#include	"cg/c++/cgaDistance.h"    /* 近接(値返し op): distance(a,b) */
#include	"cg/c++/cgaClosest.h"     /* 近接(配列返し op): closest(a,b) */
#include	"cg/c++/cgaFarthest.h"    /* 近接(配列返し op): farthest(a,b) */
#include	"cg/c++/cgaThinSpots.h"   /* 肉厚 SDF(入れ子配列返し op): thin_spots(m,t) */
#include	"cg/c++/cgaCast.h"        /* cast("exact", mesh): カーネル明示変換(MFM3→EPECK 無損失昇格) */
#include	"ts2/c++/stdEvent.h"
#include	"_ts2/c++/cgatsAgent_.h"

#include	<string.h>
#include	<stdlib.h>   /* getenv(テスト用フォールトインジェクション) */
#include	<unistd.h>   /* usleep(テスト用の計算遅延) */
#include	<stdio.h>
#include	<sys/time.h>

CLASS_TINYSTATE(cg/c++/cgatsAgent,pig/c++/ptsGenericAgent)

/* ---- ディスパッチテーブル(ファイルスコープ) ---- */
/* 型は pig 層の共通型 (pigOpEntry.h・.so 化 Phase1-4)。旧名はエイリアスで温存し OPS 本体は無改修。 */
typedef pigArgKind ArgKind;       /* AK_INLINE / AK_CACHE は pigOpEntry.h 由来 */
typedef pigOpEntry cgaOpEntry;

/* テンプレート thunk: 各クラスに static New を書かずコンストラクタ呼びを生成。
 * 入力は **ポインタ** で渡す(親 cgatsAgent が所有・寿命中生存。sArray の値渡し/コピーは避ける)。
 * 戻り型は pigCalcFactory と一致 (OPS の mkCalc へそのまま入る)。 */
template<class T> static sPtr<ptsCalcBody>
mkCalcT(sPtr<ptsObject> p, sArray<sPtr<pigData> > *a, sPtr<stdString> t) { return thNEW(T,(p, a, t)); }

static const ArgKind SHAPE3_IN[] = { AK_INLINE, AK_INLINE, AK_INLINE };  /* box/prism/pyramid */
static const ArgKind SHAPE2_IN[] = { AK_INLINE, AK_INLINE };             /* rect(w,h) 2D */
static const ArgKind SHAPE1_IN[] = { AK_INLINE };                        /* sphere(r) / boxa([..]) */
static const ArgKind EXPORT_IN[] = { AK_INLINE, AK_CACHE, AK_INLINE };  /* export(path, mesh, unit) */
static const ArgKind EXPORTVOX_IN[] = { AK_INLINE, AK_INLINE };  /* export_vox(path, params, mesh…可変) */
static const ArgKind BINMESH_IN[] = { AK_CACHE, AK_CACHE };  /* 2 mesh 入力(cache ハンドル→reader 読み) */
/* transform 系: 入力 mesh(cache)1 個 + スカラ/構造(inline)。mesh は reader、残りは value-parse。 */
static const ArgKind ROTATE_IN[]  = { AK_CACHE, AK_INLINE, AK_INLINE };             /* rotate(m,axis,deg) */
static const ArgKind SECTION_IN[] = { AK_CACHE, AK_INLINE, AK_INLINE, AK_INLINE };  /* section(m,P,N,mode) */
static const ArgKind MESH1ARG_IN[] = { AK_CACHE, AK_INLINE };  /* translate(m,vec) / mirror(m,axis) / transform(m,matrix) */
static const ArgKind MEASURE_IN[] = { AK_CACHE };  /* 計測(値返し): mesh 1 個入力 → 値(AK_INLINE)出力 */
static const ArgKind THIN_IN[] = { AK_CACHE, AK_INLINE, AK_INLINE, AK_INLINE };  /* thin_spots(m, t, rays, cone) */
static const ArgKind CAST_IN[] = { AK_INLINE, AK_CACHE };  /* cast(type_string, mesh): type=inline, mesh=cache(reader) */
static const cgaOpEntry OPS[] = {
	/* ★ rev4 Phase B spike: 代表 op に型シグネチャ (実装型・cg-mesh3d/cg-cross2d) を付与。
	 *   残りの op と decide_executor 消費は Q-A 書き味確認後 (B-1 全注釈 + B-2)。 */
	{ "box",          SHAPE3_IN, 3, AK_CACHE, &mkCalcT<cgaBox>,          0, "->cg-mesh3d" },  /* leaf 3D 生成 (mesh 入力なし) */
	{ "boxa",         SHAPE1_IN, 1, AK_CACHE, &mkCalcT<cgaBox>, 0, "->cg-mesh3d" },  /* 寸法を array(構造 inline)で */
	{ "import",       SHAPE1_IN, 1, AK_CACHE, &mkCalcT<cgaImport>, 0, "->cg-mesh3d;->cg-cross2d" },  /* import(path): 外部ファイル読み */
	{ "prism",        SHAPE3_IN, 3, AK_CACHE, &mkCalcT<cgaPrism>, 0, "->cg-mesh3d" },
	{ "pyramid",      SHAPE3_IN, 3, AK_CACHE, &mkCalcT<cgaPyramid>, 0, "->cg-mesh3d" },
	{ "sphere",       SHAPE2_IN, 2, AK_CACHE, &mkCalcT<cgaSphere>, 0, "->cg-mesh3d" },  /* sphere(r, seg): seg=円周分割数(既定 32 相当) */
	{ "icosphere",    SHAPE2_IN, 2, AK_CACHE, &mkCalcT<cgaIcosphere>, 0, "->cg-mesh3d" },  /* icosphere(r, subdiv): subdiv=細分回数(既定0=20面) */
	{ "union",        BINMESH_IN,2, AK_CACHE, &mkCalcT<cgaUnion>,        0, "(cg-mesh3d,cg-mesh3d)->cg-mesh3d;(cg-mesh3d,mf-mesh3d)->cg-mesh3d;(mf-mesh3d,cg-mesh3d)->cg-mesh3d;(cg-cross2d,cg-cross2d)->cg-cross2d;(cg-cross2d,mf-cross2d)->cg-cross2d;(mf-cross2d,cg-cross2d)->cg-cross2d" },  /* 二項 3D */
	{ "combine",      BINMESH_IN,2, AK_CACHE, &mkCalcT<cgaCombine>, 0, "(cg-mesh3d,cg-mesh3d)->cg-mesh3d;(cg-mesh3d,mf-mesh3d)->cg-mesh3d;(mf-mesh3d,cg-mesh3d)->cg-mesh3d;(cg-cross2d,cg-cross2d)->cg-cross2d;(cg-cross2d,mf-cross2d)->cg-cross2d;(mf-cross2d,cg-cross2d)->cg-cross2d" },  /* +++ 交差許容の単純合体(viewer 用) */
	{ "intersection", BINMESH_IN,2, AK_CACHE, &mkCalcT<cgaIntersection>, 0, "(cg-mesh3d,cg-mesh3d)->cg-mesh3d;(cg-mesh3d,mf-mesh3d)->cg-mesh3d;(mf-mesh3d,cg-mesh3d)->cg-mesh3d;(cg-cross2d,cg-cross2d)->cg-cross2d;(cg-cross2d,mf-cross2d)->cg-cross2d;(mf-cross2d,cg-cross2d)->cg-cross2d" },
	{ "difference",   BINMESH_IN,2, AK_CACHE, &mkCalcT<cgaDifference>, 0, "(cg-mesh3d,cg-mesh3d)->cg-mesh3d;(cg-mesh3d,mf-mesh3d)->cg-mesh3d;(mf-mesh3d,cg-mesh3d)->cg-mesh3d;(cg-cross2d,cg-cross2d)->cg-cross2d;(cg-cross2d,mf-cross2d)->cg-cross2d;(mf-cross2d,cg-cross2d)->cg-cross2d" },
	{ "export",       EXPORT_IN, 3, AK_CACHE, &mkCalcT<cgaExport>, 0, "(cg-mesh3d)->value;(cg-cross2d)->value;(mf-mesh3d)->value;(mf-cross2d)->value" },  /* 出力=D_REF。mf 入力も引受 (Stage2: export の読解 capability を sig 化・cgal は universal reader) */
#ifdef SRAVA_HAVE_HDF5
	{ "export_vox",   EXPORTVOX_IN,2,AK_CACHE, &mkCalcT<cgaVoxelize>, 1, "(cg-mesh3d)->value;(mf-mesh3d)->value" },  /* voxel化→vox.h5(出力=D_REF)。末尾メッシュ可変(variadic=1)。HDF5 必須・mf 入力も引受 */
#endif
	{ "translate",    MESH1ARG_IN,2,AK_CACHE, &mkCalcT<cgaTranslate>, 0, "(cg-mesh3d)->cg-mesh3d;(cg-cross2d)->cg-cross2d" },
	{ "rotate",       ROTATE_IN, 3, AK_CACHE, &mkCalcT<cgaRotate>, 0, "(cg-mesh3d)->cg-mesh3d;(cg-cross2d)->cg-cross2d" },
	{ "mirror",       MESH1ARG_IN,2,AK_CACHE, &mkCalcT<cgaMirror>, 0, "(cg-mesh3d)->cg-mesh3d;(cg-cross2d)->cg-cross2d" },
	{ "scale",        MESH1ARG_IN,2,AK_CACHE, &mkCalcT<cgaScale>, 0, "(cg-mesh3d)->cg-mesh3d;(cg-cross2d)->cg-cross2d" },
	{ "transform",    MESH1ARG_IN,2,AK_CACHE, &mkCalcT<cgaTransform>, 0, "(cg-mesh3d)->cg-mesh3d;(cg-cross2d)->cg-cross2d" },
	{ "color",        MESH1ARG_IN,2,AK_CACHE, &mkCalcT<cgaColor>, 0, "(cg-mesh3d)->cg-mesh3d;(cg-cross2d)->cg-cross2d;(mf-mesh3d)->cg-mesh3d;(mf-cross2d)->cg-cross2d" },  /* 面色 f:color (mf 入力も引受=cgal 専用) */
	{ "rect",         SHAPE2_IN, 2, AK_CACHE, &mkCalcT<cgaRect>,        0, "->cg-cross2d" },  /* leaf 2D 生成 */
	{ "ngon",         SHAPE2_IN, 2, AK_CACHE, &mkCalcT<cgaNgon>, 0, "->cg-cross2d" },  /* 2D 正 n 角形 */
	{ "circle",       SHAPE2_IN, 2, AK_CACHE, &mkCalcT<cgaCircle>, 0, "->cg-cross2d" },  /* circle(r, segs): segs=多角形辺数(既定32) */
	{ "polygon",      SHAPE1_IN, 1, AK_CACHE, &mkCalcT<cgaPolygon>, 0, "->cg-cross2d" },  /* 2D 明示点列 */
	{ "line",         SHAPE1_IN, 1, AK_CACHE, &mkCalcT<cgaLine>, 0, "->cg-cross2d" },  /* 2D ガイド(寸法線・開ポリライン) */
	{ "extrude",      MESH1ARG_IN,2,AK_CACHE, &mkCalcT<cgaExtrude>,     0, "(cg-cross2d)->cg-mesh3d" },  /* 2D→3D 角柱 (次元変化・Q-C) */
	{ "tube",         SHAPE2_IN, 2, AK_CACHE, &mkCalcT<cgaTube>, 0, "->cg-mesh3d;->cg-cross2d" },  /* tube(path, segs): 折れ線まわりの掃引管。次元は path 頂点の長さで決まる (3D=掃引立体 / 2D=帯)。import と同じ多出力注釈 */
	{ "revolve",      ROTATE_IN, 3, AK_CACHE, &mkCalcT<cgaRevolve>, 0, "(cg-cross2d)->cg-mesh3d" },  /* revolve(m,angle,segs): 2D→3D 回転体 */
	{ "offset",       ROTATE_IN, 3, AK_CACHE, &mkCalcT<cgaOffset>,      0, "(cg-cross2d)->cg-cross2d;(cg-mesh3d)->cg-mesh3d;(mf-mesh3d)->cg-mesh3d" },  /* ★2D+3D + mf 3D の gap 埋め (manifold は 2D offset のみ・3D は cgal が引き受ける = 旧 coercion の明示 sig 化) */
	{ "area",         MEASURE_IN,1, AK_INLINE,&mkCalcT<cgaArea>, 0, "(cg-mesh3d)->value;(cg-cross2d)->value" },  /* area(m): 値返し(2D 面積 / 3D 表面積) */
	{ "valid",        MEASURE_IN,1, AK_INLINE,&mkCalcT<cgaValid>, 0, "(cg-mesh3d)->value;(cg-cross2d)->value" },  /* valid(m): 値返し(1=正常/0=問題) */
	{ "repair",       MEASURE_IN,1, AK_CACHE, &mkCalcT<cgaRepair>, 0, "(cg-mesh3d)->cg-mesh3d;(cg-cross2d)->cg-cross2d;(mf-mesh3d)->cg-mesh3d;(mf-cross2d)->cg-cross2d" },  /* repair(m): mesh 返し(3D autorefine / 2D even-odd)・mf 入力も引受 */
	{ "section",      SECTION_IN,4, AK_CACHE, &mkCalcT<cgaSection>, 0, "(cg-mesh3d)->cg-cross2d" },  /* section(m,P,N,mode): mode 0=平面ちょうど/-1=直下/+1=直上 */
	{ "empty2d",      0,         0, AK_CACHE, &mkCalcT<cgaEmpty2D>, 0, "->cg-cross2d" },  /* 空集合(2D)。{} は中立元なので別物 */
	{ "empty3d",      0,         0, AK_CACHE, &mkCalcT<cgaEmpty3D>, 0, "->cg-mesh3d" },   /* 空集合(3D) */
	{ "volume",       MEASURE_IN,1, AK_INLINE,&mkCalcT<cgaVolume>,      0, "(cg-mesh3d)->value" },  /* 値出力 (out=value) */
	{ "perimeter",    MEASURE_IN,1, AK_INLINE,&mkCalcT<cgaPerimeter>, 0, "(cg-cross2d)->value;(mf-cross2d)->value" },  /* perimeter(m): 値返し(2D 境界長・3D エラー)・mf 入力も引受 */
	{ "centroid",     MEASURE_IN,1, AK_INLINE,&mkCalcT<cgaCentroid>, 0, "(cg-mesh3d)->value;(cg-cross2d)->value" },  /* centroid(m): 配列返し([x,y]/[x,y,z]) */
	{ "bbox",         MEASURE_IN,1, AK_INLINE,&mkCalcT<cgaBbox>, 0, "(cg-mesh3d)->value;(cg-cross2d)->value" },  /* bbox(m): 入れ子配列返し([min隅,max隅]) */
	{ "distance",     BINMESH_IN,2, AK_INLINE,&mkCalcT<cgaDistance>, 0, "(cg-mesh3d,cg-mesh3d)->value;(cg-mesh3d,mf-mesh3d)->value;(mf-mesh3d,cg-mesh3d)->value;(mf-mesh3d,mf-mesh3d)->value" },  /* distance(a,b): 値返し(3D 最近接距離・近似) */
	{ "closest",      BINMESH_IN,2, AK_INLINE,&mkCalcT<cgaClosest>, 0, "(cg-mesh3d,cg-mesh3d)->value;(cg-mesh3d,mf-mesh3d)->value;(mf-mesh3d,cg-mesh3d)->value;(mf-mesh3d,mf-mesh3d)->value" },  /* closest(a,b): 配列返し([d,[pa],[pb]]) */
	{ "farthest",     BINMESH_IN,2, AK_INLINE,&mkCalcT<cgaFarthest>, 0, "(cg-mesh3d,cg-mesh3d)->value;(cg-mesh3d,mf-mesh3d)->value;(mf-mesh3d,cg-mesh3d)->value;(mf-mesh3d,mf-mesh3d)->value" },  /* farthest(a,b): 配列返し(頂点総当り・厳密) */
	{ "thin_spots",   THIN_IN,   4, AK_INLINE,&mkCalcT<cgaThinSpots>, 0, "(cg-mesh3d)->value;(mf-mesh3d)->value" },  /* thin_spots(m,t,rays,cone): 肉厚<t の面の[[x,y,z,thk],..](SDF・cone=コーン全角°)・mf 入力も引受 */
	{ "cast",         CAST_IN,   2, AK_CACHE, &mkCalcT<cgaCast>,        0, "(cg-mesh3d)->cg-mesh3d;(mf-mesh3d)->cg-mesh3d;(cg-cross2d)->cg-cross2d;(mf-cross2d)->cg-cross2d" },  /* ★変換 op: identity + mf→cg 昇格。P2c: cast は sig 出力型で routing (目標型を産出できるモジュールへ) → 全出力型 (cg-mesh3d/cg-cross2d) を列挙 */
};
static const int N_OPS = (int)(sizeof(OPS) / sizeof(OPS[0]));

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	cgatsAgent_(
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


cgatsAgent_::cgatsAgent_(TS_ARGS0)
        : ptsGenericAgent_(parent)
{
    TS_CPARGS0
}

const pigOpEntry*	cgatsAgent_::agent_ops()   { return OPS; }
int			cgatsAgent_::agent_n_ops() { return N_OPS; }
const char*		cgatsAgent_::agent_name()  { return "cgal"; }

/* この実行ファイル(srava_agent)の実行体として自分を登録する(#3406 4.2)。
 * root(ptsApplication)は具体クラスを知らず、enable() でこの生成子を引いて起こす。
 * §5 の .so 化では、この登録が dlopen 時の登録に置き換わる。 */
static sPtr<ptsAgent>
mk_cgatsAgent(sPtr<ptsObject> med)
{
	return thNEW(cgatsAgent,(med));
}

/* ★ .so 化 Phase 3b: cgal カーネルの **フル記述子** (make_agent + OPS 付き)。cgal.so の
 * manifest.cpp がこれを extern 参照して srava_module() で公開する。planner 側の meta-only 記述子
 * (pigfModuleAgent.cpp の cgal_module_descriptor) とは別物で、こちらは実行体 (agent) を含む。
 * OPS/mk_cgatsAgent は同一 TU の static なのでここで組む。exec_caps=PROCESS のみ (CGAL は
 * thread 不可)・priority=0 (< manifold 10)・拡張子は多形式。静的自己登録はしない (dlopen 時に
 * ローダが register する / 静的 agent は従来どおり lookup(0) で引く)。 */
extern const pigModuleCodec cgal_codecs[];   /* cgCacheCodec.cpp (Phase 4③') */
extern const srava_module_descriptor cgatsAgent_descriptor;
const srava_module_descriptor cgatsAgent_descriptor = {
	/* ★ .so 化 Phase 4c: priority 20 (> manifold 10) = **cgal を既定カーネルに** (ひさ判断
	 * 2026-08-08: manifold は watertight 前提でサイレント破綻し得るため opt-in)。manifold は
	 * cast("manifold",..) / module("manifold.so",{priority}) で明示選択する。 */
	SRAVA_MODULE_ABI, "cgal", 20,
	&mk_cgatsAgent, (unsigned)EXEC_PROCESS, EXEC_PROCESS,
	OPS, N_OPS,
	/* ★ rev4 Phase C: import_exts を **型付き** ("ext:出力型") に。形式が出力型を決める (svg/dxf=2D・
	 *   mesh 系=3D)。routing が import の出力型をこれで確定しスタンプする (polymorphic import 解消)。 */
	"off:cg-mesh3d,stl:cg-mesh3d,obj:cg-mesh3d,ply:cg-mesh3d,svg:cg-cross2d,dxf:cg-cross2d",
	"off,stl,obj,ply,3mf,amf,svg,dxf",   /* P2d: amf 追加 (型軸 export routing ② で解決させ arg_module fallback 依存を解消) */
	"MESH,PLY2",   /* codec_tags: cgal がサポートする 4CC (mf の MFM3/MFC2 昇格読みは cgal_codecs の cg-mf-upgrade が担う) */
	cgal_codecs,   /* reader/writer factory (自型 MESH/PLY2 + MFM3/MFC2 昇格読み) */
	"cg-mesh3d,cg-cross2d", "MESH,PLY2",   /* types / type_tags (#3427 で manifest.cpp から移動) */
	0,   /* hash_salt: 基準カーネルなのでソルト無し (既存キャッシュキーを byte 不変に保つ) */
};
