/*
 * vdtsAgent — OpenVDB ボリュームモジュールの実行体 (#3434 P2)。
 * 状態機械は共通基底 ptsGenericAgent が持つので、ここは **OPS 表と記述子だけ**。
 * ggtsAgent.cpp のミラー。
 */
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/ptsGenericAgent.h"
#include	"pig/c++/pigOpEntry.h"
#include	"pig/c++/pigModule.h"
#include	"pig/c++/ptsCalcBody.h"
#include	"vd/c++/vdGrid.h"
#include	"vd/c++/vdaVolume.h"
#include	"vd/c++/vdaVoxels.h"
#include	"vd/c++/vdaUnion.h"
#include	"vd/c++/vdaIntersection.h"
#include	"vd/c++/vdaDifference.h"
#include	"vd/c++/vdaRenormalize.h"
#include	"vd/c++/vdaOffset.h"
#include	"_ts2/c++/vdtsAgent_.h"

#include	<stdio.h>    /* SRAVA_LOAD_LOG の診断出力 */
#include	<stdlib.h>   /* getenv */

CLASS_TINYSTATE(vd/c++/vdtsAgent,pig/c++/ptsGenericAgent)

/* 計算本体生成子 thunk (各モジュール .cpp に据え置く定型)。 */
template <class T>
static sPtr<ptsCalcBody>
mkCalcT(sPtr<ptsObject> parent, sArray<sPtr<pigData> > *args, sPtr<stdString> target)
{
	return sPtr<ptsCalcBody>::d_cast(thNEW(T,(parent, args, target)));
}

static const pigArgKind MESHDX_IN[]  = { AK_CACHE, AK_INLINE };   /* voxelize(mesh, dx) */
static const pigArgKind MEASURE_IN[] = { AK_CACHE };              /* grid 1 個入力 */
static const pigArgKind GRIDISO_IN[] = { AK_CACHE, AK_INLINE };   /* isosurface(v, iso) */
static const pigArgKind BINGRID_IN[] = { AK_CACHE, AK_CACHE };    /* 2 grid 入力 */
/* ★ offset(v, d, subdiv)。パーサが offset を**常に 3 引数へ正規化する** (subdiv 既定 1 を補う・
 *   ns_sravaParser.y:669) ので、こちらも 3 で受ける。★subdiv は「近似球の細分化レベル」で、
 *   メッシュ系の 3D offset が **半径 d の球との Minkowski 和**で実装されているためのパラメータ。
 *   距離場には近似球が存在しない (等値面を動かすだけ) ので **この引数は意味を持たず、無視する**。
 *   そこがボリューム表現の利点そのものなので、無視することがむしろ正しい。 */
static const pigArgKind OFFSET_IN[]  = { AK_CACHE, AK_INLINE, AK_INLINE };

static const pigOpEntry OPS[] = {
	/* ★ 2026-08-22 (#3434): **入口/出口 (voxelize / isosurface) はここから外した**。
	 *   メッシュとの変換は openvdb_mf.so / openvdb_cg.so が **両側の本物のクラス**で行う。
	 *   旧実装は mf-mesh3d を名乗る新クラス vdMesh を openvdb 側に作っており、
	 *   「新しい型を作らない」の違反で in-proc の d_cast 失敗の原因だった。 */
	/* ★ ブール = 点ごとの min/max。**位相の場合分けが存在しない**ので、汚い入力でも必ず答えが出る。
	 *   自型どうしだけを申告する — 他カーネルの mesh を混ぜる行は書かない。 */
	{ "union",        BINGRID_IN, 2, AK_CACHE, OPWIRE(vdaUnion, vdGeom, vdGeom),        1, "[" VD_TYPE "](*)->" VD_TYPE, 1 /* ★可換 */ },
	{ "intersection", BINGRID_IN, 2, AK_CACHE, OPWIRE(vdaIntersection, vdGeom, vdGeom), 1, "[" VD_TYPE "](*)->" VD_TYPE, 1 /* ★可換 */ },
	{ "difference",   BINGRID_IN, 2, AK_CACHE, OPWIRE(vdaDifference, vdGeom, vdGeom),   1, "[" VD_TYPE "](*)->" VD_TYPE },
	/* ★ 距離場が得意とされる操作。メッシュ系の 3D offset は球との Minkowski 和 (nef.so・重い)。
	 *   入力型が disjoint (vd-grid3d) なので nef の (nf/cg/mf-mesh3d) 行とは衝突しない。 */
	{ "offset",      OFFSET_IN,  3, AK_CACHE, OPWIRE(vdaOffset, vdGeom),      0, "(" VD_TYPE ")->" VD_TYPE },
	/* ★ 距離場を作り直す (|grad| = 1 を回復)。ブールの結果は真の距離場ではなくなるので、
	 *   levelSetVolume で測る前や、offset で帯を広げたいときに要る。**明示 op** にしてある
	 *   (作り直しはブール本体より重いので、連鎖の途中で毎回払わせない)。 */
	{ "renormalize", GRIDISO_IN, 1, AK_CACHE, OPWIRE(vdaRenormalize, vdGeom), 0, "(" VD_TYPE ")->" VD_TYPE },
	/* 計測: ★ level set から **メッシュを作らずに**直接出せるのがボリューム表現の利点。
	 *   ただし |grad| = 1 を仮定するので、ブールの結果に対しては renormalize してから測るか、
	 *   isosurface でメッシュにしてから測ること (理由は vdaRenormalize.cpp のコメント)。 */
	{ "volume",    MEASURE_IN, 1, AK_INLINE, OPWIRE(vdaVolume, vdGeom),  0, "(" VD_TYPE ")->value" },
	/* 活性ボクセル数 = 狭帯域の実サイズ。「解像度 vs 精度 vs コスト」を測るための診断 op。 */
	{ "voxels",    MEASURE_IN, 1, AK_INLINE, OPWIRE(vdaVoxels, vdGeom),  0, "(" VD_TYPE ")->value" },
};
static const int N_OPS = (int)(sizeof(OPS) / sizeof(OPS[0]));

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	vdtsAgent_(
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


vdtsAgent_::vdtsAgent_(TS_ARGS0)
        : ptsGenericAgent_(parent)
{
    TS_CPARGS0
}

const pigOpEntry*	vdtsAgent_::agent_ops()	  { return OPS; }
int			vdtsAgent_::agent_n_ops() { return N_OPS; }
const char*		vdtsAgent_::agent_name()  { return VD_MODULE_NAME; }


static sPtr<ptsAgent>
mk_vdtsAgent(sPtr<ptsObject> parent)
{
	return sPtr<ptsAgent>::d_cast(thNEW(vdtsAgent,(parent)));
}

/* 自己申告記述子。
 *  - priority=1: **既定カーネルにはならない** (既定は cgal の 20)。そもそも leaf 生成 op を
 *    持たない (ボリュームはメッシュから作るもの) ので、既定になっても意味が無い。
 *    ★ 現在の梯子 (2026-08-25): cgal 20 > manifold 10 > geogram 6 > nef 5 > pipe_proximity 4 >
 *      occt 2 > openvdb 1 > (テスト専用は負値: demo -1 / d3 -2 / d2 -3 / d4 -4 / d5 -5)。
 *      ★同点の勝敗は不定なので、同梱モジュールは互いに重複させない。
 *  - exec: PROCESS のみ。OpenVDB はプロセス全体のグローバル初期化 (openvdb::initialize) と
 *    **TBB** を使うので、in-proc (planner 内スレッド) の安全性は未検証。geogram と同じく
 *    まず process から。★in-proc 化は #3419 (op 内並列と op 間並列の調停) とまとめて扱う —
 *    そこに踏み込むと planner のプロセスに TBB のスレッドプールが入り、srava 自身の
 *    DAG 並列とスレッドを取り合うため。
 */
extern const pigModuleType openvdb_provides[];
extern const srava_module_descriptor vdtsAgent_descriptor;
extern const srava_module_descriptor vdtsAgent_descriptor = {
	.abi_version   = SRAVA_MODULE_ABI,
	.name          = VD_MODULE_NAME,
	.priority      = 1,
	.make_agent    = &mk_vdtsAgent,
	/* ★ 実験用 (2026-08-22): in-proc も **可能**にする。⚠ 既定は PROCESS のまま変えない。
	 *   in-proc を試すには module("openvdb.so",{exec_default:"thread"}) と明示すること。
	 *   狙い: in-proc agent は planner と同一アドレス空間なので **C_MEM に遅延なく含まれる**
	 *   (process agent の pid 登録遅れが原理的に無い)。メモリ制御が効くかを測るため。
	 *   ⚠ 安全性は未検証: openvdb のグローバル初期化と TBB を planner プロセスに持ち込む。 */
	.exec_caps     = (unsigned)(EXEC_THREAD | EXEC_PROCESS),
	.exec_default  = EXEC_PROCESS,
	.ops           = OPS,
	.n_ops         = N_OPS,
	.import_exts   = "",   /* なし */
	.export_exts   = "",   /* なし (等値面を書きたいなら cast でメッシュ系へ) */
	.provides      = openvdb_provides,   /* 階層 × 型名 × 4CC (ABI v16) */
	/* ★ 2026-08-22: **mf-mesh3d の申告を外した**。旧実装は mf-mesh3d を名乗る新クラス vdMesh を
	 *   作っており「新しい型を作らない」の違反だった (in-proc で d_cast が失敗する原因)。
	 *   メッシュとの変換は openvdb_mf.so / openvdb_cg.so が本物の相手クラスで行う。 */
	.hash_salt     = VD_SALT,
	/* initialize: 無し。openvdb::initialize() は vdGrid::ensure_init() が全 op の入口で
	 * 1 回だけ呼んでいる (このモジュールは EXEC_PROCESS = 1 プロセス 1 モジュール)。
	 * in-proc 化 (#3419) するときに、ここへ移すかを再検討する。 */
	/* ★ #3436 P4: N' = 1 ノードあたり受け取りたい最大項数 (policy)。openvdb は既定 2 のままでなく
	 * **16** を申告する。5 カーネル中 openvdb だけが実運用条件 (op 間並列あり) でも n 項が得だから:
	 *   ・格子の min/max には位相の場合分けが無く、α (c(k) ∝ k^α) が 1.064 と 1 に最も近い
	 *   ・得の正体は並列度ではなく **総仕事量**。中間グリッドの生成・直列化が丸ごと消える
	 *   ・並列度は落ちない (openvdb のモデルは並列性が **葉 (voxelize)** にあり、arity が減らすのは
	 *     union 節点だけ。geogram は葉が軽く並列性が木にあるので逆に落ちる)
	 * ⚠ これ以上上げない理由: 速さは頭打ちになる一方、arity を上げるほど中間キャッシュが消えて
	 *   **増分再計算が効かなくなる**。 */
	.arity         = 16,

	.initialize    = 0,
	/* ★ #3441 (ABI v10): module(so,{threads:N}) → op あたりの TBB 予算 (task_arena)。
	 *   受け口の実体は vdGrid::configure (vdGrid.cpp)。openvdb と openvdb_mf は
	 *   同じ libsrava_vd を共有するので、予算の static も共有される。 */
	.configure     = &vdGrid::configure,
};
