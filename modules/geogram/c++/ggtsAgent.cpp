/*
 * ggtsAgent — geogram モジュールの実行体 (ptsGenericAgent 派生・#3435 P3)。
 *   状態機械は共通基底 ptsGenericAgent に集約済み。この派生は **OPS[] 表と記述子だけ**を持つ。
 *   mfatsAgent / nftsAgent と同一構造。
 *
 * ★ このモジュールの位置づけ (#3435): 「厳密のまま CGAL より速い」を §6 の実データにする。
 *   geogram の本領は多オペランド (variadic CSG) だが **そこは本体改修 (#3436 P4)** が要るので、
 *   ここでは二項ブールだけを入れる (モジュール投入と本体改修を分離する方針)。
 */
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/ptsAgent.h"
#include	"pig/c++/ptsGenericAgent.h"
#include	"pig/c++/pigAgentRegistry.h"
#include	"pig/c++/pigModuleRegistry.h"
#include	"pig/c++/pigModule.h"
#include	"pig/c++/pigData.h"
#include	"pig/c++/pigOpEntry.h"
#include	"pig/c++/ptsCalcBody.h"
#include	"gg/c++/ggMesh.h"
#include	"gg/c++/ggaBox.h"
#include	"gg/c++/ggaSphere.h"
#include	"gg/c++/ggaUnion.h"
#include	"gg/c++/ggaIntersection.h"
#include	"gg/c++/ggaDifference.h"
#include	"gg/c++/ggaSolidify.h"
#include	"gg/c++/ggaVolume.h"
#include	"gg/c++/ggaNverts.h"
#include	"gg/c++/ggaNfaces.h"
#include	"gg/c++/ggaExport.h"
#include	"gg/c++/ggaCast.h"
#include	"gg/c++/ggaTranslate.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ggtsAgent_.h"

#include	<string.h>

CLASS_TINYSTATE(gg/c++/ggtsAgent,pig/c++/ptsGenericAgent)

/* ---- ディスパッチテーブル ---- */

static const pigArgKind SHAPE3_IN[]  = { AK_INLINE, AK_INLINE, AK_INLINE };  /* box(w,h,d) */
static const pigArgKind SHAPE2_IN[]  = { AK_INLINE, AK_INLINE };             /* sphere(r,seg) */
static const pigArgKind SHAPE1_IN[]  = { AK_INLINE };                        /* boxa([w,h,d]) */
static const pigArgKind EXPORT_IN[]  = { AK_INLINE, AK_CACHE, AK_INLINE };   /* export(path, mesh, unit) */
static const pigArgKind BINMESH_IN[] = { AK_CACHE, AK_CACHE };               /* 2 mesh 入力 */
static const pigArgKind CAST_IN[]    = { AK_INLINE, AK_CACHE };              /* cast(type, mesh) */
static const pigArgKind MEASURE_IN[] = { AK_CACHE };                         /* mesh 1 個入力 */
static const pigArgKind MESH1ARG_IN[]= { AK_CACHE, AK_INLINE };              /* translate(m,[x,y,z]) */

static const pigOpEntry OPS[] = {
	{ "box",          SHAPE3_IN, 3, AK_CACHE, OPWIRE(ggaBox),          0, "->" GG_TYPE },
	{ "boxa",         SHAPE1_IN, 1, AK_CACHE, OPWIRE(ggaBox),          0, "->" GG_TYPE },
	{ "sphere",       SHAPE2_IN, 2, AK_CACHE, OPWIRE(ggaSphere),       0, "->" GG_TYPE },
	/* ブール: 自型どうし + 混成 (片側が mf の raw double mesh)。混成は cache reader の
	 * gg-mf-upgrade codec が MFM3 → gg へ昇格読みして成立する。
	 * ★all-foreign ((mf,mf)) は書かない — manifold 自身が同じ op を持つので曖昧になる (disjoint 原則)。
	 * ★cg-mesh3d との混成は **読めるようになった後も書かない** (2026-08-19)。理由は 2 つ:
	 *   ① cgal.so が既に (cg,gg)/(gg,cg)->cg-mesh3d を宣言しているので、こちらが (gg,cg)->gg を
	 *      足すと **同じ入力対を 2 モジュールが主張**して priority 次第になる (disjoint 原則)。
	 *   ② 厳密 (cg) と double (gg) を混ぜた結果を gg で受けるのは**表現力の高→低への落下**で、
	 *      それは cast だけがやってよい (モジュール境界の約束 ②)。cgal が受けるのが正しい。
	 *   単独入力の op (cast / solidify) は落下先が明示されているので (cg-mesh3d) 行を持つ。 */
	{ "union",        BINMESH_IN,2, AK_CACHE, OPWIRE(ggaUnion, ggGeom, ggGeom),        1, "[" GG_TYPE ",mf-mesh3d](32)->" GG_TYPE, 1 /* ★可換 */ },
	{ "intersection", BINMESH_IN,2, AK_CACHE, OPWIRE(ggaIntersection, ggGeom, ggGeom), 1, "[" GG_TYPE ",mf-mesh3d](32)->" GG_TYPE, 1 /* ★可換 */ },
	{ "difference",   BINMESH_IN,2, AK_CACHE, OPWIRE(ggaDifference, ggGeom, ggGeom),   1, "[" GG_TYPE ",mf-mesh3d](32)->" GG_TYPE },
	/* ★ #3445: 自己交差した境界からのソリッド再構成。geogram は arrangement + radial sort で
	 * 内外を決め直せる = cgal (素通り) / manifold (同じ誤値) / nef (受け取れない) が持たない能力。
	 * nef の solidify と同じく **明示 op** (既定の変換経路には置かない)。 */
	{ "solidify",     MEASURE_IN,1, AK_CACHE, OPWIRE(ggaSolidify, ggGeom),     0, "(" GG_TYPE ")->" GG_TYPE ";(mf-mesh3d)->" GG_TYPE },
	/* ★ 2026-08-25: solidify から **(cg-mesh3d) 行を削除**した。cg(厳密な有理数) → gg(double) は
	 *   **表現力の高→低の落下**で、約束② (降格は cast のみ) 違反。落としたいなら利用者が
	 *   cast("gg-mesh3d", m) を書く (cast はその 1 行を持ったままでよい = 落ちることが op 名に残る)。
	 *   ★ 逆に **(mf-mesh3d) は残す** — double → double で精度クラスが変わらないうえ、4CC も
	 *   同じ "MFM3" なので変換すら起きない。nef も (mf)->nf を持つが priority で geogram が勝つ
	 *   (下の梯子を参照) = **double 入力は double のまま速い方で解かれる**。 */
	{ "volume",       MEASURE_IN,1, AK_INLINE,OPWIRE(ggaVolume, ggGeom),       0, "(" GG_TYPE ")->value" },
	{ "nverts",       MEASURE_IN,1, AK_INLINE,OPWIRE(ggaNverts, ggGeom),       0, "(" GG_TYPE ")->value" },
	{ "nfaces",       MEASURE_IN,1, AK_INLINE,OPWIRE(ggaNfaces, ggGeom),       0, "(" GG_TYPE ")->value" },
	{ "export",       EXPORT_IN, 3, AK_CACHE, OPWIRE(ggaExport, ggGeom),       0, "(" GG_TYPE ")->ref" },
	{ "cast",         CAST_IN,   2, AK_CACHE, OPWIRE(ggaCast, ggGeom),         0, "(" GG_TYPE ")->" GG_TYPE ";(mf-mesh3d)->" GG_TYPE ";(cg-mesh3d)->" GG_TYPE },
	{ "translate",    MESH1ARG_IN,2,AK_CACHE, OPWIRE(ggaTranslate, ggGeom),    0, "(" GG_TYPE ")->" GG_TYPE },
};
static const int N_OPS = (int)(sizeof(OPS) / sizeof(OPS[0]));

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ggtsAgent_(
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
#include	"pig/c++/pigOpEntry.h"
class ptsObject;
TS_END_INTERFACE

#endif


ggtsAgent_::ggtsAgent_(TS_ARGS0)
        : ptsGenericAgent_(parent)
{
    TS_CPARGS0
}

const pigOpEntry*	ggtsAgent_::agent_ops()   { return OPS; }
int			ggtsAgent_::agent_n_ops() { return N_OPS; }
const char*		ggtsAgent_::agent_name()  { return GG_MODULE_NAME; }

static sPtr<ptsAgent>
mk_ggtsAgent(sPtr<ptsObject> med)
{
	return thNEW(ggtsAgent,(med));
}

/* 自己申告記述子。
 *  - priority=6: **既定カーネルにはならない** (既定は cgal の 20)。ベンチで使うときに
 *    module("geogram.so",{priority:99}) で明示的に上げる。
 *    ★★ 2026-08-25 に 3 → 6 (**nef 5 の上**) へ上げた。理由 = `solidify` を nef と geogram の
 *      両方が実装しており、**double 入力 (mf/gg) は double のまま geogram で解くのが正しい**から
 *      (nef へ行くと double が黙って厳密へ昇格し、しかも面数比例で桁で遅い)。
 *      ★ nef 側の (mf-mesh3d)->nf-mesh3d 行は**残してある** — geogram を積まないビルドでは
 *        nef が拾い、`solidify(mf)` が routing 不能にならない (後退させない)。
 *      ⚠ nef と geogram が両方名乗る op は現在 solidify だけ。今後どちらにも実装のある op を
 *        足すときは、この梯子で geogram が勝つことを踏まえて sig を書くこと。
 *    ★ 現在の梯子 (2026-08-25): cgal 20 > manifold 10 > geogram 6 > nef 5 > pipe_proximity 4 >
 *      occt 2 > openvdb 1 > (テスト専用は負値: demo -1 / d3 -2 / d2 -3 / d4 -4 / d5 -5)。
 *      ★同点の勝敗は不定なので、同梱モジュールは互いに重複させない。
 *  - exec: PROCESS のみ。geogram はプロセス全体のグローバル初期化 (GEO::initialize) と
 *    OpenMP を使うので、in-proc (planner 内スレッド) では安全でない。
 *    ★ 2026-08-25 に上流で裏を取った (bench 調査・wiki Srava_benchmark_experiments §14):
 *      geogram issue #68「多くのルーチンがグローバル static を使う。thread_local にすれば
 *      スレッド安全性の助けになる」。そこで言う thread-safe の定義が
 *      **「geogram の関数を並行に呼べること (例: 2 つのメッシュに対する Delaunay を並列に)」**
 *      = まさに srava の op 間並列。issue #64 は GEO::initialize 自体のスレッド安全化。
 *      ⇒ **上流が未解決課題として認識している**ので、PROCESS 固定は当面の正解。
 *    ⚠ op **内** 並列は別で、geogram は持っている (cgal / nef より多くのスレッドを立てる)。
 *      「op 内は並列だが op 間は不可」は OCCT / CGAL とも共通の構図 (wiki §14)。
 */
extern const pigModuleType geogram_provides[];
extern const srava_module_descriptor ggtsAgent_descriptor;
extern const srava_module_descriptor ggtsAgent_descriptor = {
	.abi_version   = SRAVA_MODULE_ABI,
	.name          = GG_MODULE_NAME,
	.priority      = 6,
	.make_agent    = &mk_ggtsAgent,
	.exec_caps     = (unsigned)EXEC_PROCESS,
	.exec_default  = EXEC_PROCESS,
	.ops           = OPS,
	.n_ops         = N_OPS,
	.import_exts   = "",   /* なし (import は他カーネルで入れて cast する) */
	.export_exts   = "off,stl,obj,ply",   /* (GEO::mesh_save の拡張子ディスパッチ) */
	.provides      = geogram_provides,   /* 階層 × 型名 × 4CC (ABI v16) */
	.hash_salt     = GG_SALT,   /* キャッシュキー弁別 */
	.initialize    = 0,   /* 無し */
	/* ★ v10 (#3441): module("geogram.so",{threads:N}) で GEO::Process::set_max_threads を
	 * 呼ぶ opt-in の口。既定 (threads 未指定) は従来どおり GEO::initialize() 任せ (nproc)。 */
	.configure     = &ggMesh::configure,
};
