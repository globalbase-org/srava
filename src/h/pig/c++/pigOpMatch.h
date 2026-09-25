#ifndef ___pigOpMatch_H___
#define ___pigOpMatch_H___
/*
 * pigOpMatch — OPS 行の **マッチ関数** (#3554 AK_MATCH) が使う *共通の述語*。
 *
 * ---- なぜ 1 か所に置くのか (ひさ 2026-09-19) ----
 * ★ マッチの中身はモジュールを跨いで **数種類しかない** — cast の目標型判定 / 拡張子 /
 *   「行列が平面を保つか」。これをモジュールごとに書くと *判定が割れる*。
 *   ⇒ 値の読み方 (pigData をどう覗くか) は **pig 層**に、幾何の判定そのものは
 *     @common/affine.h@ に置き、ここはその 2 つを繋ぐだけにする。
 *
 * ---- 行に書くものは 2 通り ----
 *   (1) 引数位置が規約で決まる op (cast / import / export)
 *       → ここの関数を **そのまま行に書ける**。モジュール側は 1 行も書かない
 *   (2) 引数位置が op 固有のもの (transform の行列は第 2・rotate の角は第 3 …)
 *       → モジュール側は「**何番を見るか**」だけの薄いラッパ。判定本体はここを呼ぶ
 *
 * ⚠ マッチ関数は **純粋** (値を読むだけ)。候補ごとに何度でも呼ばれうる。
 * ⚠ 値を読む口 (@get_int@ / @obt_array@ …) 自体がゲートウェイなので、@compact()@ を
 *   自分で呼ぶ必要は無い ⇒ *読めば自動的に待つ*。
 *   ★ ただし **配列 / ハッシュの要素は eager に解決されない** ので、要素を見るときは
 *     要素ごとに口を通すこと (@obt_array@ で取ってから @elem@ 等)。
 */
#include "pig/c++/pigData.h"
#include "pig/c++/pigOpEntry.h"
#include "common/affine.h"
#include "pig/c++/pigModule.h"            /* srava_module_descriptor の完全型 */
#include "pig/c++/pigModuleRegistry.h"   /* csv_has_ext / pig_ext_out_type */
#include "pig/c++/pigSigGrammar.h"      /* pig_sig_produces */
#include "ts2/c++/stdString.h"
#include <string>

/* ★★ #3570 段3: 値が **ハッシュ (オプション束)** か。
 *   1 = ハッシュ / 0 = それ以外 (数・配列・文字列・エラー・未定義)。
 *
 *   ⚠ 記述子の @in[]@ は @AK_INLINE@ (値) と @AK_CACHE@ (幾何) の 2 種しかなく、
 *     「値の中でもハッシュ」を表せない。⇒ **行を分けるにはマッチ関数しかない**
 *     (#3554 の 4 本目の用途)。
 *   例: @tube(path, {closed:1})@ は occt・@tube(path, 24)@ は *segs を取る他カーネル* へ。
 *   ⚠ 純粋 — 値を読むだけ。@obt_hash@ はゲートウェイなので読めば自動的に待つ。 */
inline int
pig_val_is_hash(sPtr<pigData> arg)
{
	if ( arg == thNULL || arg->is_error() ) return 0;
	return arg->obt_hash().is_notNull() ? 1 : 0;
}

/* ★★ #3572: 値が **配列なら要素数**・配列でなければ **0**。
 *
 *   ⚠ 「配列でない」と「要素 0 個の配列」を **区別しない**。区別しないのは、これを使う側
 *     (@rand@ の軸数) にとって *スカラ = 軸 1 本* と *空配列* は「軸の数を言っていない」で
 *     同じだから — 区別すると呼び手に意味の無い書き分けを強いる。
 *     ⇒ 区別が要る op が出たら、そのときに別の述語を足すこと (この関数を曖昧にしない)。
 *   ⚠ 純粋 — 値を読むだけ。@obt_array@ はゲートウェイなので読めば自動的に待つ
 *     (@compact()@ を自分で呼ぶ必要は無い。@rand(map(…), …)@ ・ @rand(rand(…), …)@ を
 *      in-proc / process の両方で実測して確かめた 2026-09-22)。
 *     ★ ただし **要素は見ない** (要素は eager に解決されないので、数えるだけなら通らない)。 */
inline int
pig_val_array_len(sPtr<pigData> arg)
{
	if ( arg == thNULL || arg->is_error() ) return 0;
	sPtr<pigDataArray> ar = arg->obt_array();
	return ar.is_notNull() ? ar->length() : 0;
}

/* ★★ #3588 (2026-09-23): @tube_ruled@ の **path 引数の次元** — 3 = [x,y,z] ・ 2 = [x,y] ・
 *   0 = 読めない (配列でない / 要素が [pos,r] でない / 位置が 1 要素以下)。
 *
 *   ---- なぜ要るのか ----
 *   @tube_ruled(path, segs)@ は path 頂点の位置の長さで **3D 掃引立体 / 2D 帯**を作り分ける。
 *   作り分け自体は op が正しくやっていたが、結果が名乗る型は @sig_dispatch@ が
 *   *入力型で先に当たった sigline* から採る。この op は **幾何入力を持たない** (path も segs も
 *   AK_INLINE の値) ので、"->cg-mesh3d;->cg-cross2d" と並べると照合すべき入力型が無く
 *   **必ず先頭が勝つ** ⇒ 2D の結果まで cg-mesh3d を名乗り、@extrude@ と 2D ブールが拒んだ (#3588)。
 *   ⇒ 行を出力型ごとに分け、この述語で振り分ける (import と同じ形)。
 *
 *   ⚠⚠ **判定規則は op 本体と同じでなければならない** — 「**先頭頂点の位置の長さ**で確定・
 *     3 以上なら 3D」。片方だけ変えると *routing と計算で次元が食い違い*、型スタンプだけが
 *     嘘になる = #3588 と同じ形の再発になる。変えるときは必ず両方:
 *       @modules/cgal/c++/cgaTube.cpp@ / @modules/manifold/c++/mfaTube.cpp@ の「先頭頂点で次元を確定」
 *   ⚠ 純粋 — 値を読むだけ。⚠ **配列の要素は eager に解決されない**ので、要素ごとに口を通す
 *     (@obt_array@ はゲートウェイなので、通せば待つ)。 */
inline int
pig_val_path_dim(sPtr<pigData> arg)
{
	if ( arg == thNULL || arg->is_error() ) return 0;
	sPtr<pigDataArray> path = arg->obt_array();
	if ( ! path.is_notNull() || path->length() < 1 ) return 0;
	sPtr<pigDataArray> pr = path->get_ix(thNEW(pigDataInteger,((INTEGER64)0)))->obt_array();
	if ( ! pr.is_notNull() || pr->length() < 2 ) return 0;     /* 要素が [pos, r] でない */
	sPtr<pigDataArray> pos = pr->get_ix(thNEW(pigDataInteger,((INTEGER64)0)))->obt_array();
	if ( ! pos.is_notNull() ) return 0;
	int pl = pos->length();
	return ( pl >= 3 ) ? 3 : ( ( pl >= 2 ) ? 2 : 0 );
}

/* ★ @tube_ruled@ の行を **path の次元**で選ぶ (第 1 引数を見る)。
 *
 *   ⚠⚠ **3D 側が catch-all** である (「2D でない」で受ける) のは意図的:
 *     壊れた path を *どの行も受けない* ようにすると、利用者に出るのは
 *     「この op を実行できるモジュールが無い」という **routing の文言**になり、
 *     op が持っている具体的な診断 ("each vertex must be [pos, r]" ・
 *     "vertex position must be [x,y]" 等) に **届かなくなる**。
 *     ⇒ 絞るのは 2D 側だけにして、残りは 3D 側が引き受け、診断は op に任せる。
 *   ⚠ したがって記述子では **2D の行を先に置く** — 逆にすると catch-all が先勝ちして
 *     2D の行が永久に選ばれない (#3554 段1 と同じ罠)。ロード時検査はこの順序までは見ない
 *     (マッチ関数どうしの重なりは静的に判定できない)。 */
inline int
pig_match_path_is_2d(const srava_module_descriptor *d, const pigOpEntry *e, int argNo, sPtr<pigData> arg)
{
	(void)d; (void)e;
	if ( argNo != 0 ) return 1;                 /* path は第 1 引数 */
	return ( pig_val_path_dim(arg) == 2 ) ? 1 : 0;
}

inline int
pig_match_path_is_3d(const srava_module_descriptor *d, const pigOpEntry *e, int argNo, sPtr<pigData> arg)
{
	(void)d; (void)e;
	if ( argNo != 0 ) return 1;
	return ( pig_val_path_dim(arg) == 2 ) ? 0 : 1;   /* 2D 以外 = 3D と「読めない」を受ける */
}

/* ★ 値 (3x4 / 4x4 の行列) が **z=0 平面を平面へ写すか**。
 *   1 = 平面に留まる (2D のまま扱える) / 0 = 面外へ出る・読めない・特異。
 *
 *   ⚠ 判定本体は @srava_affine::keeps_z_plane@ — *線形部と平行移動それぞれの大きさに対する
 *     相対 1e-12*。厳密な 0 と比べないのは @rotate("x",180)@ の m21 = sin(π) = 1.2e-16 が
 *     **正当な平面→平面の変換**だから (affine.h の実測コメントを参照)。
 *   ⚠ **行列しか見ない**。値の枠 (どの平面に置かれているか) は見ないので、これで救えるのは
 *     枠が既定 (z=0) と決まっている @cross2d@ だけ。@face3d@ は枠が任意なので行列だけでは決まらない
 *     (#3554 の「この機能で救えないもの」)。 */
inline int
pig_val_keeps_xy(sPtr<pigData> arg)
{
	double e[12];
	const char *why = 0;
	char buf[256];
	if ( ! srava_affine::matrix_transform(arg, e, &why, buf, (int)sizeof buf) )
		return 0;                       /* 12/16 要素でない・特異 = マッチしない */
	return srava_affine::keeps_z_plane(e, "match", &why, buf, (int)sizeof buf);
}

/* ★ @translate(m, vec)@ の vec が **xy 内の移動か** (z 成分が 0)。
 *   行列を組んでから同じ述語で見るので、判定が translate 用に割れない。 */
inline int
pig_val_translate_keeps_xy(sPtr<pigData> vec)
{
	double e[12];
	const char *why = 0;
	char buf[256];
	if ( ! srava_affine::matrix_translate(vec, e, &why, buf, (int)sizeof buf) ) return 0;
	return srava_affine::keeps_z_plane(e, "match", &why, buf, (int)sizeof buf);
}

/* ★ @mirror(m, axis)@ の axis が **平面を保つか** (x / y / z 軸なら保つ・任意軸は面外)。 */
inline int
pig_val_mirror_keeps_xy(sPtr<pigData> axis)
{
	double e[12];
	const char *why = 0;
	char buf[256];
	if ( ! srava_affine::matrix_mirror(axis, e, &why, buf, (int)sizeof buf) ) return 0;
	return srava_affine::keeps_z_plane(e, "match", &why, buf, (int)sizeof buf);
}

/* ★ @scale(m, vec)@ が **平面を保つか**。@matrix_scale@ は対角行列しか作らないので
 *   構造上いつも保つ (z 行が (0,0,sz,0))。⚠ それでも *判定を書いておく* — 将来
 *   matrix_scale が変わったとき、ここが自動で追従する (「常に真」と決め打つと追従しない)。 */
inline int
pig_val_scale_keeps_xy(sPtr<pigData> vec)
{
	double e[12];
	const char *why = 0;
	char buf[256];
	if ( ! srava_affine::matrix_scale(vec, e, &why, buf, (int)sizeof buf) ) return 0;
	return srava_affine::keeps_z_plane(e, "match", &why, buf, (int)sizeof buf);
}

/* ★ @rotate(m, axis, deg)@ の軸が **z 軸か**。
 *   ⚠⚠ **保守的な判定**である。本当に知りたいのは「その回転が平面を保つか」だが、それは
 *     *軸と角度の両方*で決まるのに対し、マッチ関数は **引数を 1 個ずつしか見られない**
 *     (@int match(d,e,argNo,arg)@)。⇒ 角度に依らず平面を保つ *軸 z* だけを拾う。
 *   ⇒ @rotate(R,"x",180)@ は平面を保つ (2D では mirror("y") と同じ) が **救えない**。
 *     救うにはマッチ関数へ引数列を渡す設計が要る (#3554 で要相談)。 */
inline int
pig_val_axis_is_z(sPtr<pigData> axis)
{
	double u[3];
	const char *why = 0;
	char buf[256];
	if ( ! srava_affine::unit_vector(axis, "z", "match", "axis", u, &why, buf, (int)sizeof buf) )
		return 0;
	const double EPS = 1e-12;
	return ( std::fabs(u[0]) <= EPS && std::fabs(u[1]) <= EPS && std::fabs(std::fabs(u[2]) - 1.0) <= EPS ) ? 1 : 0;
}

/* ================================================================================
 * ★★ #3554 最後の段 (2026-09-19): cast / import / export の **特別扱いを無くす**ための述語。
 *
 *   この 3 つは routing の中に専用ブロックを持っていたが、どれも「*値の引数がどの行かを決める*」
 *   同じ形なので、**マッチ関数 + sig** の普通の検索に戻せる。
 *
 *   ★★ 根拠は **記述子の申告そのもの** (@d->export_exts@ / @d->import_exts@ / @e->sig@)。
 *     ⇒ 行名の @#@ の後ろを読む形にすると *宣言が 2 か所*になって矛盾しうるが、申告を見れば
 *       **CSV / sig だけが正**になり二重帳簿にならない (ひさ 2026-09-19)。
 *     ⇒ ロード時検査 (「export op を持つのに export_exts 未申告は拒否」) も **そのまま生きる**。
 * ================================================================================ */

/* 値 (path 文字列) から拡張子を取り出す ("/a/b.stl" → ".stl")。無ければ ""。 */
inline std::string
pig_val_path_ext(sPtr<pigData> arg)
{
	if ( arg == thNULL ) return std::string();
	sPtr<stdString> sp = arg->get_str();        /* ★ 値の口 = ゲートウェイなので待つ */
	if ( sp == thNULL ) return std::string();
	const char *p = sp->get_str();
	const char *dot = ( p != 0 ) ? ::strrchr(p, '.') : 0;
	return ( dot != 0 ) ? std::string(dot) : std::string();
}

/* ★ export: **第 1 引数の拡張子を、このモジュールが書けるか** (@d->export_exts@ が根拠)。
 *   ⚠ 出力は全部 @ref@ なので、行を拡張子ごとに分ける必要は無い (1 本のままでよい)。 */
inline int
pig_match_export_ext(const srava_module_descriptor *d, const pigOpEntry *e, int argNo, sPtr<pigData> arg)
{
	(void)e;
	if ( argNo != 0 ) return 1;                 /* path は第 1 引数 */
	if ( d == 0 ) return 0;
	std::string ext = pig_val_path_ext(arg);
	return ext.empty() ? 0 : ( csv_has_ext(d->export_exts, ext.c_str()) ? 1 : 0 );
}

/* ★ import: **拡張子が産む型 (型付き CSV) が、この行の sig の出力型か**。
 *   ⚠⚠ import は *出力型が拡張子で決まる* ので、sig だけでは決まらない
 *     (cgal の import sig は "->cg-mesh3d;->cg-cross2d;->cg-face3d" の 3 つ)。
 *     ⇒ **行を拡張子群ごとに分け**、各行の sig を 1 つの出力型にしておくこと。
 *       そうすれば *同じ 1 本のマッチ関数*が行ごとに別の答えを出す (@e->sig@ を見るため)。
 *   ★ これで「CSV は読めると言うのに sig がその型を産まない」= **記述子の嘘**がその場で外れる。 */
inline int
pig_match_import_ext(const srava_module_descriptor *d, const pigOpEntry *e, int argNo, sPtr<pigData> arg)
{
	if ( argNo != 0 ) return 1;                 /* path は第 1 引数 */
	if ( d == 0 || e == 0 ) return 0;
	std::string ext = pig_val_path_ext(arg);
	if ( ext.empty() ) return 0;
	std::string t = pig_ext_out_type(d->import_exts, ext.c_str());
	if ( t.empty() ) return 0;                  /* その拡張子を読めない (または無型の申告) */
	return pig_sig_produces(e->sig, t.c_str());
}

/* ★ cast: **第 1 引数が言う目標型が、この行の sig の出力型か**。
 *   ⇒ 旧 @sig_dispatch@ の @wantOut@ (cast 専用の第 5 引数) はこれに置き換わって撤去された
 *     (#3554 最後の段 2/5 ・ 2026-09-19)。
 *
 *   ⚠⚠ **cast の行は 1 行 1 出力型**でなければならない (例: cgal は @cast#cg-mesh3d@ /
 *     @cast#cg-cross2d@ / @cast#cg-face3d@ の 3 行)。1 行に出力型が 2 つあると:
 *       ここ (行の可否)      … どれかの sigline が目標型を産めば **成立**
 *       sig_dispatch (実型)  … *入力型で先に当たった* sigline の出力型を採る
 *     と **判定が 2 か所に割れ**、@cast("cg-cross2d", <cg-mesh3d>)@ が cgal に振られたうえで
 *     出力型 cg-mesh3d を名乗る = 要求と違う型が黙って返る。
 *   ⇒ この規約は記述子のロード時に @pig_descriptor_violation@ が弾く (書き忘れを実行時に
 *     発見させない)。★ 行の **名前** ではなく @e->sig@ が根拠であることは変わらない —
 *     @#@ の後ろは読んでいない (二重帳簿にしないため)。 */
inline int
pig_match_cast_target(const srava_module_descriptor *d, const pigOpEntry *e, int argNo, sPtr<pigData> arg)
{
	(void)d;
	if ( argNo != 0 ) return 1;                 /* 目標型名は第 1 引数 */
	if ( e == 0 || arg == thNULL ) return 0;
	sPtr<stdString> sp = arg->get_str();
	if ( sp == thNULL ) return 0;
	return pig_sig_produces(e->sig, sp->get_str());
}

#endif
