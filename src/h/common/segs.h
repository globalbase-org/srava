#ifndef ___common_segs_h___
#define ___common_segs_h___

/*
 * segs.h — 分割数 (segs) / 辺数 (n) の **共通検査** (ヘッダオンリー・カーネル非依存)。
 *
 * ★★ #3530: 同じ `if ( segs < 3 )` を実装ごとに **逆の意味**で読んでいた。
 *   cgaCircle は「3 にクランプ」・mfCross は「既定値 32 へ」。⇒ circle(1,0) が
 *   cgal では三角形・manifold では 32 角形になり、**エラーも警告も無く** 体積が 2.4 倍
 *   違う答えを返していた (#3516 で潰した「黙って誤値」の型)。
 *   起票時に数えたら **4 通りの規約が併存**していた (sphere 族 / revolve / ngon 族 /
 *   circle が cgal と manifold で別)。
 *
 * ---- ★★ 決めた規則 (2026-09-14 ひさ・案 C) ----
 *
 *   segs = **近似の細かさ**。既定値を持つ。
 *       0 (または省略) → 既定値 (op ごと。多くは 32)
 *       1 または 2     → **明示エラー**
 *       負             → **明示エラー**   ⚠ 以前は黙って既定値に落ちていた
 *       3 以上         → その値
 *
 *   n = **形そのもの** (ngon / prism / pyramid)。既定値を持たない。
 *       3 未満 (0 を含む) → **明示エラー**   ← 従来どおり・変更なし
 *
 *   ★ この規則なら「なぜ ngon(0) はエラーで circle(1,0) は 32 角形なのか」が 1 行で
 *     説明できる: n は形そのものなので既定値が無く、segs は近似の細かさなので既定値がある。
 *
 * ---- ★★ 判定は「引数の **意味**」で決める。nreq では決めない ----
 *
 *   決定時の文言は「**省略できる引数だけが 0 = 未指定を受ける**」だったが、これは
 *   *代理* であって実体ではない。実体は上の「既定値を持つ引数か」。分かれるのが openvdb:
 *
 *       openvdb   cylinder(r, h, segs, dx)   末尾の dx (ボクセルサイズ) が **必須**
 *                                            ⇒ segs は位置的に省略できない
 *                                            ⇒ しかし意味は同じ「近似の細かさ」
 *
 *   「nreq 以降か」で判定すると openvdb の cylinder(1,1,0,0.05) だけがエラーになり、
 *   **新しい食い違いを作る**。⇒ segs という引数はどこに居ても check_segs() を通す。
 *   ⚠ 同型の失敗が過去に何度もある (代理を見ると条件を 1 つ増やした瞬間に黙って誤答)。
 *
 * ---- ★ occt も検査だけは行う ----
 *
 *   occt は segs を **無視する** (球も円も厳密な解析曲面なので分割数に意味が無い) が、
 *   検査しないと **同じ式が cg/mf では落ちて occt では通る**という新しい食い違いになる。
 *   ⇒ 値を使わなくても契約は同じ、という扱いにする。
 *
 * ---- 使い方 ----
 *
 *       int segs_in = ( na > 1 ) ? (int)(*args)[1]->get_int() : 0;   / * 0 = 未指定 * /
 *       int segs = 0;
 *       if ( srava_geo::check_segs(segs_in, 32, &segs) != srava_geo::SEGS_OK ) {
 *               result = cga_err(thNEW(stdString,(
 *                   srava_geo::segs_error("circle").c_str()))); return;
 *       }
 *
 *   ⚠ 新しく segs を取る op を足したらここを通すこと。忘れると
 *     **srava_segs_coverage** が赤くなって名指しする (test/srava_segs_coverage.sh)。
 */

#include <string>

namespace srava_geo {

enum {
	SEGS_OK        = 0,
	SEGS_ERR_RANGE = 1
};

/* 上限。tube / 掃引系が元から持っていた黙ったクランプを 1 箇所へ集めた。
 * ⚠ ここは **エラーにしない** — 上限は「これ以上細かくしても意味が無い」という実装都合で
 *   あって契約ではないため (下限は契約: 3 未満は多角形にならない)。 */
const int SEGS_MAX = 4096;

/* segs (近似の細かさ・既定値あり)。*out に解決後の値を書く。 */
inline int
check_segs(int segs, int dflt, int *out)
{
	if ( segs == 0 ) { *out = dflt; return SEGS_OK; }   /* 省略 と同じ */
	if ( segs < 3 )  { return SEGS_ERR_RANGE; }         /* 1 / 2 / 負 */
	*out = ( segs > SEGS_MAX ) ? SEGS_MAX : segs;
	return SEGS_OK;
}

/* n (形そのもの・既定値なし)。0 もエラー。 */
inline int
check_sides(int n, int *out)
{
	if ( n < 3 ) return SEGS_ERR_RANGE;
	*out = ( n > SEGS_MAX ) ? SEGS_MAX : n;
	return SEGS_OK;
}

/* エラー文言。★ 利用者の目に出る文字列なので英語 (コメントは日本語のまま)。
 * ★ 文言も 1 箇所に置く — 検査だけ共通にして文言を配ると、同じ誤りに別の説明が付く。 */
inline std::string
segs_error(const char *op)
{
	return std::string(op) + ": segments must be >= 3 (use 0 or omit it for the default)";
}

inline std::string
sides_error(const char *op)
{
	return std::string(op) + ": n must be >= 3";
}

}  /* namespace srava_geo */

#endif
