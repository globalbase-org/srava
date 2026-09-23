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
/* ★ #3474: 基本立体をカーネル間で統一 */
#include	"gg/c++/ggaPyramid.h"
#include	"gg/c++/ggaCylinder.h"
#include	"gg/c++/ggaCone.h"
#include	"gg/c++/ggaTorus.h"
#include	"gg/c++/ggaTetrahedron.h"
#include	"gg/c++/ggaPrism.h"
#include	"gg/c++/ggaIcosphere.h"
#include	"gg/c++/ggaImport.h"
#include	"gg/c++/ggaEmpty3D.h"
#include	"gg/c++/ggaTube.h"
#include	"gg/c++/ggaUnion.h"
#include	"gg/c++/ggaHull.h"
#include	"gg/c++/ggaIntersection.h"
#include	"gg/c++/ggaDifference.h"
#include	"gg/c++/ggaSolidify.h"
#include	"gg/c++/ggaVolume.h"
#include	"gg/c++/ggaDistanceAt.h"   /* ★ #3514: 点との距離 */
#include	"gg/c++/ggaEstimateNormals.h"  /* ★ #3528: 点群の法線推定 (Co3Ne) */
#include	"pt/c++/ptCloud.h"             /* ★ #3528: 点群の本体クラス (中立の libsrava_pt) */
#include	"gg/c++/ggaExport.h"
#include	"gg/c++/ggaCast.h"
#include	"gg/c++/ggaTranslate.h"
#include	"gg/c++/ggaRotate.h"
#include	"gg/c++/ggaScale.h"
#include	"gg/c++/ggaMirror.h"
#include	"gg/c++/ggaTransform.h"
#include	"ts2/c++/stdString.h"
#include	"pig/c++/pigOpMatch.h"   /* ★ #3554 最後の段 2/5: cast の共通マッチ述語 */
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
static const pigArgKind ROTATE_IN[]  = { AK_CACHE, AK_INLINE, AK_INLINE };  /* rotate(m,axis,deg) */

static const pigOpEntry OPS[] = {
	{ "box",          SHAPE3_IN, 3, AK_CACHE, OPWIRE(ggaBox),          0, "->" GG_TYPE },
	{ "boxa",         SHAPE1_IN, 1, AK_CACHE, OPWIRE(ggaBox),          0, "->" GG_TYPE },
	/* ★ #3474: 基本立体はカーネル差が出ないので全カーネルに置く (common/solids.h)。 */
	{ "pyramid",       SHAPE3_IN, 3, AK_CACHE, OPWIRE(ggaPyramid), 0, "->" GG_TYPE },  /* pyramid(n,h,r) */
	{ "cylinder",      SHAPE3_IN, 3, AK_CACHE, OPWIRE(ggaCylinder), 0, "->" GG_TYPE, 0, 0, 2 },  /* cylinder(r,h,seg) */  /* ★ #3530: nreq=2 → segs は省略可 (occt と揃えた。省略 と 0 は同じ「未指定」) */
	{ "cone",          SHAPE3_IN, 3, AK_CACHE, OPWIRE(ggaCone), 0, "->" GG_TYPE, 0, 0, 2 },  /* cone(r,h,seg) */  /* ★ #3530: nreq=2 → segs は省略可 (occt と揃えた。省略 と 0 は同じ「未指定」) */
	{ "torus",         SHAPE3_IN, 3, AK_CACHE, OPWIRE(ggaTorus), 0, "->" GG_TYPE, 0, 0, 2 },  /* torus(R,r,seg) */  /* ★ #3530: nreq=2 → segs は省略可 (occt と揃えた。省略 と 0 は同じ「未指定」) */
	{ "tetrahedron",   SHAPE1_IN, 1, AK_CACHE, OPWIRE(ggaTetrahedron), 0, "->" GG_TYPE },  /* tetrahedron(r) */
	/* ★ #3474 続き (2026-09-05): prism / icosphere / import の歯抜けも埋める。 */
	{ "prism",         SHAPE3_IN, 3, AK_CACHE, OPWIRE(ggaPrism), 0, "->" GG_TYPE },  /* prism(n,h,r) */
	{ "icosphere",     SHAPE2_IN, 2, AK_CACHE, OPWIRE(ggaIcosphere), 0, "->" GG_TYPE, 0, 0, 1 },  /* icosphere(r,subdiv) */  /* ★ nreq=1: 以降は省略可 (既定は op が入れる) */
	/* ★ #3554 最後の段 3/5 (2026-09-19): import の行は共通述語 @pig_match_import_ext@ が選ぶ
	 *   (拡張子が産む型 = @d->import_exts@ の型付き CSV が、**この行の sig の出力型**か)。
	 *   ⚠ 出力型が拡張子で決まるので、*sig だけでは行が決まらない* のが import の特徴。
	 *   ★ このカーネルは import の出力型が 1 つなので **行を分ける必要は無い**。 */
	{ "import",        SHAPE1_IN, 1, AK_CACHE, OPWIRE(ggaImport), 0, "->" GG_TYPE, 0, 0, 0, &pig_match_import_ext },  /* import(path): STL/OFF */
	{ "empty3d",      0,         0, AK_CACHE, OPWIRE(ggaEmpty3D), 0, "->" GG_TYPE },  /* 空集合(3D)。{} は中立元なので別物 */
	/* ★ nreq=1: segs は省略可 (既定 32 は op が入れる)。掃引は common/tube.h。 */
	{ "tube_ruled",         SHAPE2_IN, 2, AK_CACHE, OPWIRE(ggaTube), 0, "->" GG_TYPE, 0, 0, 1 },  /* tube(path[,segs]): 3D のみ */
	{ "sphere",       SHAPE2_IN, 2, AK_CACHE, OPWIRE(ggaSphere),       0, "->" GG_TYPE, 0, 0, 1 },  /* ★ nreq=1: 以降は省略可 (既定は op が入れる) */
	/* ブール: 自型どうし + 混成 (片側が mf の raw double mesh)。混成は cache reader の
	 * gg-mf-upgrade codec が MFM3 → gg へ昇格読みして成立する。
	 * ★all-foreign ((mf,mf)) は書かない — manifold 自身が同じ op を持つので曖昧になる (disjoint 原則)。
	 * ★cg-mesh3d との混成は **読めるようになった後も書かない** (2026-08-19)。理由は 2 つ:
	 *   ① cgal.so が既に (cg,gg)/(gg,cg)->cg-mesh3d を宣言しているので、こちらが (gg,cg)->gg を
	 *      足すと **同じ入力対を 2 モジュールが主張**して priority 次第になる (disjoint 原則)。
	 *   ② 厳密 (cg) と double (gg) を混ぜた結果を gg で受けるのは**表現力の高→低への落下**で、
	 *      それは cast だけがやってよい (モジュール境界の約束 ②)。cgal が受けるのが正しい。
	 *   単独入力の op (cast / solidify) は落下先が明示されているので (cg-mesh3d) 行を持つ。 */
	{ "union",        BINMESH_IN,2, AK_CACHE, OPWIRE(ggaUnion, ggGeom, ggGeom),        1, "[" GG_TYPE ",mf-mesh3d,ch-mesh3d](32)->" GG_TYPE, 1 /* ★可換 */ },
	{ "intersection", BINMESH_IN,2, AK_CACHE, OPWIRE(ggaIntersection, ggGeom, ggGeom), 1, "[" GG_TYPE ",mf-mesh3d,ch-mesh3d](32)->" GG_TYPE, 1 /* ★可換 */ },
	{ "difference",   BINMESH_IN,2, AK_CACHE, OPWIRE(ggaDifference, ggGeom, ggGeom),   1, "[" GG_TYPE ",mf-mesh3d,ch-mesh3d](32)->" GG_TYPE },
	/* ★ #3511: 凸包。**1 個でも受ける**ので nin=1 + variadic=1。ブールと違い頂点しか見ないので
	 *   **32 オペランドの上限 (operand_bit が 32 bit) が掛からない** ⇒ `(*)` と書ける。可換 = 1。 */
	/* ★★ #3528: **"(*!)" = 分解禁止**。hull は入力から **頂点しか使わない**ので、木に分解すると
	 *   「点 → メッシュを作って cache へ書き、読み戻して面を捨てて頂点に戻す」を段ごとに繰り返す
	 *   = 作ったものを次の段で捨てる。⚠⚠ それ以前に **落ちうる** — 退化検査は部分集合について
	 *   閉じていないので、全体が立体でも群が同一平面になると「立体にならない」で明示エラーになる。
	 *   ⇒ 主型による振り分け (fold 形) は保ったまま、分解だけを止める。 */
	{ "hull",         MEASURE_IN,1, AK_CACHE, OPWIRE(ggaHull, ggGeom),           1, "(" GG_TYPE ")->" GG_TYPE ";[" GG_TYPE ",mf-mesh3d,ch-mesh3d](*!)->" GG_TYPE, 1 /* ★可換 */ },
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
	/* ★ #3487: 値の素性を訊く op。どれも →value で 2D 型を要さない。無いと確認のためだけに
	 * 別カーネルへ cast させることになり、**cast が通らない値では確認手段そのものが消える**
	 * (#3478 の非有界・非多様体)。中身は common/meshprops.h (valid の共通定義もそこ)。 */
	/* ★ #3514: **位相を直接数える** 3 本。これまで位相の健全性は「シェルを 1 枚失えば体積が
	 *   100 倍ずれる」という *体積を代理指標に使う* 判定しかできていなかった
	 *   (wiki Srava_kernel_sweep_20260911-2 §5.4)。代理をやめる。
	 *     nshells(m)  境界シェル = 面の連結成分の枚数   球 1 ・中空の箱 2 ・xor の N 球 N
	 *     nparts(m)   塊 = 立体の連結成分の数           球 1 ・中空の箱 1 ・xor の N 球 N/2
	 *     genus(m)    種数 = 取っ手の総数               球 0 ・トーラス 1 ・中空の箱 0
	 *   ★ nparts は **nef の nparts と同じ約束** (SNC の marked volume = 塊)。ライブラリが直接
	 *     くれるのはシェルの方なので、符号つき体積が正のシェルを数えて塊に直している
	 *     (定義と根拠は src/h/common/meshprops.h)。
	 *   ⚠ nef 版と違って **変換を挟まない** — これが本題。掃引規模のメッシュを Nef へ通すと
	 *     100GB 級になる (#3510 の掃引) ので、そこでは nef の nparts は事実上使えなかった。 */
	/* ★ #3514: **点との距離** — p から境界までの最短距離 (符号なし)。値返し。
	 *   ⚠ 既存の distance(a,b) は **2 つの立体**の間の距離。問うているものが違うので別名にした
	 *     (位置で指す _at は face_at と同じ流儀)。閉形式: 球 (半径 r) の中心から距離 d の点 → |d - r|。
	 */
	{ "distance_at",  MESH1ARG_IN,2, AK_INLINE,OPWIRE(ggaDistanceAt, ggGeom),  0, "(" GG_TYPE ")->value" },
	/* ★ #3528: **点群の法線を推定する** — cgal 版と **同じ op 名・同じ sig** で中身が違う
	 *   (Co3Ne_compute_normals(M,k,reorient=true))。利用者から見れば 1 つの op で、実装が
	 *   モジュールごとにあるのは srava では普通の形 (nverts は 8 モジュール・minkowski は
	 *   manifold と nef 系が同じ入力型で重なっている)。両方ロードしていれば priority で
	 *   cgal (20) が受け、**`"geogram"::estimate_normals(p)` で名指しできる** (#3467)。
	 *   ⇒ cgal (GPL) を入れない構成でも法線推定ができる。
	 *   ★ 型 pt-cloud3d は geogram のものではない — **中立の libsrava_pt** が持ち、ここは
	 *     借りているだけ (ggCacheCodec.cpp が &ptCloud::WIRE を provides に並べている)。
	 *   ⚠ 印の根拠は cgal 版より弱い — Co3Ne の reorient は「向き付けできなかった点」を
	 *     報告しない (詳細は ggaEstimateNormals.cpp 冒頭)。 */
	{ "estimate_normals", MESH1ARG_IN,2, AK_CACHE, OPWIRE(ggaEstimateNormals, ptCloud), 0, "(pt-cloud3d)->pt-cloud3d", 0, 0, 1 },
	/* ★★ #3554 最後の段 4/5 (2026-09-19): export の行は共通述語 @pig_match_export_ext@ が選ぶ
	 *   (= 第 1 引数の拡張子を **d->export_exts** が書けるか)。出力は常に @ref@ なので
	 *   **行を分ける必要は無い** (cast / import と違うのはここ)。
	 *   ⚠⚠ 同時に **規約① (自型優先) を撤去**した — 「入力型の home カーネルが書けるならそこ」
	 *     という routing の特例で、sig でも記述子でもない *3 つめの規則* だった。
	 *     ⇒ いまは priority × sig × 拡張子 の普通の決着。 */
	{ "export",       EXPORT_IN, 3, AK_CACHE, OPWIRE(ggaExport, ggGeom),       0, "(" GG_TYPE ")->ref", 0, 0, 0, &pig_match_export_ext },
	{ "cast",         CAST_IN,   2, AK_CACHE, OPWIRE(ggaCast, ggGeom),         0, "(" GG_TYPE ")->" GG_TYPE ";(mf-mesh3d)->" GG_TYPE ";(cg-mesh3d)->" GG_TYPE ";(ch-mesh3d)->" GG_TYPE
	                                                          /* ★ #3527: gu-mesh3d も MFM3 ⇒ ggGeom::create_for_meta が読める (2D は geogram に型が無い) */
	                                                          ";(gu-mesh3d)->" GG_TYPE,  /* ★ #3464: 同じ精度クラス (MFM3) */
	                                                          /* ★ #3554 最後の段 2/5: cast の行は
	                                                           *   共通述語 @pig_match_cast_target@ が選ぶ (目標型 = この行の sig の出力型か)。
	                                                           *   ⚠ このカーネルの cast は出力型が 1 つなので **行を分ける必要は無い**
	                                                           *     (分ける理由は「1 行 1 出力型」という規約の方であって、名前ではない)。 */
	                                                          0, 0, 0, &pig_match_cast_target },
	{ "translate",    MESH1ARG_IN,2,AK_CACHE, OPWIRE(ggaTranslate, ggGeom),    0, "(" GG_TYPE ")->" GG_TYPE },
	/* ★ #3486: アフィン変換 4 op。translate だけあって残り 3 本が無いと、式の途中で
	 * **カーネルが裏返る** (rotate を書いた瞬間に cgal/manifold へ落ちる)。4 本とも
	 * 3D→3D で 2D 型を要さないので、2D 型を持たないこのカーネルでも置ける。
	 * 引数の解釈と行列作りは common/affine.h・適用は ggMesh::apply_affine。 */
	{ "rotate",       ROTATE_IN,  3,AK_CACHE, OPWIRE(ggaRotate, ggGeom),       0, "(" GG_TYPE ")->" GG_TYPE },
	{ "scale",        MESH1ARG_IN,2,AK_CACHE, OPWIRE(ggaScale, ggGeom),        0, "(" GG_TYPE ")->" GG_TYPE },
	{ "mirror",       MESH1ARG_IN,2,AK_CACHE, OPWIRE(ggaMirror, ggGeom),       0, "(" GG_TYPE ")->" GG_TYPE },
	{ "transform",    MESH1ARG_IN,2,AK_CACHE, OPWIRE(ggaTransform, ggGeom),    0, "(" GG_TYPE ")->" GG_TYPE },
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
	/* ★ #3474 続き: import を持つので **拡張子を申告する** (未申告だとロード時に拒否)。
	 *   読み手は common/meshio.h。対応形式は STL / OFF だけ。 */
	.import_exts   = "stl:" GG_TYPE ",off:" GG_TYPE,   /* なし (import は他カーネルで入れて cast する) */
	.export_exts   = "off,stl,obj,ply",   /* (GEO::mesh_save の拡張子ディスパッチ) */
	.provides      = geogram_provides,   /* 階層 × 型名 × 4CC (ABI v16) */
	/* ★ v18 (#3466): このモジュールが出す結果の版。**計算を変えたら手で上げる**。 */
	.cache_version = 1,
	.initialize    = 0,   /* 無し */
	/* ★ v10 (#3441): module("geogram.so",{threads:N}) で GEO::Process::set_max_threads を
	 * 呼ぶ opt-in の口。既定 (threads 未指定) は従来どおり GEO::initialize() 任せ (nproc)。 */
	.configure     = &ggMesh::configure,
};
