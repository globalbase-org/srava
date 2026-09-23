#ifndef PT_RANDOM_H
#define PT_RANDOM_H
/*
 * ptRandom — 擬似乱数の生成器と、rand 系 op の共通引数検査 (2026-09-21)。
 *
 * ★★★ この op 群の約束は **「同じ引数なら必ず同じ結果」** (ひさ 2026-09-21)。シードが省略
 *   できないのはそのための形で、「毎回違う値が欲しい」は *この op では言えない*。
 *   ⇒ 結果は (a, b, n, s) だけの関数。時刻・アドレス・スレッド数・実行順に依存しない。
 *
 * ⚠⚠ 「同じ」の範囲は **機械と OS をまたぐ**。srava は結果をキャッシュし、その鍵は引数の
 *   ハッシュなので、Linux で作った cache を macOS が HIT させた瞬間に *別の乱数列* が
 *   同じ鍵で通ってしまう。⇒ 標準ライブラリの分布器は使えない:
 *     std::mt19937 そのもの        … 規格が列を決めている (可搬)
 *     std::uniform_int_distribution … ⚠ **規格が写像を決めていない** (libstdc++ と libc++ で違う)
 *     std::uniform_real_distribution… ⚠ 同上
 *     std::random_device / time(0)  … ⚠ そもそも決定的でない
 *   ⇒ **生成器も写像も自前で持つ**。中身は uint64 の加減乗算とシフトだけなので、
 *     どの OS・どの標準ライブラリでも 1 ビット違わない。
 *
 * 生成器 = xoshiro256** (Blackman & Vigna)。状態 256bit・周期 2^256-1。
 *   種は splitmix64 で 4 語へ展開する (元論文の推奨。0 を入れても状態が全 0 にならない)。
 *   ★ 暗号用途ではない。ここが要求しているのは **再現性と一様性**だけ。
 *
 * 区間の約束 (ひさ 2026-09-21 決定):
 *   整数       [a, b] **閉**   … a と b の両方が出る。候補は b-a+1 個
 *   浮動小数点 [a, b) **半開** … 慣例どおり右端は出ない。[a,b) を割って使う側で端が重ならない
 *   ⚠ 非対称に見えるが、離散と連続では「端を含む」の意味が違う。両方 (a==b) のときは
 *     整数は a を返し、浮動小数点も a を返す (幅 0)。
 */
#include	"pig/c++/pigData.h"
#include	<stdint.h>

/* ---- 生成器 ------------------------------------------------------------------- */

class ptRandom {
public:
	explicit ptRandom(INTEGER64 seed) { reseed(seed); }
	void	reseed(INTEGER64 seed);

	uint64_t	next_u64();

	/* [a, b] 閉区間の整数。a <= b であること (検査は pt_rand_spec が済ませている)。
	 * ★ 偏りを残さない — 2 冪マスク + 棄却。剰余だと候補数が 2 冪でないとき先頭が濃くなる。 */
	INTEGER64	next_int(INTEGER64 a, INTEGER64 b);

	/* [a, b) 半開区間の浮動小数点。u = (x >> 11) / 2^53 ∈ [0,1) を a + (b-a)*u へ写す。
	 * ★ 53bit ちょうど取るのは double の仮数と同じ幅だから (端数の丸めで偏らせない)。 */
	double		next_flt(double a, double b);

	/* ★★ #3576: 平均 0 ・ 標準偏差 1 の正規乱数を **1 個**返す (Marsaglia polar)。
	 *
	 *   ⚠⚠ **1 呼び出しで 2 値できるが、2 つ目は捨てる**。持ち越すと「前に何回呼ばれたか」で
	 *     消費順が変わり、*同じ (中心, sigma, n, seed) が呼び出し文脈で違う結果*になる。
	 *     ⇒ 捨てる方が一様乱数を余分に食うが、**引数だけの関数**という約束を保てる。
	 *   ⚠ 棄却法なので **1 点あたりの一様乱数の消費数は一定でない**。それでも決定的
	 *     (同じ種なら同じ棄却列) なので約束は保たれる。 */
	double		next_gauss();

private:
	uint64_t	s_[4];
};

/* ---- rand 系 op の共通引数検査 -------------------------------------------------- */

/* 1 軸ぶんの区間。整数か浮動小数点かは **書かれた a と b の種別**で決まる (下記)。 */
struct ptRandAxis {
	int		is_int;   /* 1 = 整数乱数 / 0 = 浮動小数点乱数 */
	INTEGER64	ia, ib;   /* is_int のときの [a,b] */
	double		fa, fb;   /* そうでないときの [a,b) */
};

struct ptRandSpec {
	int		nd;        /* 軸の数。rand=1 / rand#pt2d=2 / rand#pt3d=3 (#3572) */
	ptRandAxis	ax[3];
	INTEGER64	n;         /* 生成する個数 (>= 0) */
	INTEGER64	seed;
};

/* rand(a,b,n,s) の 3 行 (rand / rand#pt2d / rand#pt3d) 共通の 4 引数検査。
 * ★ nd は **行が決めて渡す** — 行を選ぶのは第 1 引数の要素数 (pttsAgent.cpp の
 *   pt_match_rand_dim)。⇒ ここに来た時点で「a の軸数」は確定している。
 * ⚠⚠ **b の軸数はここで初めて見る** (マッチ関数は第 2 引数を見ない・ひさ 2026-09-22)。
 *   両方をマッチで見ると食い違いが「どの行も成立しない」に化け、*どこが悪いか言わない*
 *   文言になる。⇒ 食い違いは下の "a and b must each be an array of N numbers" が言う。
 *   nd == 1 … a, b は **スカラ**
 *   nd >= 2 … a, b は **長さ nd の配列** (軸ごとに独立に区間を与える)
 *
 * ★★ 整数か浮動小数点かの規則は **1 つだけ**で、それを軸ごとに適用する (ひさ 2026-09-21):
 *       その軸の a と b が **両方とも整数** なら整数乱数、**どちらか一方でも浮動小数点**なら
 *       浮動小数点乱数。
 *     ⇒ rand([0,0,0],[10,10,10],n,s) は整数格子上の点、
 *        rand([0,0,0.0],[10,10,10],n,s) は z 軸だけ連続。
 *     ⚠ 軸ごとに独立なのが肝 — 「1 軸でも float なら全軸 float」にすると、a を書き換えて
 *       いない軸の値が黙って動く。
 *
 * ⚠ n と s は **整数でなければならない**。浮動小数点を黙って切り捨てると 1.4 と 1.6 が同じ
 *   シードになり、「同じ引数なら同じ結果」の逆 (違う引数で同じ結果) が静かに起きる。
 *
 * 成功 1 / 失敗 0 (err に理由。モジュールに static を置かないため戻り値経由 = ptCloud と同じ)。 */
int	pt_rand_spec(sPtr<pigData> a, sPtr<pigData> b, sPtr<pigData> n, sPtr<pigData> s,
	             int nd, const char *opn, ptRandSpec &out, char *err, int errsz);

/* ★★★ #3576: **自前の自然対数**。
 *
 *   ⚠⚠ 正規乱数はどの作り方でも対数が要る (Box-Muller も Marsaglia polar も Ziggurat の裾も)。
 *     ところが @std::log@ は **C / C++ の規格が正しい丸めを要求していない** ので、
 *     glibc と Apple libm で **最後の 1 bit が違いうる**。⇒ このファイル冒頭の約束
 *     「どの OS・どの標準ライブラリでも 1 ビット違わない」が **正規乱数だけ崩れる**。
 *     @std::uniform_int_distribution@ を使わない理由とまったく同じ形なので、同じ扱いにする。
 *
 *   ★ 中身は **IEEE 754 が丸めを規定している演算だけ** (+ - * / と frexp)。
 *     @frexp@ は指数の取り出しなので **丸めが起きない** (値は厳密) ⇒ 可搬。
 *       log(x) = 2*atanh(z) + e*ln2 ・ z = (m-1)/(m+1) ・ m ∈ [1/√2, √2) ⇒ |z| <= 0.1716
 *     奇数次の級数を 12 項で打ち切る (z^24 ≈ 1e-19 < double の分解能)。
 *   ★ @sqrt@ は **IEEE 754 が正しい丸めを要求している**ので、そちらは libm のままでよい。
 *   ⚠ x <= 0 は呼び手が通さない (next_gauss の s は (0,1) に限られる)。0 以下なら 0 を返す。 */
double	pt_log_det(double x);

/* ---- rand_gaussian の引数検査 (#3576) ------------------------------------------ */

struct ptGaussSpec {
	int		nd;        /* 軸の数。rand_gaussian=1 / #pt2d=2 / #pt3d=3 */
	double		c[3];      /* 中心 (nd 個) */
	double		sigma;     /* ★ **各軸の**標準偏差 (下記) */
	INTEGER64	n;
	INTEGER64	seed;
};

/* rand_gaussian(center, sigma, n, seed) の 4 引数を検査して spec を組む。
 *   nd == 1 … center は **スカラ** / nd >= 2 … center は **長さ nd の配列**
 *
 * ★★ @sigma@ は **各軸の標準偏差** (ひさ 2026-09-22)。各軸が独立に N(0, sigma) に従う
 *   = 2D なら円形・3D なら球形の対称性を持つ等方ガウス。
 *   ⚠⚠ 「**距離の**標準偏差」ではない。2D の等方ガウスでは中心からの距離は Rayleigh 分布に
 *     従い、その標準偏差は sigma に**ならない** (平均 ≈1.253σ ・ 標準偏差 ≈0.655σ)。
 *   ⚠ 1 次元 (nd==1) では両者が一致するので、スカラ版だけ見ていると差が出ない。
 *   ⇒ 各軸で採った理由: スカラ版と連続 ・ 回転不変 ・ sigma→0 で中心へ収束。
 *
 * ⚠ sigma < 0 はエラー。sigma == 0 は **全点が中心** (退化だが定義できる) として通す。
 * ⚠ n と seed は **整数だけ** (rand と同じ理由)。
 *
 * 成功 1 / 失敗 0 (err に理由)。 */
int	pt_rand_gauss_spec(sPtr<pigData> c, sPtr<pigData> sigma, sPtr<pigData> n, sPtr<pigData> s,
	                   int nd, const char *opn, ptGaussSpec &out, char *err, int errsz);

#endif /* PT_RANDOM_H */
