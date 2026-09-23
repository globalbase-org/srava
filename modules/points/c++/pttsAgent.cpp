/*
 * pttsAgent — 点群モジュール "points" の実行体 (ptsGenericAgent 派生・#3528)。
 *   状態機械は共通基底 ptsGenericAgent に集約済みで、この派生は OPS 表と記述子だけ。
 *
 * ★★ このモジュールは **外部ライブラリを持たない**。srava は「外部ライブラリ 1 つにつき
 *   モジュール 1 つ」という構造だが、点群はそのどれにも属さない — CGAL / geogram / OCCT /
 *   OpenVDB / manifold の **5 つすべてが点群を受け取り、そのどれもが「平坦な double 配列」で
 *   受け取る** (ptCloud.h 冒頭に API を列挙)。メッシュと違って保存すべきカーネル固有表現が
 *   無いので、型をどれか 1 つのカーネルに置くと残り 4 つがそこへ依存することになる
 *   (cgal に置けば geogram / manifold / openvdb が GPL を引き込む)。
 *   ⇒ 型と codec は中立の libsrava_pt に置き、このモジュールがそれを名乗る (ひさ判断 2026-09-13)。
 *
 * ★ 消費側 (voronoi / delaunay / Poisson / RANSAC …) は自分のモジュールで sig 行を型ごとに
 *   書き、provides に &ptCloud::WIRE を並べるだけでよい (occt_mf が mfGeom を借りるのと同じ)。
 *
 * ⚠ area(p) / volume(p) は **ここで宣言しない**。宣言しないことが明示エラーになる —
 *   routing が "no module can execute op 'area' on input types (pt-cloud3d)" と、
 *   受け付ける型まで添えて断る。空の op を置いて同じ文を手で書く必要は無い。
 */
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/ptsApplication.h"    /* ptsApp 値メンバの完全型 */
#include	"pig/c++/ptsAgent.h"
#include	"pig/c++/ptsGenericAgent.h"   /* 共通基底 (状態機械) */
#include	"pig/c++/pigAgentRegistry.h"
#include	"pig/c++/pigModuleRegistry.h"
#include	"pig/c++/pigModule.h"
#include	"pig/c++/pigData.h"
#include	"pig/c++/pigOpEntry.h"
#include	"pig/c++/ptsCalcBody.h"
#include	"pt/c++/ptCloud.h"
#include	"pt/c++/ptaPoints2D.h"
#include	"pt/c++/ptaPoints3D.h"
#include	"pt/c++/ptaRand.h"       /* ★ 2026-09-21: 擬似乱数 (シード必須) */
#include	"pt/c++/ptaRandPt2D.h"
#include	"pt/c++/ptaRandPt3D.h"
#include	"pt/c++/ptaRandGaussian.h"       /* ★ #3576: 正規分布 */
#include	"pt/c++/ptaRandGaussianPt2D.h"
#include	"pt/c++/ptaRandGaussianPt3D.h"
#include	"pt/c++/ptaRandGaussianMix2D.h"   /* ★ #3577: 混合分布 */
#include	"pt/c++/ptaRandGaussianMix3D.h"
#include	"pt/c++/ptaImport.h"
#include	"pt/c++/ptaExport.h"
#include	"pt/c++/ptaNverts.h"
#include	"pt/c++/ptaVert.h"    /* ★ #3527: vert(p, i) — 3 つ組の「取り出す」側 */
#include	"pt/c++/ptaBbox.h"
#include	"pt/c++/ptaCentroid.h"
#include	"pt/c++/ptaValid.h"
#include	"pt/c++/ptaTranslate.h"   /* ★ #3578: transform 一族 */
#include	"pt/c++/ptaRotate.h"
#include	"pt/c++/ptaScale.h"
#include	"pt/c++/ptaMirror.h"
#include	"pt/c++/ptaTransform.h"
#include	"pt/c++/ptaUnion.h"
#include	"ts2/c++/stdString.h"
#include	"pig/c++/pigOpMatch.h"   /* ★ #3554 最後の段 3/5: import の共通マッチ述語 */
#include	"pt/c++/ptAffine.h"       /* ★ #3578: 行と計算本体が共有する述語 */
#include	"_ts2/c++/pttsAgent_.h"

#include	<string.h>

CLASS_TINYSTATE(pt/c++/pttsAgent,pig/c++/ptsGenericAgent)


static const pigArgKind VALUE1_IN[]  = { AK_INLINE };                        /* points3d([..]) / import(path) */
static const pigArgKind EXPORT_IN[]  = { AK_INLINE, AK_CACHE, AK_INLINE };   /* export(path, p, unit) */
static const pigArgKind MEASURE_IN[] = { AK_CACHE };                         /* nverts(p) 等 */
static const pigArgKind IDX_IN[]     = { AK_CACHE, AK_INLINE };              /* vert(p, i) */
/* rand(a,b,n,s)。
 * ★★ 4 個とも **必須** (nreq を書かない = nin 個すべて必須)。シードを省略可能にしないのが
 *   この op 群の設計そのもの — 詳細は modules/points/c++/ptaRand.cpp 冒頭。 */
static const pigArgKind RAND_IN[]    = { AK_INLINE, AK_INLINE, AK_INLINE, AK_INLINE };
/* ★ #3577: rand_gaussian(点群, sigma, n, seed) — 第 1 引数だけ **幾何**。 */
static const pigArgKind MIXG_IN[]    = { AK_CACHE, AK_INLINE, AK_INLINE, AK_INLINE };

/* ★★★ #3572 (2026-09-22 ・ ひさ設計): `rand` / `rand_pt2d` / `rand_pt3d` を **`rand` 1 つ**へ
 *   統合する。行は `rand` / `rand#pt2d` / `rand#pt3d` の 3 本で、選ぶのはこの関数。
 *
 *     rand(0, 10, n, s)                → 値の配列      (軸 1 本 = スカラ)
 *     rand([0,0], [1,1], n, s)         → pt-cloud2d
 *     rand([0,0,0], [1,1,1], n, s)     → pt-cloud3d
 *
 *   ★★ 見るのは **第 1 引数の形だけ** (要素数 0 / 2 / 3)。
 *   ⚠⚠ **第 2 引数はマッチさせない** (ひさ 2026-09-22)。両方をマッチで見ると、a と b の
 *     軸数が食い違ったときに *どの行も成立しない* ことになり、「no candidate」という
 *     **場所を教えない文言**に化ける。⇒ 軸数の食い違いは行が決まった後に op の中で
 *     比べ、`a and b must each be an array of 3 numbers` と名指しで言う
 *     (= いま rand_pt3d が持っている診断をそのまま残す)。
 *
 *   ★ どの軸数の行かは **sig の出力型**から引く (行名の `#` の後ろは読まない)。
 *     ⇒ 宣言が 2 か所に割れない = cast / import と同じ作法 (pigOpMatch.h の説明)。
 *   ★★ 軸 1 本の行 (`rand`) は **「2 でも 3 でもない」を全部引き取る** (要素数 1 や 4 の配列も)。
 *     ⇒ どの形で書いても *行は決まる* ので、間違いは op の中で **名指しで**言える
 *       (`a must be a number (one axis) or an array of 2 or 3 numbers`)。
 *     ⚠ 引き取らないと routing の「どの候補も受けない」に落ち、
 *       `no module can execute op 'rand' … — accepted: ->value` という
 *       *形の話をしていない* 文言になる (実測 2026-09-22)。**行が決まらないと本体が
 *       走らない**ので、診断を op 側に持たせるには行を用意するしかない。 */
static int
pt_match_rand_dim(const srava_module_descriptor *d, const pigOpEntry *e, int argNo, sPtr<pigData> arg)
{
	(void)d;
	if ( argNo != 0 ) return 1;                 /* ★ 第 1 引数の形だけで決める */
	if ( e == 0 ) return 0;
	/* ★★ #3577: **この行が幾何入力を申告しているなら、値の形は見ない** (sig に譲る)。
	 *   @rand_gaussian(pt-cloud3d, sigma, n, seed)@ (混合分布) の行は insets=[pt-cloud3d] で
	 *   選ばれるので、第 1 引数の *値の形* を見る出番が無い。
	 *
	 *   ⚠⚠ ここを **引数の側で** 判定してはいけない。routing の時点では幾何引数は
	 *     まだキャッシュではなく **継続 pair ("delayed" . promise)** なので、
	 *     @arg->is_cache()@ は **偽**を返す (2026-09-22 に実際に踏んだ)。
	 *     ⇒ @pig_val_array_len@ が 0 を返して「軸 1 本」に見え、混合分布の行が
	 *       どれも成立せず「no module can execute op」になる。
	 *   ★ 行の申告 (@e->in[0]@) なら **値を待たずに**決まる。純粋で、再走にも強い。
	 *   ★ 値の行 (in[0]==AK_INLINE) に幾何が渡されたときは、その行の sig が入力型を
	 *     1 つも持たないので sigline_matches が偽になり **構造的に外れる**。 */
	if ( e->in != 0 && e->nin > 0 && e->in[0] == AK_CACHE ) return 1;
	int n = pig_val_array_len(arg);             /* 配列でなければ 0 = 軸 1 本 */
	if ( pig_sig_produces(e->sig, "pt-cloud2d") ) return ( n == 2 ) ? 1 : 0;
	if ( pig_sig_produces(e->sig, "pt-cloud3d") ) return ( n == 3 ) ? 1 : 0;
	if ( pig_sig_produces(e->sig, "value")      ) return ( n == 2 || n == 3 ) ? 0 : 1;
	return 0;                                   /* 知らない出力型の行は選ばない */
}

/* ★ #3578: transform 一族 と union。
 *   XF_IN  = translate / scale / mirror / transform (点群 + 値 1 個)
 *   ROT_IN = rotate (点群 + 軸 + 角度)
 *   UNI_IN = union  (点群 2 つ) */
static const pigArgKind XF_IN[]  = { AK_CACHE, AK_INLINE };
static const pigArgKind ROT_IN[] = { AK_CACHE, AK_INLINE, AK_INLINE };
static const pigArgKind UNI_IN[] = { AK_CACHE, AK_CACHE };

/* ★★ #3578: 変種行 (#xy / #z) を選ぶマッチ関数。
 *   ⚠ どれも **第 2 引数だけ**を見る (行列を決める値)。判定そのものは pt/c++/ptAffine.h に
 *     1 か所だけ置いてあり、**計算本体も同じ関数を呼ぶ** — 別々に書くと *値は正しいのに
 *     型だけ違う* という一番見つけにくい壊れ方になる (#3554 段5a)。
 *   ★ 述語を 5 本に分けているのは、マッチ関数が「どの op か」を引数から知れないため
 *     (e->op の # の後ろを読む手もあるが、それは行名を二重帳簿にする ⇒ pigOpMatch.h の作法に反する)。 */
static int
pt_match_translate_xy(const srava_module_descriptor *d, const pigOpEntry *e, int argNo, sPtr<pigData> arg)
{
	(void)d; (void)e;
	return ( argNo != 1 ) ? 1 : pt_affine_keeps_xy(PT_AFF_TRANSLATE, arg);
}
static int
pt_match_rotate_z(const srava_module_descriptor *d, const pigOpEntry *e, int argNo, sPtr<pigData> arg)
{
	(void)d; (void)e;
	return ( argNo != 1 ) ? 1 : pt_affine_keeps_xy(PT_AFF_ROTATE, arg);   /* 軸は第 2 引数 */
}
static int
pt_match_scale_xy(const srava_module_descriptor *d, const pigOpEntry *e, int argNo, sPtr<pigData> arg)
{
	(void)d; (void)e;
	return ( argNo != 1 ) ? 1 : pt_affine_keeps_xy(PT_AFF_SCALE, arg);
}
static int
pt_match_mirror_xy(const srava_module_descriptor *d, const pigOpEntry *e, int argNo, sPtr<pigData> arg)
{
	(void)d; (void)e;
	return ( argNo != 1 ) ? 1 : pt_affine_keeps_xy(PT_AFF_MIRROR, arg);
}
static int
pt_match_xform_xy(const srava_module_descriptor *d, const pigOpEntry *e, int argNo, sPtr<pigData> arg)
{
	(void)d; (void)e;
	return ( argNo != 1 ) ? 1 : pt_affine_keeps_xy(PT_AFF_TRANSFORM, arg);
}

static const pigOpEntry OPS[] = {
	/* ---- 生成 (leaf) ---- */
	{ "points2d", VALUE1_IN,  1, AK_CACHE,  OPWIRE(ptaPoints2D), 0, "->pt-cloud2d" },
	{ "points3d", VALUE1_IN,  1, AK_CACHE,  OPWIRE(ptaPoints3D), 0, "->pt-cloud3d" },
	/* ---- 擬似乱数 (leaf・2026-09-21 ・ #3572 で 1 つの名前へ 2026-09-22) ----
	 * ★★★ 同じ引数なら必ず同じ結果 (シード必須)。⇒ キャッシュと矛盾しない。
	 * ★ 軸 1 本 (スカラ) なら **値**を返す (幾何引数が無いので sig は "->value")。
	 *   軸 2/3 本 (配列) なら pt-cloud を返す — 10^5 点を値配列で運ばせないため。
	 * ★★ #3572: 次元は **op 名ではなく第 1 引数の形**が言う (pt_match_rand_dim)。
	 *   ⇒ 呼ぶ側は `rand` だけ覚えればよく、`rand_pt2d` と書いて 3 要素を渡す
	 *     (名前と中身が食い違う) 形が構造的に書けなくなる。
	 *   ⚠ 行名 `rand#pt2d` / `rand#pt3d` は ps ・ 診断 ・ キャッシュキーに **そのまま出る**
	 *     (#3554 の表示規約)。⇒ どの行が走ったかは外から確かめられる。
	 * ★ points が名乗るのは「外部ライブラリを持たない中立の計算」だから (このモジュールの
	 *   priority は 0 なので、同名 op を持つ幾何カーネルを押しのけることはない)。 */
	{ "rand",      RAND_IN,   4, AK_INLINE, OPWIRE(ptaRand),      0, "->value",      0, 0, 0, &pt_match_rand_dim },
	{ "rand#pt2d", RAND_IN,   4, AK_CACHE,  OPWIRE(ptaRandPt2D),  0, "->pt-cloud2d", 0, 0, 0, &pt_match_rand_dim },
	{ "rand#pt3d", RAND_IN,   4, AK_CACHE,  OPWIRE(ptaRandPt3D),  0, "->pt-cloud3d", 0, 0, 0, &pt_match_rand_dim },
	/* ---- 正規分布 (#3576 ・ 2026-09-22) ----
	 * ★ 行の選び方は @rand@ と **同じマッチ関数**でよい — @pt_match_rand_dim@ は
	 *   *sig の出力型*から軸数を引くので、op 名に依らない。⇒ 述語を増やさずに済む。
	 * ★ sigma は **各軸の標準偏差** (等方ガウス)。⚠ 「距離の標準偏差」ではない。
	 * ⚠ 値の行は **常に浮動小数点**を返す (一様版の「両方整数なら整数」の規則は無い)。 */
	{ "rand_gaussian",      RAND_IN, 4, AK_INLINE, OPWIRE(ptaRandGaussian),      0, "->value",      0, 0, 0, &pt_match_rand_dim },
	{ "rand_gaussian#pt2d", RAND_IN, 4, AK_CACHE,  OPWIRE(ptaRandGaussianPt2D),  0, "->pt-cloud2d", 0, 0, 0, &pt_match_rand_dim },
	{ "rand_gaussian#pt3d", RAND_IN, 4, AK_CACHE,  OPWIRE(ptaRandGaussianPt3D),  0, "->pt-cloud3d", 0, 0, 0, &pt_match_rand_dim },
	/* ★★ #3577: **混合分布** — 入力点群の各点を中心とする等方ガウスを均等に重ね合わせ、
	 *   そこから n 点を引く。⚠ 「各入力点に中心版を施したもの」とは違う。
	 *   ★ この 2 行は **sig が選ぶ** (第 1 引数が幾何なので insets が立つ)。
	 *     ⇒ 同じ op 名で「マッチ関数が選ぶ行」と「sig が選ぶ行」が同居している。
	 *   ⚠ 第 1 引数の wire は **ptCloud** (値ではなく幾何を受け取る)。 */
	{ "rand_gaussian#mix2d", MIXG_IN, 4, AK_CACHE, OPWIRE(ptaRandGaussianMix2D, ptCloud), 0, "(pt-cloud2d)->pt-cloud2d", 0, 0, 0, &pt_match_rand_dim },
	{ "rand_gaussian#mix3d", MIXG_IN, 4, AK_CACHE, OPWIRE(ptaRandGaussianMix3D, ptCloud), 0, "(pt-cloud3d)->pt-cloud3d", 0, 0, 0, &pt_match_rand_dim },
	/* ---- ファイル (新しい op 名は作らない。拡張子 → 型は import_exts が決める) ---- */
	/* ★ #3554 最後の段 3/5 (2026-09-19): import の行は共通述語 @pig_match_import_ext@ が選ぶ
	 *   (拡張子が産む型 = @d->import_exts@ の型付き CSV が、**この行の sig の出力型**か)。
	 *   ⚠ 出力型が拡張子で決まるので、*sig だけでは行が決まらない* のが import の特徴。
	 *   ★ このカーネルは import の出力型が 1 つなので **行を分ける必要は無い**。 */
	{ "import",   VALUE1_IN,  1, AK_CACHE,  OPWIRE(ptaImport),   0, "->pt-cloud3d", 0, 0, 0, &pig_match_import_ext },
	/* ★★ #3554 最後の段 4/5 (2026-09-19): export の行は共通述語 @pig_match_export_ext@ が選ぶ
	 *   (= 第 1 引数の拡張子を **d->export_exts** が書けるか)。出力は常に @ref@ なので
	 *   **行を分ける必要は無い** (cast / import と違うのはここ)。
	 *   ⚠⚠ 同時に **規約① (自型優先) を撤去**した — 「入力型の home カーネルが書けるならそこ」
	 *     という routing の特例で、sig でも記述子でもない *3 つめの規則* だった。
	 *     ⇒ いまは priority × sig × 拡張子 の普通の決着。 */
	/* ★ #3582 (ひさ判断 2026-09-22): **2D も書ける**。出力は常に ref なので行は 1 本のまま。
	 *   ⚠⚠ 往復で型が変わる (pt-cloud2d → pt-cloud3d) — .xyz に「2D である」ことを書く場所が
	 *     無いため。受け入れたうえで **文書に書く** ことにした (#3582 の案 (a))。 */
	{ "export",   EXPORT_IN,  3, AK_CACHE,  OPWIRE(ptaExport, ptCloud), 0, "(pt-cloud3d)->ref;(pt-cloud2d)->ref", 0, 0, 0, &pig_match_export_ext },
	/* ---- 既存 op の約束のまま点群へ ---- */
	{ "nverts",   MEASURE_IN, 1, AK_INLINE, OPWIRE(ptaNverts, ptCloud),   0, "(pt-cloud2d)->value;(pt-cloud3d)->value" },
	/* ★ #3527: 数えられるのに取り出せなかった側を埋める。⚠ こちらの索引は **格納順** =
	 *   入力の順 ⇒ **定義で決まる索引**。メッシュの vert (列挙順 = 実装依存) とは由来が違う。 */
	{ "vert",     IDX_IN,     2, AK_INLINE, OPWIRE(ptaVert, ptCloud),     0, "(pt-cloud2d)->value;(pt-cloud3d)->value" },
	{ "bbox",     MEASURE_IN, 1, AK_INLINE, OPWIRE(ptaBbox, ptCloud),     0, "(pt-cloud2d)->value;(pt-cloud3d)->value" },
	{ "centroid", MEASURE_IN, 1, AK_INLINE, OPWIRE(ptaCentroid, ptCloud), 0, "(pt-cloud2d)->value;(pt-cloud3d)->value" },
	{ "valid",    MEASURE_IN, 1, AK_INLINE, OPWIRE(ptaValid, ptCloud),    0, "(pt-cloud2d)->value;(pt-cloud3d)->value" },
	/* ---- transform 一族 (#3578 ・ 2026-09-22) ----------------------------------------
	 * ★★ **面外へ出す変換は pt-cloud3d を返す** (sig を 3 本に分ける):
	 *       (pt-cloud2d)->pt-cloud2d   z=0 平面を保つ行列   … 変種行 #xy / #z
	 *       (pt-cloud2d)->pt-cloud3d   面外へ出す行列       … 基底行
	 *       (pt-cloud3d)->pt-cloud3d
	 * ⚠ **変種を先に置く** (無条件の行が前に在ると変種が永久に選ばれず、ロード時に弾かれる)。
	 * ★ 述語は 1 つも新設していない — cgal / manifold / occt の 2D が #3554 段4/5 で使っている
	 *   共通述語 (pigOpMatch.h) をそのまま借りた。⇒ 「平面を保つか」の判定が 8 本目として割れない。
	 * ★ 点群には **枠 (平面) が無い** ので、cgal の cross2d / face3d のような「置かれた 2D」は
	 *   在り得ない。2D を出るということは即ち 3 次元の点群になるということ。 */
	{ "translate#xy", XF_IN,  2, AK_CACHE, OPWIRE(ptaTranslate, ptCloud), 0, "(pt-cloud2d)->pt-cloud2d", 0, 0, 0, &pt_match_translate_xy },
	{ "translate",    XF_IN,  2, AK_CACHE, OPWIRE(ptaTranslate, ptCloud), 0, "(pt-cloud2d)->pt-cloud3d;(pt-cloud3d)->pt-cloud3d" },
	/* ⚠⚠ **軸 z のときだけ** 2D のまま — マッチ関数は引数を 1 個ずつしか見られないので、
	 *   角度が要る「x 軸 180 度」のような平面を保つ回転は救えない (保守的に 3D)。 */
	{ "rotate#z",     ROT_IN, 3, AK_CACHE, OPWIRE(ptaRotate, ptCloud),    0, "(pt-cloud2d)->pt-cloud2d", 0, 0, 0, &pt_match_rotate_z },
	{ "rotate",       ROT_IN, 3, AK_CACHE, OPWIRE(ptaRotate, ptCloud),    0, "(pt-cloud2d)->pt-cloud3d;(pt-cloud3d)->pt-cloud3d" },
	{ "scale#xy",     XF_IN,  2, AK_CACHE, OPWIRE(ptaScale, ptCloud),     0, "(pt-cloud2d)->pt-cloud2d", 0, 0, 0, &pt_match_scale_xy },
	{ "scale",        XF_IN,  2, AK_CACHE, OPWIRE(ptaScale, ptCloud),     0, "(pt-cloud2d)->pt-cloud3d;(pt-cloud3d)->pt-cloud3d" },
	{ "mirror#xy",    XF_IN,  2, AK_CACHE, OPWIRE(ptaMirror, ptCloud),    0, "(pt-cloud2d)->pt-cloud2d", 0, 0, 0, &pt_match_mirror_xy },
	{ "mirror",       XF_IN,  2, AK_CACHE, OPWIRE(ptaMirror, ptCloud),    0, "(pt-cloud2d)->pt-cloud3d;(pt-cloud3d)->pt-cloud3d" },
	{ "transform#xy", XF_IN,  2, AK_CACHE, OPWIRE(ptaTransform, ptCloud), 0, "(pt-cloud2d)->pt-cloud2d", 0, 0, 0, &pt_match_xform_xy },
	{ "transform",    XF_IN,  2, AK_CACHE, OPWIRE(ptaTransform, ptCloud), 0, "(pt-cloud2d)->pt-cloud3d;(pt-cloud3d)->pt-cloud3d" },
	/* ---- union (#3578) ---------------------------------------------------------------
	 * ★ **単純に混ぜる** (重複を落とさない) ⇒ nverts(union(a,b)) == nverts(a)+nverts(b)。
	 *
	 * ★★ sig は **fold 形 2 本** (ひさ指示 2026-09-22「union([a,b,c]) は入れておきましょう」)。
	 *   ⚠ 当初は #3575 の「fold 形にしない」に従って固定形 4 本にしていたが、それだと
	 *     @union([a,b,c])@ が通らない — 配列形は pigfArrayFold が **n 項ノード 1 つ**に畳むだけで、
	 *     二項の木へ分解するのは @pigfModuleAgent::try_decompose@ であり、それは
	 *     **fold 形の行にマッチしたときにしか働かない** (@foldN < 0@ なら分解しない)。
	 *
	 *   [pt-cloud3d,pt-cloud2d](2)->pt-cloud3d   主型 3d が 1 つでも在れば 3D
	 *   [pt-cloud2d](2)->pt-cloud2d              全部 2D のときだけ 2D
	 *
	 *   ★ 主型の規則 (可変部に set[0] が最低 1 個) が「どちらかが 3D なら 3D」をそのまま書いた形に
	 *     なっている。⇒ 昇格の規則が sig **だけ**で決まり、op の中と二重帳簿にならない。
	 *   ★ (2) は **一度に 2 項まで**という capability。3 項以上は分解へ回る (cgal の union と同じ)。
	 *
	 * ★★ #3575 の懸念「2D+3D 混在を許すと畳む順で主型が動く」は **union には当たらない**。
	 *   昇格が max(dim) = 結合的かつ単調なので、どう畳んでも答えは同じ:
	 *       [2d,2d,3d] → union(union(2d,2d)=2d, 3d) = 3d
	 *       [2d,3d,2d] → union(union(2d,3d)=3d, 2d) = 3d
	 *   ⚠ 当たるのは #3575 の (8)(9) (型が非対称で、分解すると意味が変わるもの) の方。
	 *
	 * ⚠⚠ **可換の印を立てない** (末尾の 0)。並びは「格納順 = 入力の順」(#3527) が約束なので、
	 *   union(a,b) と union(b,a) は同じ点集合で **違う点群**。印を立てると
	 *     ① 分解が均衡木になり (非可換なら **左 fold** = 書かれた順のまま)
	 *     ② 二項ノードのキャッシュキーが正規化されて両者が同じ結果を返す
	 *   ⇒ 索引の約束が静かに破れる。 */
	{ "union",        UNI_IN, 2, AK_CACHE, OPWIRE(ptaUnion, ptCloud, ptCloud), 0,
	                                       "[pt-cloud3d,pt-cloud2d](2)->pt-cloud3d"
	                                       ";[pt-cloud2d](2)->pt-cloud2d" },
};
static const int N_OPS = (int)(sizeof(OPS) / sizeof(OPS[0]));

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	pttsAgent_(
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


pttsAgent_::pttsAgent_(TS_ARGS0)
        : ptsGenericAgent_(parent)
{
    TS_CPARGS0
}

const pigOpEntry*	pttsAgent_::agent_ops()   { return OPS; }
int			pttsAgent_::agent_n_ops() { return N_OPS; }
const char*		pttsAgent_::agent_name()  { return PT_MODULE_NAME; }

static sPtr<ptsAgent>
mk_pttsAgent(sPtr<ptsObject> med)
{
	return thNEW(pttsAgent,(med));
}

extern const pigModuleType points_provides[];
extern const srava_module_descriptor pttsAgent_descriptor;
const srava_module_descriptor pttsAgent_descriptor = {
	.abi_version   = SRAVA_MODULE_ABI,
	.name          = PT_MODULE_NAME,
	/* ★ priority 0: **既定カーネルにはならない** (幾何カーネルではないので候補にもならない —
	 *   box / sphere のような立体の op を 1 つも名乗っていない)。同名 op (import/export/nverts/
	 *   bbox/centroid/valid) はすべて **入力型で** 振り分けられるので、priority は効かない。 */
	.priority      = 0,
	.make_agent    = &mk_pttsAgent,
	/* 中身は平坦な配列の走査と stdio だけで、スレッド安全でない外部ライブラリを持たない。 */
	.exec_caps     = (unsigned)(EXEC_THREAD | EXEC_PROCESS),
	.exec_default  = EXEC_PROCESS,
	.ops           = OPS,
	.n_ops         = N_OPS,
	/* ★ 型つき CSV ("ext:出力型")。⚠ **曖昧でない拡張子だけ**を点群に割り当てる —
	 *   ply はメッシュにも純粋な点群にもなりうるが、型は routing の段で拡張子から決まるので
	 *   中身を見てから選べない (しかも ply は既に cgal が ply:cg-mesh3d と申告済み)。
	 *   off も面 0 個なら同じ問題。⇒ 形式は当面 xyz だけ (ひさ判断 2026-09-13)。 */
	.import_exts   = "xyz:pt-cloud3d",
	.export_exts   = "xyz",
	.provides      = points_provides,   /* 階層 × 型名 × 4CC (ABI v16) */
	/* ★ v18 (#3466): このモジュールが出す結果の版。**計算を変えたら手で上げる**。 */
	.cache_version = 1,
	.initialize    = 0,   /* 無し */
	.configure     = 0,   /* module() の opts は消費しない */
};
