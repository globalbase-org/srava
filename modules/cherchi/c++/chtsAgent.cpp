/*
 * chtsAgent — cherchi モジュールの実行体 (ptsGenericAgent 派生・#3438 P6)。
 *   状態機械は共通基底 ptsGenericAgent に集約済み。この派生は **OPS[] 表と記述子だけ**を持つ。
 *   ggtsAgent / mfatsAgent / nftsAgent と同一構造。
 *
 * ★ このモジュールの位置づけ (#3438):
 *   ① geogram (P3) と並べて「**厳密高速勢**」の 2 点比較にする。厳密の実現方法が違う —
 *      geogram = 交点を厳密に構成 / cherchi = **indirect predicates** (交点を「どの 3 平面の
 *      交わりか」という間接表現のまま厳密述語を評価し、有理数展開を避ける)。
 *   ② ★★ **素で n 項** (#3436 P4 の対照相手)。booleanPipeline が三角形ごとの label を取り、
 *      arrangement を 1 回だけ作って分類する。二項はその 2 ラベル版でしかない。
 *      ⇒ 「n 項 (arrangement 1 回) vs 二分木 (中間キャッシュ + op 間並列)」を **geogram と
 *      cherchi の 2 カーネルで**測れる。
 *   ⚠ solidify (#3445) は**持たない** — 分類が label 単位なので、自己交差した 1 枚の
 *      メッシュには効かない (実測。理由は chMesh.h 冒頭)。
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
#include	"ch/c++/chMesh.h"
#include	"ch/c++/chaBox.h"
#include	"ch/c++/chaSphere.h"
#include	"ch/c++/chaUnion.h"
#include	"ch/c++/chaIntersection.h"
#include	"ch/c++/chaDifference.h"
#include	"ch/c++/chaVolume.h"
#include	"ch/c++/chaNverts.h"
#include	"ch/c++/chaNfaces.h"
#include	"ch/c++/chaExport.h"
#include	"ch/c++/chaCast.h"
#include	"ch/c++/chaTranslate.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/chtsAgent_.h"

#include	<string.h>

CLASS_TINYSTATE(ch/c++/chtsAgent,pig/c++/ptsGenericAgent)

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
	{ "box",          SHAPE3_IN, 3, AK_CACHE, OPWIRE(chaBox),          0, "->" CH_TYPE },
	{ "boxa",         SHAPE1_IN, 1, AK_CACHE, OPWIRE(chaBox),          0, "->" CH_TYPE },
	{ "sphere",       SHAPE2_IN, 2, AK_CACHE, OPWIRE(chaSphere),       0, "->" CH_TYPE },
	/* ブール: 自型どうし + 混成 (片側が mf の raw double mesh)。混成は cache reader が
	 * MFM3 (= 同じ wire 形式) をそのまま読んで成立する。
	 * ★all-foreign ((mf,mf)) は書かない — manifold 自身が同じ op を持つので曖昧になる (disjoint 原則)。
	 * ★gg-mesh3d (geogram) との混成も書かない。どちらも double の三角形メッシュなので
	 *   「どちらのカーネルで解くか」が priority 次第になってしまう (同じ理由で geogram 側も
	 *   ch を書かない)。混ぜたい利用者は cast を書く = **どちらで解くかが式に残る**。
	 * ★(32) は IRMB の label が bitset<32> であることそのもの (chMesh::CH_MAX_OPERANDS)。 */
	{ "union",        BINMESH_IN,2, AK_CACHE, OPWIRE(chaUnion, chGeom, chGeom),        1, "[" CH_TYPE ",mf-mesh3d](32)->" CH_TYPE, 1 /* ★可換 */ },
	{ "intersection", BINMESH_IN,2, AK_CACHE, OPWIRE(chaIntersection, chGeom, chGeom), 1, "[" CH_TYPE ",mf-mesh3d](32)->" CH_TYPE, 1 /* ★可換 */ },
	{ "difference",   BINMESH_IN,2, AK_CACHE, OPWIRE(chaDifference, chGeom, chGeom),   1, "[" CH_TYPE ",mf-mesh3d](32)->" CH_TYPE },
	{ "volume",       MEASURE_IN,1, AK_INLINE,OPWIRE(chaVolume, chGeom),       0, "(" CH_TYPE ")->value" },
	{ "nverts",       MEASURE_IN,1, AK_INLINE,OPWIRE(chaNverts, chGeom),       0, "(" CH_TYPE ")->value" },
	{ "nfaces",       MEASURE_IN,1, AK_INLINE,OPWIRE(chaNfaces, chGeom),       0, "(" CH_TYPE ")->value" },
	{ "export",       EXPORT_IN, 3, AK_CACHE, OPWIRE(chaExport, chGeom),       0, "(" CH_TYPE ")->ref" },
	{ "cast",         CAST_IN,   2, AK_CACHE, OPWIRE(chaCast, chGeom),         0, "(" CH_TYPE ")->" CH_TYPE ";(mf-mesh3d)->" CH_TYPE ";(cg-mesh3d)->" CH_TYPE },
	{ "translate",    MESH1ARG_IN,2,AK_CACHE, OPWIRE(chaTranslate, chGeom),    0, "(" CH_TYPE ")->" CH_TYPE },
};
static const int N_OPS = (int)(sizeof(OPS) / sizeof(OPS[0]));

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	chtsAgent_(
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


chtsAgent_::chtsAgent_(TS_ARGS0)
        : ptsGenericAgent_(parent)
{
    TS_CPARGS0
}

const pigOpEntry*	chtsAgent_::agent_ops()   { return OPS; }
int			chtsAgent_::agent_n_ops() { return N_OPS; }
const char*		chtsAgent_::agent_name()  { return CH_MODULE_NAME; }

static sPtr<ptsAgent>
mk_chtsAgent(sPtr<ptsObject> med)
{
	return thNEW(chtsAgent,(med));
}

/* 自己申告記述子。
 *  - priority=3: **既定カーネルにはならない** (既定は cgal の 20)。ベンチで使うときに
 *    module("cherchi.so",{priority:99}) で明示的に上げる。
 *    ★ 梯子 (2026-08-26): cgal 20 > manifold 10 > geogram 6 > nef 5 > pipe_proximity 4 >
 *      **cherchi 3** > occt 2 > openvdb 1 > (テスト専用は負値)。
 *      ★ **既存の梯子を動かさない位置**に置いた: cherchi が実装する op は今のところ
 *        すべて他カーネルも持っているので、上に置くと「今まで geogram/nef が解いていた式が
 *        黙って cherchi へ移る」= 新モジュールを積んだだけで既存の routing が変わってしまう。
 *        ⚠ 同点は勝敗が不定なので、同梱モジュールは値を重複させない。
 *  - exec: PROCESS のみ。IRMB は初期化 (initFPU) で **FPU の丸めモードをプロセス単位で
 *    触る** (述語が IEEE 754 の丸め方向に依存する) ので、planner 内スレッドで走らせると
 *    他モジュールの浮動小数演算に影響しうる。geogram / nef と同じ扱い。
 *    ⚠ op **内** 並列は別で、IRMB は TBB で並列化されている (TBB_PARALLEL=1)。
 */
extern const pigModuleType cherchi_provides[];
extern const srava_module_descriptor chtsAgent_descriptor;
extern const srava_module_descriptor chtsAgent_descriptor = {
	.abi_version   = SRAVA_MODULE_ABI,
	.name          = CH_MODULE_NAME,
	.priority      = 3,
	.make_agent    = &mk_chtsAgent,
	.exec_caps     = (unsigned)EXEC_PROCESS,
	.exec_default  = EXEC_PROCESS,
	.ops           = OPS,
	.n_ops         = N_OPS,
	.import_exts   = "",   /* なし (import は他カーネルで入れて cast する) */
	.export_exts   = "off,stl,obj",   /* chMesh::write_to の拡張子ディスパッチと一致させること */
	.provides      = cherchi_provides,   /* 階層 × 型名 × 4CC (ABI v16) */
	.hash_salt     = CH_SALT,   /* キャッシュキー弁別 */
	.initialize    = 0,   /* 無し (initFPU は booleanPipeline が自分で呼ぶ) */
	.configure     = 0,   /* threads の口はまだ持たない (IRMB は TBB 任せ) */
};
