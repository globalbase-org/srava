/*
 * nftsAgent — Nef モジュールの実行体 (ptsGenericAgent 派生・#3433 P1)。
 *   状態機械は共通基底 ptsGenericAgent に集約済み。この派生は **OPS[] 表と記述子だけ**を持つ。
 *   mfatsAgent / cgatsAgent と同一構造。
 *
 * ★このモジュールの要件 (#3433): **Nef 型を維持したまま op 連鎖する**こと。
 *   ブール op は nfNefMesh のまま結果を返し、境界表現へ戻すのは volume / export / cache 書き出しだけ。
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
#include	"nf/c++/nfMesh.h"   /* NF_TYPE / NF_TAG / NF_MODULE_NAME */
#include	"nf/c++/nfaBox.h"
#include	"nf/c++/nfaSphere.h"
/* ★ #3474: 基本立体をカーネル間で統一 */
#include	"nf/c++/nfaPyramid.h"
#include	"nf/c++/nfaCylinder.h"
#include	"nf/c++/nfaCone.h"
#include	"nf/c++/nfaTorus.h"
#include	"nf/c++/nfaTetrahedron.h"
#include	"nf/c++/nfaPrism.h"
#include	"nf/c++/nfaIcosphere.h"
#include	"nf/c++/nfaImport.h"
#include	"nf/c++/nfaEmpty3D.h"
#include	"nf/c++/nfaTube.h"
#include	"nf/c++/nfaUnion.h"
#include	"nf/c++/nfaIntersection.h"
#include	"nf/c++/nfaDifference.h"
#include	"nf/c++/nfaComplement.h"
#include	"nf/c++/nfaMinkowski.h"
#include	"nf/c++/nfaHull.h"
#include	"nf/c++/nfaOffset.h"
#include	"nf/c++/nfaConvexDecomposition.h"
#include	"nf/c++/nfaNparts.h"
#include	"nf/c++/nfaPart.h"
#include	"nf/c++/nfaUnify.h"
#include	"nf/c++/nfaSolidify.h"
#include	"nf/c++/nfaVolume.h"
#include	"nf/c++/nfaBbox.h"
#include	"nf/c++/nfaCentroid.h"
#include	"nf/c++/nfaArea.h"
#include	"nf/c++/nfaValid.h"
#include	"nf/c++/nfaNverts.h"
#include	"nf/c++/nfaNfaces.h"
#include	"nf/c++/nfaExport.h"
#include	"nf/c++/nfaCast.h"
#include	"nf/c++/nfaTranslate.h"
#include	"nf/c++/nfaRotate.h"
#include	"nf/c++/nfaScale.h"
#include	"nf/c++/nfaMirror.h"
#include	"nf/c++/nfaTransform.h"
#include	"ts2/c++/stdString.h"
#include	"pig/c++/pigOpMatch.h"   /* ★ #3554 最後の段 2/5: cast の共通マッチ述語 */
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
static const pigArgKind ROTATE_IN[]  = { AK_CACHE, AK_INLINE, AK_INLINE };  /* rotate(m,axis,deg) */
static const pigArgKind OFFSET_IN[]  = { AK_CACHE, AK_INLINE, AK_INLINE };  /* offset(m, d, subdiv) */

static const pigOpEntry OPS[] = {
	{ "box",          SHAPE3_IN, 3, AK_CACHE, OPWIRE(nfaBox),          0, "->" NF_TYPE },
	{ "boxa",         SHAPE1_IN, 1, AK_CACHE, OPWIRE(nfaBox),          0, "->" NF_TYPE },
	/* ★ #3474: 基本立体はカーネル差が出ないので全カーネルに置く (common/solids.h)。 */
	{ "pyramid",       SHAPE3_IN, 3, AK_CACHE, OPWIRE(nfaPyramid), 0, "->" NF_TYPE },  /* pyramid(n,h,r) */
	{ "cylinder",      SHAPE3_IN, 3, AK_CACHE, OPWIRE(nfaCylinder), 0, "->" NF_TYPE, 0, 0, 2 },  /* cylinder(r,h,seg) */  /* ★ #3530: nreq=2 → segs は省略可 (occt と揃えた。省略 と 0 は同じ「未指定」) */
	{ "cone",          SHAPE3_IN, 3, AK_CACHE, OPWIRE(nfaCone), 0, "->" NF_TYPE, 0, 0, 2 },  /* cone(r,h,seg) */  /* ★ #3530: nreq=2 → segs は省略可 (occt と揃えた。省略 と 0 は同じ「未指定」) */
	{ "torus",         SHAPE3_IN, 3, AK_CACHE, OPWIRE(nfaTorus), 0, "->" NF_TYPE, 0, 0, 2 },  /* torus(R,r,seg) */  /* ★ #3530: nreq=2 → segs は省略可 (occt と揃えた。省略 と 0 は同じ「未指定」) */
	{ "tetrahedron",   SHAPE1_IN, 1, AK_CACHE, OPWIRE(nfaTetrahedron), 0, "->" NF_TYPE },  /* tetrahedron(r) */
	/* ★ #3474 続き (2026-09-05): prism / icosphere / import の歯抜けも埋める。 */
	{ "prism",         SHAPE3_IN, 3, AK_CACHE, OPWIRE(nfaPrism), 0, "->" NF_TYPE },  /* prism(n,h,r) */
	{ "icosphere",     SHAPE2_IN, 2, AK_CACHE, OPWIRE(nfaIcosphere), 0, "->" NF_TYPE, 0, 0, 1 },  /* icosphere(r,subdiv) */  /* ★ nreq=1: 以降は省略可 (既定は op が入れる) */
	/* ★ #3554 最後の段 3/5 (2026-09-19): import の行は共通述語 @pig_match_import_ext@ が選ぶ
	 *   (拡張子が産む型 = @d->import_exts@ の型付き CSV が、**この行の sig の出力型**か)。
	 *   ⚠ 出力型が拡張子で決まるので、*sig だけでは行が決まらない* のが import の特徴。
	 *   ★ このカーネルは import の出力型が 1 つなので **行を分ける必要は無い**。 */
	{ "import",        SHAPE1_IN, 1, AK_CACHE, OPWIRE(nfaImport), 0, "->" NF_TYPE, 0, 0, 0, &pig_match_import_ext },  /* import(path): STL/OFF */
	{ "empty3d",      0,         0, AK_CACHE, OPWIRE(nfaEmpty3D), 0, "->" NF_TYPE },  /* 空集合(3D)。{} は中立元なので別物 */
	/* ★ nreq=1: segs は省略可 (既定 32 は op が入れる)。掃引は common/tube.h。 */
	{ "tube_ruled",         SHAPE2_IN, 2, AK_CACHE, OPWIRE(nfaTube), 0, "->" NF_TYPE, 0, 0, 1 },  /* tube(path[,segs]): 3D のみ */
	{ "sphere",       SHAPE2_IN, 2, AK_CACHE, OPWIRE(nfaSphere),       0, "->" NF_TYPE, 0, 0, 1 },  /* ★ nreq=1: 以降は省略可 (既定は op が入れる) */
	/* ブール: 自型どうし + **混成** (片側が cg / mf / gg)。混成は cache reader の昇格読みで成立する
	 * (nf-cg-upgrade: MESH → nf / nf-mf-upgrade: MFM3 → nf)。
	 * ★ gg-mesh3d は 4CC が MFM3 (manifold と共有する形式) なので **nf-mf-upgrade がそのまま読む**
	 *   = codec は不要で sig の宣言だけで開通する (2026-08-25 追加)。nef は geogram より先に
	 *   書かれたので gg 型が存在せず、追随が漏れていた。
	 * ★all-foreign ((cg,cg)) は書かない — cgal 自身が同じ op を持つので曖昧になる (disjoint 原則)。 */
	{ "union",        BINMESH_IN,2, AK_CACHE, OPWIRE(nfaUnion, NF_MESH, NF_MESH),        1, "[" NF_TYPE ",cg-mesh3d,mf-mesh3d,gg-mesh3d,ch-mesh3d](*)->" NF_TYPE, 1 /* ★可換 */ },
	{ "intersection", BINMESH_IN,2, AK_CACHE, OPWIRE(nfaIntersection, NF_MESH, NF_MESH), 1, "[" NF_TYPE ",cg-mesh3d,mf-mesh3d,gg-mesh3d,ch-mesh3d](*)->" NF_TYPE, 1 /* ★可換 */ },
	{ "difference",   BINMESH_IN,2, AK_CACHE, OPWIRE(nfaDifference, NF_MESH, NF_MESH),   1, "[" NF_TYPE ",cg-mesh3d,mf-mesh3d,gg-mesh3d,ch-mesh3d](*)->" NF_TYPE },
	/* ★Nef 固有: 補集合。cgal(corefinement)/manifold には無い op = 多カーネルの質的な差。
	 * nef しか持たない op なので all-foreign (cg-mesh3d) を書いてよい (曖昧にならない)。 */
	{ "complement",   MEASURE_IN,1, AK_CACHE, OPWIRE(nfaComplement, NF_MESH),   0, "(" NF_TYPE ")->" NF_TYPE ";(cg-mesh3d)->" NF_TYPE ";(mf-mesh3d)->" NF_TYPE ";(gg-mesh3d)->" NF_TYPE },
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
	{ "minkowski",    BINMESH_IN,2, AK_CACHE, OPWIRE(nfaMinkowski, NF_MESH, NF_MESH),    0, NF_MINK_SIG },
	/* ★ #3511: 凸包。**1 個でも受ける**ので nin=1 + variadic=1。頂点しか見ないので
	 *   n 項が 1 回の convex_hull_3 で済む = `(*)` と書ける。可換 = 1。
	 *   他カーネルのメッシュも受ける (nef が凸包を持つこと自体は nef 固有ではないが、
	 *   nf-mesh3d を返す口がここにしか無いので all-foreign を書いてよい)。 */
	/* ★★ #3528: **"(*!)" = 分解禁止**。hull は入力から **頂点しか使わない**ので、木に分解すると
	 *   「点 → メッシュを作って cache へ書き、読み戻して面を捨てて頂点に戻す」を段ごとに繰り返す
	 *   = 作ったものを次の段で捨てる。⚠⚠ それ以前に **落ちうる** — 退化検査は部分集合について
	 *   閉じていないので、全体が立体でも群が同一平面になると「立体にならない」で明示エラーになる。
	 *   ⇒ 主型による振り分け (fold 形) は保ったまま、分解だけを止める。 */
	{ "hull",         MEASURE_IN,1, AK_CACHE, OPWIRE(nfaHull, NF_MESH),        1, "(" NF_TYPE ")->" NF_TYPE ";[" NF_TYPE ",cg-mesh3d,mf-mesh3d,gg-mesh3d](*!)->" NF_TYPE, 1 /* ★可換 */ },
	/* ★3D offset (#3440 の 2): cgal.so から**移設**した。中身は Minkowski 和 (Nef + 凸分解) なので
	 * cgal.so に置くのは約束①違反だった。**2D offset は cgal.so に残る** (straight skeleton・Nef 無関係)
	 * ので、ここで申告するのは **3D の型だけ**。minkowski と同じく nef 固有 = all-foreign を書いてよい。
	 * 結果は nf 型。cg で続けたいときは利用者が cast("cg-mesh3d", ...) を書く (約束②)。 */
	{ "offset",       OFFSET_IN, 3, AK_CACHE, OPWIRE(nfaOffset, NF_MESH),       0, "(" NF_TYPE ")->" NF_TYPE ";(cg-mesh3d)->" NF_TYPE ";(mf-mesh3d)->" NF_TYPE ";(gg-mesh3d)->" NF_TYPE, 0, 0, 2 },  /* ★ nreq=2: 以降は省略可 (既定は op が入れる) */
	/* ★Nef 固有: 凸分解 (#3441)。凸片は 1 つの mesh の中に別々の連結成分として入る
	 * (mesh の配列を返せないため。返し方の検討は #3441 に記録)。個数だけなら convex_pieces。 */
	{ "convex_decomposition", MEASURE_IN,1, AK_CACHE, OPWIRE(nfaConvexDecomposition, NF_MESH), 0, "(" NF_TYPE ")->" NF_TYPE ";(cg-mesh3d)->" NF_TYPE ";(mf-mesh3d)->" NF_TYPE ";(gg-mesh3d)->" NF_TYPE },
	/* ★塊の取り出し (#3441 追補・ひさ提案): mesh の配列を返せないので **数 + n 番目** の 2 本にする。
	 * 塊 = marked volume。凸分解の結果に使うと片が 1 つずつ得られる。 */
	{ "nparts",               MEASURE_IN,1, AK_INLINE,OPWIRE(nfaNparts, NF_MESH),              0, "(" NF_TYPE ")->value;(cg-mesh3d)->value;(mf-mesh3d)->value;(gg-mesh3d)->value" },
	{ "part",                 MESH1ARG_IN,2,AK_CACHE, OPWIRE(nfaPart, NF_MESH),                0, "(" NF_TYPE ")->" NF_TYPE ";(cg-mesh3d)->" NF_TYPE ";(mf-mesh3d)->" NF_TYPE ";(gg-mesh3d)->" NF_TYPE },
	/* ★Nef 固有: 内壁除去 (#3442)。repair とは別物で **体積が変わる**。自動ではやらない。 */
	{ "unify",                MEASURE_IN,1, AK_CACHE, OPWIRE(nfaUnify, NF_MESH),               0, "(" NF_TYPE ")->" NF_TYPE ";(cg-mesh3d)->" NF_TYPE ";(mf-mesh3d)->" NF_TYPE ";(gg-mesh3d)->" NF_TYPE },
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
	{ "solidify",     MEASURE_IN,1, AK_CACHE, OPWIRE(nfaSolidify, NF_MESH),     0, "(" NF_TYPE ")->" NF_TYPE ";(cg-mesh3d)->" NF_TYPE ";(mf-mesh3d)->" NF_TYPE },
	/* ★ #3443: 境界へ落としたときの頂点数 / 面数。 */
	{ "nverts",       MEASURE_IN,1, AK_INLINE,OPWIRE(nfaNverts, NF_MESH), 0, "(" NF_TYPE ")->value" },
	{ "nfaces",       MEASURE_IN,1, AK_INLINE,OPWIRE(nfaNfaces, NF_MESH), 0, "(" NF_TYPE ")->value" },
	{ "volume",       MEASURE_IN,1, AK_INLINE,OPWIRE(nfaVolume, NF_MESH),       0, "(" NF_TYPE ")->value" },
	/* ★ #3487: 値の素性を訊く op。どれも →value で 2D 型を要さない。無いと確認のためだけに
	 * 別カーネルへ cast させることになり、**cast が通らない値では確認手段そのものが消える**
	 * (#3478 の非有界・非多様体)。中身は common/meshprops.h (valid の共通定義もそこ)。 */
	{ "bbox",         MEASURE_IN,1, AK_INLINE,OPWIRE(nfaBbox, NF_MESH),     0, "(" NF_TYPE ")->value" },
	{ "centroid",     MEASURE_IN,1, AK_INLINE,OPWIRE(nfaCentroid, NF_MESH), 0, "(" NF_TYPE ")->value" },
	{ "area",         MEASURE_IN,1, AK_INLINE,OPWIRE(nfaArea, NF_MESH),     0, "(" NF_TYPE ")->value" },
	{ "valid",        MEASURE_IN,1, AK_INLINE,OPWIRE(nfaValid, NF_MESH),    0, "(" NF_TYPE ")->value" },
	/* ★★ #3554 最後の段 4/5 (2026-09-19): export の行は共通述語 @pig_match_export_ext@ が選ぶ
	 *   (= 第 1 引数の拡張子を **d->export_exts** が書けるか)。出力は常に @ref@ なので
	 *   **行を分ける必要は無い** (cast / import と違うのはここ)。
	 *   ⚠⚠ 同時に **規約① (自型優先) を撤去**した — 「入力型の home カーネルが書けるならそこ」
	 *     という routing の特例で、sig でも記述子でもない *3 つめの規則* だった。
	 *     ⇒ いまは priority × sig × 拡張子 の普通の決着。 */
	{ "export",       EXPORT_IN, 3, AK_CACHE, OPWIRE(nfaExport, NF_MESH),       0, "(" NF_TYPE ")->ref", 0, 0, 0, &pig_match_export_ext },
	/* cast は sig の**出力型**で routing される。cg→nf の実変換は nf-cg-upgrade codec が担う。 */
	{ "cast",         CAST_IN,   2, AK_CACHE, OPWIRE(nfaCast, NF_MESH),         0, "(" NF_TYPE ")->" NF_TYPE ";(cg-mesh3d)->" NF_TYPE ";(mf-mesh3d)->" NF_TYPE ";(gg-mesh3d)->" NF_TYPE ";(ch-mesh3d)->" NF_TYPE
	                                                          /* ★ #3527: gu-mesh3d も MFM3 ⇒ NF_MESH::create_for_meta が読める (2D は nef に型が無い) */
	                                                          ";(gu-mesh3d)->" NF_TYPE,  /* ★ #3464: cherchi も MFM3 を名乗る = mf と同じ経路 */
	                                                          /* ★ #3554 最後の段 2/5: cast の行は
	                                                           *   共通述語 @pig_match_cast_target@ が選ぶ (目標型 = この行の sig の出力型か)。
	                                                           *   ⚠ このカーネルの cast は出力型が 1 つなので **行を分ける必要は無い**
	                                                           *     (分ける理由は「1 行 1 出力型」という規約の方であって、名前ではない)。 */
	                                                          0, 0, 0, &pig_match_cast_target },
	{ "translate",    MESH1ARG_IN,2,AK_CACHE, OPWIRE(nfaTranslate, NF_MESH),    0, "(" NF_TYPE ")->" NF_TYPE },
	/* ★ #3486: アフィン変換 4 op。translate だけあって残り 3 本が無いと、式の途中で
	 * **カーネルが裏返る** (rotate を書いた瞬間に cgal/manifold へ落ちる)。4 本とも
	 * 3D→3D で 2D 型を要さないので、2D 型を持たないこのカーネルでも置ける。
	 * 引数の解釈と行列作りは common/affine.h・適用は nfNefMesh::apply_affine。 */
	{ "rotate",       ROTATE_IN,  3,AK_CACHE, OPWIRE(nfaRotate, NF_MESH),       0, "(" NF_TYPE ")->" NF_TYPE },
	{ "scale",        MESH1ARG_IN,2,AK_CACHE, OPWIRE(nfaScale, NF_MESH),        0, "(" NF_TYPE ")->" NF_TYPE },
	{ "mirror",       MESH1ARG_IN,2,AK_CACHE, OPWIRE(nfaMirror, NF_MESH),       0, "(" NF_TYPE ")->" NF_TYPE },
	{ "transform",    MESH1ARG_IN,2,AK_CACHE, OPWIRE(nfaTransform, NF_MESH),    0, "(" NF_TYPE ")->" NF_TYPE },
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
	/* ★ #3474 続き: import を持つので **拡張子を申告する** (未申告だとロード時に拒否)。
	 *   読み手は common/meshio.h。対応形式は STL / OFF だけ。 */
	.import_exts   = "stl:" NF_TYPE ",off:" NF_TYPE,   /* なし (import は cgal/manifold 経由で入れて cast する) */
	.export_exts   = "off,stl,ply,obj",   /* */
	.provides      = nef_provides,   /* 階層 × 型名 × 4CC (ABI v16) */
	/* ★ v18 (#3466): このモジュールが出す結果の版。**計算を変えたら手で上げる**。 */
	.cache_version = 6,   /* ★ v6 (#3525・2026-09-15): **volume を落とすところで CGAL::exact() を通す**。
	                       *   to_double は Lazy_exact_nt の区間近似で正しく丸められず、値が 1〜2 ulp
	                       *   動いていた。⚠ cgal 側と対で直してある (cgMesh3D::op_volume)。
	                       * ★★ v5 (#3507・2026-09-10): SNC の framing を **ブロック分割**へ変更
	                       *   ([u32 blocklen][block]…[u32 0])。全長の前置をやめたので書き手が
	                       *   全文を作らなくてよくなり、4 GiB の上限も消えた。
	                       *   ★★ v4 (#3499・2026-09-07): **nef_snc が付録の境界を書くのをやめた**
	                       *   (常に NF_FORM_SNC)。他カーネルへ渡す cast は橋モジュール
	                       *   nef_cg.so / nef_mf.so が持つ。hybrid の書き方は変えていないが、
	                       *   版番号は 2 変種で共有している (同一記述子) ので両方上がる。
	                       *   ★ v3 (2026-09-06): 境界を併記する条件を is_simple() から
	                       *   「to_mesh が成功するか」へ広げた (非 2-多様体でも境界は取れる)。
	                       *   ★ #3478: v2 = nef_snc が SNC の後ろに厳密境界を併記 */
	/* ★ v7 (#3419): op 内並列の方式と σ (docs/srava_load_control_design.md §5.5/§5.6)。
	 *   CGAL Nef ベース。T1-d 実測で TBB 依存ゼロ */
	.initialize    = 0,   /* 無し */
	.configure     = 0,   /* ★ v10 (#3441): opts フックは未使用(このモジュールは module() の
	                       *   opts を消費しない) */
};
