/*
 * nftsAgent — Nef モジュールの実行体 (ptsGenericAgent 派生・#3433 P1)。
 *   状態機械は共通基底 ptsGenericAgent に集約済み。この派生は **OPS[] 表と記述子だけ**を持つ。
 *   mfatsAgent / cgatsAgent と同一構造。
 *
 * ★このモジュールの要件 (#3433): **Nef 型を維持したまま op 連鎖する**こと。
 *   ブール op は nfMesh のまま結果を返し、境界表現へ戻すのは volume / export / cache 書き出しだけ。
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
#include	"nf/c++/nfMesh.h"   /* NF_TYPE / NF_TAG / NF_MODULE_NAME / NF_SALT */
#include	"nf/c++/nfaBox.h"
#include	"nf/c++/nfaSphere.h"
#include	"nf/c++/nfaUnion.h"
#include	"nf/c++/nfaIntersection.h"
#include	"nf/c++/nfaDifference.h"
#include	"nf/c++/nfaComplement.h"
#include	"nf/c++/nfaMinkowski.h"
#include	"nf/c++/nfaOffset.h"
#include	"nf/c++/nfaConvexDecomposition.h"
#include	"nf/c++/nfaNparts.h"
#include	"nf/c++/nfaPart.h"
#include	"nf/c++/nfaUnify.h"
#include	"nf/c++/nfaSolidify.h"
#include	"nf/c++/nfaVolume.h"
#include	"nf/c++/nfaNverts.h"
#include	"nf/c++/nfaNfaces.h"
#include	"nf/c++/nfaExport.h"
#include	"nf/c++/nfaCast.h"
#include	"nf/c++/nfaTranslate.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/nftsAgent_.h"

#include	<string.h>

CLASS_TINYSTATE(nf/c++/nftsAgent,pig/c++/ptsGenericAgent)

/* ---- ディスパッチテーブル ---- */

static const pigArgKind SHAPE3_IN[]  = { AK_INLINE, AK_INLINE, AK_INLINE };  /* box(w,h,d) */
static const pigArgKind SHAPE2_IN[]  = { AK_INLINE, AK_INLINE };             /* sphere(r,seg) */
static const pigArgKind SHAPE1_IN[]  = { AK_INLINE };                        /* boxa([w,h,d]) */
static const pigArgKind EXPORT_IN[]  = { AK_INLINE, AK_CACHE, AK_INLINE };   /* export(path, mesh, unit) */
static const pigArgKind BINMESH_IN[] = { AK_CACHE, AK_CACHE };               /* 2 mesh 入力 */
static const pigArgKind CAST_IN[]    = { AK_INLINE, AK_CACHE };              /* cast(type, mesh) */
static const pigArgKind MEASURE_IN[] = { AK_CACHE };                         /* mesh 1 個入力 */
static const pigArgKind MESH1ARG_IN[]= { AK_CACHE, AK_INLINE };              /* translate(m,[x,y,z]) */
static const pigArgKind OFFSET_IN[]  = { AK_CACHE, AK_INLINE, AK_INLINE };  /* offset(m, d, subdiv) */

static const pigOpEntry OPS[] = {
	{ "box",          SHAPE3_IN, 3, AK_CACHE, OPWIRE(nfaBox),          0, "->" NF_TYPE },
	{ "boxa",         SHAPE1_IN, 1, AK_CACHE, OPWIRE(nfaBox),          0, "->" NF_TYPE },
	{ "sphere",       SHAPE2_IN, 2, AK_CACHE, OPWIRE(nfaSphere),       0, "->" NF_TYPE },
	/* ブール: 自型どうし + **混成** (片側が cg / mf / gg)。混成は cache reader の昇格読みで成立する
	 * (nf-cg-upgrade: MESH → nf / nf-mf-upgrade: MFM3 → nf)。
	 * ★ gg-mesh3d は 4CC が MFM3 (manifold と共有する形式) なので **nf-mf-upgrade がそのまま読む**
	 *   = codec は不要で sig の宣言だけで開通する (2026-08-25 追加)。nef は geogram より先に
	 *   書かれたので gg 型が存在せず、追随が漏れていた。
	 * ★all-foreign ((cg,cg)) は書かない — cgal 自身が同じ op を持つので曖昧になる (disjoint 原則)。 */
	{ "union",        BINMESH_IN,2, AK_CACHE, OPWIRE(nfaUnion, nfGeom, nfGeom),        1, "[" NF_TYPE ",cg-mesh3d,mf-mesh3d,gg-mesh3d](*)->" NF_TYPE, 1 /* ★可換 */ },
	{ "intersection", BINMESH_IN,2, AK_CACHE, OPWIRE(nfaIntersection, nfGeom, nfGeom), 1, "[" NF_TYPE ",cg-mesh3d,mf-mesh3d,gg-mesh3d](*)->" NF_TYPE, 1 /* ★可換 */ },
	{ "difference",   BINMESH_IN,2, AK_CACHE, OPWIRE(nfaDifference, nfGeom, nfGeom),   1, "[" NF_TYPE ",cg-mesh3d,mf-mesh3d,gg-mesh3d](*)->" NF_TYPE },
	/* ★Nef 固有: 補集合。cgal(corefinement)/manifold には無い op = 多カーネルの質的な差。
	 * nef しか持たない op なので all-foreign (cg-mesh3d) を書いてよい (曖昧にならない)。 */
	{ "complement",   MEASURE_IN,1, AK_CACHE, OPWIRE(nfaComplement, nfGeom),   0, "(" NF_TYPE ")->" NF_TYPE ";(cg-mesh3d)->" NF_TYPE ";(mf-mesh3d)->" NF_TYPE ";(gg-mesh3d)->" NF_TYPE },
	/* ★Nef 固有: Minkowski 和 (#3440)。offset はこの特殊形 (球との和) = こちらがプリミティブ。
	 * nef しか持たない op なので **all-foreign も書いてよい** (cgal/manifold に minkowski は無く
	 * 曖昧にならない = complement と同じ扱い)。cg/mf の mesh は昇格読み (nf-cg-upgrade /
	 * nf-mf-upgrade) で nf になり、結果は常に nf。
	 * ★入力 4 型 (自型 / cg / mf / gg) の **全 16 組**を書く (ひさ指示 2026-08-17・gg は 2026-08-25)。
	 *   混成 (mf,cg) や (cg,mf) も含む — 昇格読みは型ごとに独立なので、組を落とす理由が無い。
	 *   ⚠ 組が型数の 2 乗で増える。sig の略記法 (docs/sig_grammar_design.md) が入れば 1 行になる。
	 * ★もう一方の nef 変種の型 (nf-mesh3d ⇄ nfb-mesh3d) は**書かない**。codec は相手の 4CC も
	 *   読めるが、両変種を同時にロードすると同じ組が priority 同値 (5) で衝突して
	 *   どちらが計算するか一意でなくなる (実際に踏んだ)。混在使用は想定しない設計
	 *   (#3433: A/B は module(so,"off") で切り替える)。
	 * ★この列挙は **routing の必要条件**である (2026-08-17 に確認)。sig を "(nf,nf)" 1 本へ削ると
	 *   cg/mf 入力は routing 不能でエラーになる。以前は「その op を実装するモジュールが 1 つだけなら
	 *   そこへ直送」という op 名ベースの経路が拾っていたが、型でなく名前で振る経路は設計に無い概念
	 *   (ひさ指摘) なので撤去した (#3440)。 */
#define NF_MINK_ROW(A)	"(" A "," NF_TYPE ")->" NF_TYPE ";(" A ",cg-mesh3d)->" NF_TYPE ";(" A ",mf-mesh3d)->" NF_TYPE ";(" A ",gg-mesh3d)->" NF_TYPE
#define NF_MINK_SIG	NF_MINK_ROW(NF_TYPE) ";" NF_MINK_ROW("cg-mesh3d") ";" NF_MINK_ROW("mf-mesh3d") ";" NF_MINK_ROW("gg-mesh3d")
	{ "minkowski",    BINMESH_IN,2, AK_CACHE, OPWIRE(nfaMinkowski, nfGeom, nfGeom),    0, NF_MINK_SIG },
	/* ★3D offset (#3440 の 2): cgal.so から**移設**した。中身は Minkowski 和 (Nef + 凸分解) なので
	 * cgal.so に置くのは約束①違反だった。**2D offset は cgal.so に残る** (straight skeleton・Nef 無関係)
	 * ので、ここで申告するのは **3D の型だけ**。minkowski と同じく nef 固有 = all-foreign を書いてよい。
	 * 結果は nf 型。cg で続けたいときは利用者が cast("cg-mesh3d", ...) を書く (約束②)。 */
	{ "offset",       OFFSET_IN, 3, AK_CACHE, OPWIRE(nfaOffset, nfGeom),       0, "(" NF_TYPE ")->" NF_TYPE ";(cg-mesh3d)->" NF_TYPE ";(mf-mesh3d)->" NF_TYPE ";(gg-mesh3d)->" NF_TYPE },
	/* ★Nef 固有: 凸分解 (#3441)。凸片は 1 つの mesh の中に別々の連結成分として入る
	 * (mesh の配列を返せないため。返し方の検討は #3441 に記録)。個数だけなら convex_pieces。 */
	{ "convex_decomposition", MEASURE_IN,1, AK_CACHE, OPWIRE(nfaConvexDecomposition, nfGeom), 0, "(" NF_TYPE ")->" NF_TYPE ";(cg-mesh3d)->" NF_TYPE ";(mf-mesh3d)->" NF_TYPE ";(gg-mesh3d)->" NF_TYPE },
	/* ★塊の取り出し (#3441 追補・ひさ提案): mesh の配列を返せないので **数 + n 番目** の 2 本にする。
	 * 塊 = marked volume。凸分解の結果に使うと片が 1 つずつ得られる。 */
	{ "nparts",               MEASURE_IN,1, AK_INLINE,OPWIRE(nfaNparts, nfGeom),              0, "(" NF_TYPE ")->value;(cg-mesh3d)->value;(mf-mesh3d)->value;(gg-mesh3d)->value" },
	{ "part",                 MESH1ARG_IN,2,AK_CACHE, OPWIRE(nfaPart, nfGeom),                0, "(" NF_TYPE ")->" NF_TYPE ";(cg-mesh3d)->" NF_TYPE ";(mf-mesh3d)->" NF_TYPE ";(gg-mesh3d)->" NF_TYPE },
	/* ★Nef 固有: 内壁除去 (#3442)。repair とは別物で **体積が変わる**。自動ではやらない。 */
	{ "unify",                MEASURE_IN,1, AK_CACHE, OPWIRE(nfaUnify, nfGeom),               0, "(" NF_TYPE ")->" NF_TYPE ";(cg-mesh3d)->" NF_TYPE ";(mf-mesh3d)->" NF_TYPE ";(gg-mesh3d)->" NF_TYPE },
	/* ★ #3445: 壊れた境界 (自己交差した閉メッシュ) からソリッドを組み直す。
	 * 自己交差は Nef 構築を素通りする (面どうしの交差は検査されない) ので、壊れた形のまま
	 * nf に入っている。それを面ごとの Nef の n 項 union + 有界セルの mark で解き直す。
	 * ★重い op (面数に比例して Nef の union) なので既定経路には置かず明示的に呼ばせる。
	 * ★cg/mf 入力も受ける (他の nef op と同じ all-foreign 行)。cg→nf は**低→高の昇格**なので
	 *   約束② (高→低の落下は cast のみ) には触れない。受け取り側は sig の**出力型**から
	 *   欲しい型を作る (pgts_consumable_types) ので、cg/mf の実体は codec が nf へ昇格読みする。
	 * ★★ **solidify は nef と geogram の両方が持つ**唯一の op で、振り分けは
	 *   **精度クラスが保存される方へ** と決めた (ひさ判断 2026-08-25):
	 *     cg(厳密) / nf(厳密) → nef      厳密のまま (geogram は (cg) 行を削除した = 降格を書かない)
	 *     mf(double) / gg(double) → geogram  double のまま・面数比例の Nef より桁で速い
	 *   これは **geogram の priority を 6 (nef 5 の上) へ上げる**ことで効かせている。
	 *   ★ (mf-mesh3d)->nf の行を **消さずに残す**のがミソ: geogram を積まないビルド (既定 OFF) では
	 *     nef が拾うので `solidify(mf)` が routing 不能にならない = 後退しない。
	 *   ⚠ **(gg-mesh3d) は足さない**。gg の値は geogram を積んだときにしか存在せず、そのときは
	 *     priority で必ず geogram が勝つので、書いても一生使われない死んだ行になる。 */
	{ "solidify",     MEASURE_IN,1, AK_CACHE, OPWIRE(nfaSolidify, nfGeom),     0, "(" NF_TYPE ")->" NF_TYPE ";(cg-mesh3d)->" NF_TYPE ";(mf-mesh3d)->" NF_TYPE },
	/* ★ #3443: 境界へ落としたときの頂点数 / 面数。 */
	{ "nverts",       MEASURE_IN,1, AK_INLINE,OPWIRE(nfaNverts, nfGeom), 0, "(" NF_TYPE ")->value" },
	{ "nfaces",       MEASURE_IN,1, AK_INLINE,OPWIRE(nfaNfaces, nfGeom), 0, "(" NF_TYPE ")->value" },
	{ "volume",       MEASURE_IN,1, AK_INLINE,OPWIRE(nfaVolume, nfGeom),       0, "(" NF_TYPE ")->value" },
	{ "export",       EXPORT_IN, 3, AK_CACHE, OPWIRE(nfaExport, nfGeom),       0, "(" NF_TYPE ")->ref" },
	/* cast は sig の**出力型**で routing される。cg→nf の実変換は nf-cg-upgrade codec が担う。 */
	{ "cast",         CAST_IN,   2, AK_CACHE, OPWIRE(nfaCast, nfGeom),         0, "(" NF_TYPE ")->" NF_TYPE ";(cg-mesh3d)->" NF_TYPE ";(mf-mesh3d)->" NF_TYPE ";(gg-mesh3d)->" NF_TYPE },
	{ "translate",    MESH1ARG_IN,2,AK_CACHE, OPWIRE(nfaTranslate, nfGeom),    0, "(" NF_TYPE ")->" NF_TYPE },
};
static const int N_OPS = (int)(sizeof(OPS) / sizeof(OPS[0]));

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	nftsAgent_(
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


nftsAgent_::nftsAgent_(TS_ARGS0)
        : ptsGenericAgent_(parent)
{
    TS_CPARGS0
}

const pigOpEntry*	nftsAgent_::agent_ops()   { return OPS; }
int			nftsAgent_::agent_n_ops() { return N_OPS; }
const char*		nftsAgent_::agent_name()  { return NF_MODULE_NAME; }

static sPtr<ptsAgent>
mk_nftsAgent(sPtr<ptsObject> med)
{
	return thNEW(nftsAgent,(med));
}

/* 自己申告記述子。
 *  - priority=5: cgal(20) / manifold(10) より低く、**既定カーネルにはならない**。
 *    ベンチや Nef 固有 op を使うときに module("nef.so",{priority:99}) で明示的に上げる。
 *  - exec: cgal.so と同じく **PROCESS のみ** (EPECK/Nef の in-proc 安全性は未検証。
 *    in-proc 化は #3433 のフォローアップ)。
 */
extern const pigModuleType nef_provides[];
extern const srava_module_descriptor nftsAgent_descriptor;
extern const srava_module_descriptor nftsAgent_descriptor = {
	.abi_version   = SRAVA_MODULE_ABI,
	.name          = NF_MODULE_NAME,
	.priority      = 5,
	.make_agent    = &mk_nftsAgent,
	.exec_caps     = (unsigned)EXEC_PROCESS,
	.exec_default  = EXEC_PROCESS,
	.ops           = OPS,
	.n_ops         = N_OPS,
	.import_exts   = "",   /* なし (import は cgal/manifold 経由で入れて cast する) */
	.export_exts   = "off,stl,ply,obj",   /* */
	.provides      = nef_provides,   /* 階層 × 型名 × 4CC (ABI v16) */
	.hash_salt     = NF_SALT,   /* キャッシュキー弁別 */
	/* ★ v7 (#3419): op 内並列の方式と σ (docs/srava_load_control_design.md §5.5/§5.6)。
	 *   CGAL Nef ベース。T1-d 実測で TBB 依存ゼロ */
	.initialize    = 0,   /* 無し */
	.configure     = 0,   /* ★ v10 (#3441): opts フックは未使用(このモジュールは module() の
	                       *   opts を消費しない) */
};
