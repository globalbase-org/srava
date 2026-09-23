/*
 * gutsAgent — 中立モジュール "geomutils" の実行体 (ptsGenericAgent 派生・#3527)。
 *   状態機械は共通基底 ptsGenericAgent に集約済みで、この派生は OPS 表と記述子だけ。
 *
 * ★★ このモジュールは **外部ライブラリを持たない** (points.so と同じ位置づけ)。
 *   素性を訊く計算の中身は既に src/h/common/meshprops.h (3D) と ringprops.h (2D) に
 *   *定義ごと 1 本*で書かれており、manifold / geogram / cherchi はどれも内部表現から
 *   素の配列へ写してそこを通っている。⇒ 実質「同じ型」を 3 者が名乗らずに作っていた。
 *   型として名乗らせ、1 モジュールが所有する (ひさ設計 2026-09-17)。
 *
 * ★ priority = 7 — **必要十分**として決めた値。上下の両側に根拠がある:
 *     5 以下 … mf / gg から 3D の素性 op を落とした瞬間、いま priority に負けて
 *              死んでいる nef の nparts (mf-mesh3d) / (gg-mesh3d) の 4 行が **生き返り**、
 *              nparts が黙って Nef 変換経路へ落ちる (#3510 の掃引規模では SNC が 100GB 級)
 *     8 以上 … 押さえる対象は増えない。mf (10) を超えると 2D まで奪い、
 *              「2D は manifold のまま」という決定と食い違う
 *   ⚠ cgal (20) は超えない ⇒ **既定カーネルにはならない** (leaf は priority 最大へ振られるが、
 *     このモジュールは box / sphere のような立体を作る op を 1 つも名乗っていない)。
 *
 * ⚠ op は **自分の型に答えるものから**入れる。他カーネルの型を名乗る行 (移送) は段 3 で、
 *   先に宣言すると「routing は通るが中身が無い」状態ができる。
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
#include	"gu/c++/guGeom.h"
#include	"gu/c++/guaArea.h"
#include	"gu/c++/guaBbox.h"
#include	"gu/c++/guaCast.h"
#include	"gu/c++/guaCentroid.h"
#include	"gu/c++/guaGenus.h"
#include	"gu/c++/guaNfaces.h"
#include	"gu/c++/guaNparts.h"
#include	"gu/c++/guaPtIntersection.h"   /* ★ #3579 */
#include	"gu/c++/guaPtDifference.h"     /* ★ #3579 */
#include	"gu/c++/guaNshells.h"
#include	"gu/c++/guaNverts.h"
#include	"gu/c++/guaPart.h"
#include	"gu/c++/guaPartAt.h"
#include	"gu/c++/guaDistanceAt.h"   /* ★ #3553 */
#include	"gu/c++/guaShell.h"
#include	"gu/c++/guaShellAt.h"
#include	"gu/c++/guaVert.h"
#include	"gu/c++/guaVerts.h"
#include	"gu/c++/guaFaceVerts.h"
#include	"pt/c++/ptCloud.h"   /* ★ #3527 段 5: verts が返す **値の器** を借りる (#3528) */
#include	"gu/c++/guaValid.h"
#include	"gu/c++/guaVolume.h"
#include	"ts2/c++/stdString.h"
#include	"pig/c++/pigOpMatch.h"   /* ★ #3554 最後の段 2/5: cast の共通マッチ述語 */
#include	"_ts2/c++/gutsAgent_.h"

#include	<string.h>

CLASS_TINYSTATE(gu/c++/gutsAgent,pig/c++/ptsGenericAgent)

static const pigArgKind MEASURE_IN[] = { AK_CACHE };                 /* volume(m) 等 */
static const pigArgKind CAST_IN[]    = { AK_INLINE, AK_CACHE };     /* cast("gu-…", v) */
static const pigArgKind PIECE_IN[]   = { AK_CACHE, AK_INLINE };     /* part(v,i) / part_at(v,p) */
/* ★ #3579: 点群 x mesh。**幾何が 2 つ** = sig に 2 つ現れる (値引数 mode は sig に出ない)。 */
static const pigArgKind PTSPLIT3_IN[] = { AK_CACHE, AK_CACHE, AK_INLINE };  /* intersection(A,M,mode) */
static const pigArgKind PTSPLIT2_IN[] = { AK_CACHE, AK_CACHE };             /* difference(A,M) */

static const pigOpEntry OPS[] = {
	/* ★★ #3527 段 2: **型の入口**。これが無いと gu の値を作る手段が 1 つも無く、
	 *   5 カーネルに足した出口の行も volume も一度も走らせられない (実測で確認した)。
	 *   ★ 実体化は全部 guGeom::WIRE が済ませるので、この op は identity。
	 *     MFM3 (mf/gg/ch/gu) と MFC2 (mf/gu) はそのまま・MESH / PLY2 (cgal の厳密) は
	 *     decode_*_exact が有理数 → double へ落として読む。
	 *   ⚠ 厳密 → double は **不可逆**。厳密が要る問いは cgal 側で訊くこと。
	 *   ⚠ nfb-mesh3d (NEFB) は名乗らない — gg / ch も受けていない (橋が要る話)。 */
	/* ★★ #3554 最後の段 2/5 (2026-09-19): cast は **目標型ごとに 1 行**。行を選ぶのは
	 *   共通述語 @pig_match_cast_target@ (第 1 引数の型名が この行の sig の出力型か)。
	 *   ⚠ 1 行に出力型を 2 つ書くと、行の可否と実際に名乗る型が食い違う
	 *     ⇒ ロード時に pig_descriptor_violation が弾く。 */
	{ "cast#" GU_TYPE_3D, CAST_IN, 2, AK_CACHE, OPWIRE(guaCast, guGeom), 0,
	  "(" GU_TYPE_3D ")->" GU_TYPE_3D ";(mf-mesh3d)->" GU_TYPE_3D ";(gg-mesh3d)->" GU_TYPE_3D
	  ";(ch-mesh3d)->" GU_TYPE_3D ";(cg-mesh3d)->" GU_TYPE_3D, 0, 0, 0, &pig_match_cast_target },
	{ "cast#" GU_TYPE_2D, CAST_IN, 2, AK_CACHE, OPWIRE(guaCast, guGeom), 0,
	  "(" GU_TYPE_2D ")->" GU_TYPE_2D ";(mf-cross2d)->" GU_TYPE_2D ";(cg-cross2d)->" GU_TYPE_2D
	  /* ★★ 規約②: 降格は cast だけ。frame_is_default() が偽なら明示エラー (幾何は動かさない)。 */
	  ";(" GU_TYPE_2DP ")->" GU_TYPE_2D ";(mf-face3d)->" GU_TYPE_2D ";(cg-face3d)->" GU_TYPE_2D,
	  0, 0, 0, &pig_match_cast_target },
	{ "cast#" GU_TYPE_2DP, CAST_IN, 2, AK_CACHE, OPWIRE(guaCast, guGeom), 0,
	  "(" GU_TYPE_2DP ")->" GU_TYPE_2DP ";(mf-face3d)->" GU_TYPE_2DP ";(cg-face3d)->" GU_TYPE_2DP,
	  0, 0, 0, &pig_match_cast_target },
	/* ★★ #3527: **自分が産んだ型の体積は自分で答える**。part(mf-mesh3d,i) の返りが
	 *   gu-mesh3d になるので、これが無いと設計の中心にある検定
	 *     Σ volume(part(m,i)) == volume(m)   (符号なし)
	 *     Σ volume(shell(m,i)) == volume(m)  (符号つきでのみ成立)
	 *   が **書けない**。
	 * ⚠ 他カーネルの型 (mf/gg/ch-mesh3d) は **名乗らない** — あちらは自前のライブラリで
	 *   答えており (mfMesh::op_volume() は m_.Volume())、meshprops 経由ではないので
	 *   寄せる対象ではない。名乗れば priority 7 が gg(6)/ch(3) を奪って **答えが変わる**。
	 * ⚠ 2D に体積は無いので gu-cross2d / gu-face3d の行は置かない (#3525 と同じ線引き)。 */
	{ "volume", MEASURE_IN, 1, AK_INLINE, OPWIRE(guaVolume, guGeom), 0, "(" GU_TYPE_3D ")->value" },

	/* ================= 段 3: mf / gg / ch から **寄せた** 素性 op ==================
	 * ★★ この 9 本は *移動* であって新実装ではない。mf / gg / ch はこれまで内部表現から
	 *   素の配列へ写して common/meshprops.h を通していた ⇒ 答えは 1 ビットも変わらない。
	 *   (検定は「寄せる前 / 後で同値」。⚠ それだけだと *両方が同じ壊れ方* を見逃すので、
	 *    codec を壊すと赤くなる陽性対照と対で回す。)
	 * ⚠ cgal (cg-*) は **名乗らない** — 据置。名乗っても priority 20 に負けて死に行になる
	 *   (#3527 の訂正 1 で perimeter がまさにそれだった)。
	 * ⚠ 2D は **manifold を触らない**決定 (mf が既に持つ 5 本はそのまま)。gu が 2D で
	 *   名乗るのは **自分の型** と、mf に無い valid / nparts だけ。
	 * ⚠ nshells / genus に 2D の行は無い (曲面の量なので 2D では定義できない・#3525)。 */
#define GU_3D	"(" GU_TYPE_3D ")->value;(mf-mesh3d)->value;(gg-mesh3d)->value;(ch-mesh3d)->value"
#define GU_ALL	GU_3D ";(" GU_TYPE_2D ")->value;(" GU_TYPE_2DP ")->value"
	{ "nverts",   MEASURE_IN, 1, AK_INLINE, OPWIRE(guaNverts,   guGeom), 0, GU_ALL },
	{ "nfaces",   MEASURE_IN, 1, AK_INLINE, OPWIRE(guaNfaces,   guGeom), 0, GU_ALL },
	{ "area",     MEASURE_IN, 1, AK_INLINE, OPWIRE(guaArea,     guGeom), 0, GU_ALL },
	{ "bbox",     MEASURE_IN, 1, AK_INLINE, OPWIRE(guaBbox,     guGeom), 0, GU_ALL },
	{ "centroid", MEASURE_IN, 1, AK_INLINE, OPWIRE(guaCentroid, guGeom), 0, GU_ALL },
	/* ★ valid / nparts は 2D でも **mf に無い** ので、mf-cross2d / mf-face3d も名乗る。 */
	{ "valid",    MEASURE_IN, 1, AK_INLINE, OPWIRE(guaValid,    guGeom), 0,
	  GU_ALL ";(mf-cross2d)->value;(mf-face3d)->value" },
	{ "nparts",   MEASURE_IN, 1, AK_INLINE, OPWIRE(guaNparts,   guGeom), 0,
	  GU_ALL ";(mf-cross2d)->value;(mf-face3d)->value" },
	{ "nshells",  MEASURE_IN, 1, AK_INLINE, OPWIRE(guaNshells,  guGeom), 0, GU_3D },
	{ "genus",    MEASURE_IN, 1, AK_INLINE, OPWIRE(guaGenus,    guGeom), 0, GU_3D },

	/* ================= 段 4: **片の取り出し** (入れ子) ==============================
	 * ★★★ ここが段 4 の本丸。数える側 (nparts / nshells) は既に在ったのに取り出せなかった
	 *   のは、塊が「外殻 **+ その直接の空洞**」の両方を境界に持つからで、取り出すには
	 *   **どの空洞がどの塊のものか** = 入れ子を解く必要がある。⇒ meshprops.h の nesting()
	 *   (3D・巻き数) と ringprops.h の nesting() (2D・点の内外) が段 4 で書いた実体。
	 *
	 * ★★ **数える側と取り出す側が同じ分解を見る**ようになった。これが #3527 の決定の核心で、
	 *   現況は nparts(mf-mesh3d) が meshprops の double・part(mf-mesh3d,i) が nef の SNC と
	 *   *別々の分解* を見ており、Σ volume(part(m,i)) == volume(m) が原理的に保証されて
	 *   いなかった。1 実装に寄せると自動的に消える。
	 *
	 * ⚠⚠ **返り型が変わる**: part(mf-mesh3d,i) / part(gg-mesh3d,i) は いま nef が答えており
	 *   nf-mesh3d を返す。gu (priority 7 > nef 5) が取ると **gu-mesh3d** になる。
	 *   ⇒ 意図した変化 (#3527 のジャーナル「書き漏らし 1」で明記済み)。厳密な答えが要るなら
	 *     "nef_snc"::part(...) と名指しすれば従来どおり取れる。
	 * ⚠ cg-* は **名乗らない** — cgal (20) が自分の型に厳密に答え続ける (規約どおり)。
	 *
	 * ★ 2D は part / part_at だけ。**殻は曲面の概念**なので 2D には無い (#3525 の線引きと同じ)。
	 *   ⇒ shell / shell_at に 2D の行を置かない ⇒ ルータが先に弾く。 */
#define GU_PIECE_3D	"(" GU_TYPE_3D ")->" GU_TYPE_3D ";(mf-mesh3d)->" GU_TYPE_3D \
			";(gg-mesh3d)->" GU_TYPE_3D ";(ch-mesh3d)->" GU_TYPE_3D
#define GU_PIECE_2D	";(" GU_TYPE_2D ")->" GU_TYPE_2D ";(" GU_TYPE_2DP ")->" GU_TYPE_2DP \
			";(mf-cross2d)->" GU_TYPE_2D ";(mf-face3d)->" GU_TYPE_2DP
	{ "part",     PIECE_IN, 2, AK_CACHE, OPWIRE(guaPart,    guGeom), 0, GU_PIECE_3D GU_PIECE_2D },
	{ "part_at",  PIECE_IN, 2, AK_CACHE, OPWIRE(guaPartAt,  guGeom), 0, GU_PIECE_3D GU_PIECE_2D },
	{ "shell",    PIECE_IN, 2, AK_CACHE, OPWIRE(guaShell,   guGeom), 0, GU_PIECE_3D },
	{ "shell_at", PIECE_IN, 2, AK_CACHE, OPWIRE(guaShellAt, guGeom), 0, GU_PIECE_3D },

	/* ================= #3579: **点群と mesh の積と差** ==============================
	 * ★★★ 境界ちょうどの点は **第 3 の集合**。@mode@ で選ぶ (0 境界 / -1 内側 / +1 外側)。
	 *   ⇒ section の共面と同じ約束・同じ 0/-1/+1。⚠ 「境界は含む」と決め打たない —
	 *     整数格子の点群では **97% が境界に載る** (mac 実測 194/200)。
	 *   ★ 3 要素配列の口は **パーサの糖衣** (section が既にそうしている) ⇒ sig は mode 形だけ。
	 *     sig の outtype は登録型 1 個しか書けない (配列には WIRE も 4CC も無い)。
	 *
	 * ⚠⚠ **可換にしない ・ fold 形にしない**。型が非対称なので可換フラグを立てると
	 *   fold 分解が引数を組み替えて壊れる。⇒ 固定形 "(a,b)->c" で書き、commutative は 0。
	 *   ⇒ @intersection(mesh, 点群)@ の順は sig に無いので **routing が断る**。
	 *
	 * ★ 行は **3 つ** (アルゴリズムは 2 つ)。⚠ 3 つ目を忘れると「2D 同士が落ちる」:
	 *     (pt-cloud3d, mesh3d)  巻き数            meshprops.h shell_winding
	 *     (pt-cloud2d, mesh3d)  巻き数・点は z=0  ★ 暗黙の昇格 ⇒ docs に明記した
	 *     (pt-cloud2d, cross2d) point-in-polygon  ringprops.h part_at
	 *   ⚠ @pt-cloud3d x cross2d@ (X > Y) は **書かない** ⇒ routing が断る。黙って射影して
	 *     答えると「面外の高さを捨てた答え」になる (#3534 と同じ線引き)。
	 * ⚠ face3d (2DP) の行は **置いていない** — 「3D の点が *平面上の領域* に入るか」は
	 *   面外成分の扱いを決めないと言えない。#3575 の 3 行にも挙がっていないので、
	 *   意味を勝手に作らず **空けてある** (dev-macmini-1 へ照会済み)。
	 *
	 * ⚠ 速度: 巻き数は 1 点 O(面数)。10^5 点を想定するので **bbox で先に枝刈り**してある
	 *   (guGeom.cpp の op_classify_points)。それでも足りなければ後段の課題。 */
#define GU_PTSPLIT	"(" PT_TYPE_3D "," GU_TYPE_3D ")->" PT_TYPE_3D \
			";(" PT_TYPE_3D ",mf-mesh3d)->" PT_TYPE_3D \
			";(" PT_TYPE_3D ",gg-mesh3d)->" PT_TYPE_3D \
			";(" PT_TYPE_3D ",ch-mesh3d)->" PT_TYPE_3D \
			";(" PT_TYPE_2D "," GU_TYPE_3D ")->" PT_TYPE_2D \
			";(" PT_TYPE_2D ",mf-mesh3d)->" PT_TYPE_2D \
			";(" PT_TYPE_2D ",gg-mesh3d)->" PT_TYPE_2D \
			";(" PT_TYPE_2D ",ch-mesh3d)->" PT_TYPE_2D \
			";(" PT_TYPE_2D "," GU_TYPE_2D ")->" PT_TYPE_2D \
			";(" PT_TYPE_2D ",mf-cross2d)->" PT_TYPE_2D
	/* ⚠ 引数の数が違う: intersection は mode を取る (3) ・ difference は取らない (2)。
	 *   ★ #3570 で routing に引数の数が入ったので、これで別の op として解ける。 */
	{ "intersection", PTSPLIT3_IN, 3, AK_CACHE, OPWIRE(guaPtIntersection, ptCloud, guGeom), 0,
	  GU_PTSPLIT },
	{ "difference",   PTSPLIT2_IN, 2, AK_CACHE, OPWIRE(guaPtDifference,   ptCloud, guGeom), 0,
	  GU_PTSPLIT },

	/* ================= 段 5: **頂点を読む** ========================================
	 * ★★ #3527 の「やること 4」— @nverts@ で数えられるのに **座標を読む op が 1 本も無かった**。
	 *   09-17 午前に cgal へ入り、ここで mf / gg / ch へ広がる (2D も同時に埋まる)。
	 *
	 * ★ 3 つ組の 2 つ: @vert(v,i)@ が「取り出す」・@verts(v)@ が「まとめて」。
	 *   ⚠ 「位置で指す」に当たるものは **作らない** — 点は片ではないので「その位置に在る頂点」は
	 *     *最近傍* の話になり、@closest@ が既にその役をしている (規約③の 3 つ組は片に対する規約)。
	 *
	 * ★★ @verts@ は **点群型を借りて**返す (#3528 の中立型)。借りているのは *幾何の機能* ではなく
	 *   **値の器**だけなので、モジュール境界の約束①「他カーネルの機能を借りて自分の顔で出さない」に
	 *   触れない。前例は cgal の @estimate_normals@ / @verts@。
	 *   ⇒ provides に &ptCloud::WIRE を並べ、LINK に srava_pt を足してある (guCacheCodec.cpp)。
	 *
	 * ★★ 成分数は bbox / centroid と **同じ約束**。⇒ face3d は **world の 3 成分 / pt-cloud3d**。
	 *   ⚠⚠ 枠の中の 2 成分だと *置き場所が黙って落ちる* — 違う高さの断面が同じ答えを返し、
	 *     hull(verts(sec)) が常に z=0 に出る (2026-09-17 実測)。#3533 が bbox / centroid で決めた
	 *     「face3d は world」へ揃えた (ひさ判断)。**cgal も同時に直した** (cache_version 8→9)。
	 *
	 * ⚠ @face_verts@ は 3D だけ — 2D は面を持たない。⇒ 2D の行を置かず、ルータに弾かせる。
	 * ★ @face(m,i)@ (三角形そのものを mesh で返す) は **作らない** (#3527 の規約⑤)。 */
#define GU_VERT_3D	"(" GU_TYPE_3D ")->value;(mf-mesh3d)->value;(gg-mesh3d)->value;(ch-mesh3d)->value"
#define GU_VERT_2D	";(" GU_TYPE_2D ")->value;(" GU_TYPE_2DP ")->value" \
			";(mf-cross2d)->value;(mf-face3d)->value"
	{ "vert",       PIECE_IN,   2, AK_INLINE, OPWIRE(guaVert,      guGeom), 0, GU_VERT_3D GU_VERT_2D },
	{ "face_verts", PIECE_IN,   2, AK_INLINE, OPWIRE(guaFaceVerts, guGeom), 0, GU_VERT_3D },
	/* ★★ #3553 (2026-09-18・ひさ裁定): **点との距離**。定義は 3D と同じ (面の集合までの最短距離)。
	 *   ⇒ mf / ch はここで初めて持つ。2D (mf-cross2d / mf-face3d) も同じ定義で受ける。
	 * ⚠⚠ **gg-mesh3d は載せない** — geogram は MeshFacetsAABB で自前に持っているので、
	 *   priority でこちらが勝つと *速い実装を総当たりで置き換える* ことになる。
	 *   ⇒ 歯抜けを埋めるモジュールが、既にあるものを奪ってはいけない。
	 * ⚠ cg-* も載せない (cgal は AABB_tree で 3D を持つ・2D は現状のまま = ひさ判断)。 */
	{ "distance_at", PIECE_IN,  2, AK_INLINE, OPWIRE(guaDistanceAt, guGeom), 0,
	  "(" GU_TYPE_3D ")->value;(mf-mesh3d)->value;(ch-mesh3d)->value"
	  ";(" GU_TYPE_2D ")->value;(" GU_TYPE_2DP ")->value"
	  ";(mf-cross2d)->value;(mf-face3d)->value" },
	/* ⚠ 返り型は **成分数で分かれる**: 3D と face3d は pt-cloud3d ・ cross2d は pt-cloud2d。 */
	{ "verts",      MEASURE_IN, 1, AK_CACHE,  OPWIRE(guaVerts,     guGeom), 0,
	  "(" GU_TYPE_3D ")->" PT_TYPE_3D ";(mf-mesh3d)->" PT_TYPE_3D
	  ";(gg-mesh3d)->" PT_TYPE_3D ";(ch-mesh3d)->" PT_TYPE_3D
	  ";(" GU_TYPE_2D ")->" PT_TYPE_2D ";(mf-cross2d)->" PT_TYPE_2D
	  ";(" GU_TYPE_2DP ")->" PT_TYPE_3D ";(mf-face3d)->" PT_TYPE_3D },
#undef GU_VERT_3D
#undef GU_VERT_2D
#undef GU_PIECE_3D
#undef GU_PIECE_2D
#undef GU_3D
#undef GU_ALL
};
static const int N_OPS = (int)(sizeof(OPS) / sizeof(OPS[0]));

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	gutsAgent_(
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

gutsAgent_::gutsAgent_(TS_ARGS0)
        : ptsGenericAgent_(parent)
{
    TS_CPARGS0
}

const pigOpEntry*	gutsAgent_::agent_ops()   { return OPS; }
int			gutsAgent_::agent_n_ops() { return N_OPS; }
const char*		gutsAgent_::agent_name()  { return GU_MODULE_NAME; }

static sPtr<ptsAgent>
mk_gutsAgent(sPtr<ptsObject> med)
{
	return thNEW(gutsAgent,(med));
}

extern const pigModuleType geomutils_provides[];
extern const srava_module_descriptor gutsAgent_descriptor;
const srava_module_descriptor gutsAgent_descriptor = {
	.abi_version   = SRAVA_MODULE_ABI,
	.name          = GU_MODULE_NAME,
	.priority      = 7,
	.make_agent    = &mk_gutsAgent,
	/* 中身は平坦な配列の走査だけで、スレッド安全でない外部ライブラリを持たない。 */
	.exec_caps     = (unsigned)(EXEC_THREAD | EXEC_PROCESS),
	.exec_default  = EXEC_PROCESS,
	.ops           = OPS,
	.n_ops         = N_OPS,
	.import_exts   = 0,
	.export_exts   = 0,
	.provides      = geomutils_provides,
	/* ★ 新設モジュールなので 1 から始める。cache_version は **モジュールごとに独立**
	 *   (ソルトが "\x01" + モジュール名 + "\x01" + "v" + 版 で名前空間が切られている)
	 *   ので、他モジュールの番号とは無関係 (pigModuleRegistry.h:246)。 */
	.cache_version = 1,
	.initialize    = 0,   /* 無し */
	.configure     = 0,   /* module() の opts は消費しない */
};
