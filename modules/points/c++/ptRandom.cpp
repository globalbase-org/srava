/*
 * ptRandom — 擬似乱数の生成器と rand 系 op の共通引数検査の実装 (2026-09-21)。
 *   設計の根拠 (なぜ標準ライブラリの分布器を使わないか・区間の約束) は pt/c++/ptRandom.h。
 *
 * ★ 外部ライブラリを持たない (uint64 の算術と double の 1 回の乗算だけ) ので、libsrava_pt に
 *   同居させる。cgal / geogram 側が同じ列を欲しがったとき、ここを呼べば列が一致する。
 */
#include	"pig/c++/pigData.h"
#include	"pt/c++/ptCloud.h"    /* pt_cloud_random — 点群を作る関数の一族はここに居る */
#include	"pt/c++/ptRandom.h"

#include	<stdio.h>
#include	<string.h>
#include	<math.h>

/* ---- 生成器 ------------------------------------------------------------------- */

/* splitmix64 — 種 1 語を独立性の高い 4 語へ展開する (xoshiro 論文の推奨手順)。
 * ★ これが無いと seed=0 と seed=1 の列が数十語にわたって似通う。 */
static inline uint64_t
sm64(uint64_t &x)
{
	uint64_t z = ( x += (uint64_t)0x9E3779B97F4A7C15ull );
	z = ( z ^ (z >> 30) ) * (uint64_t)0xBF58476D1CE4E5B9ull;
	z = ( z ^ (z >> 27) ) * (uint64_t)0x94D049BB133111EBull;
	return z ^ (z >> 31);
}

static inline uint64_t
rotl64(uint64_t x, int k)
{
	return (x << k) | (x >> (64 - k));
}

void
ptRandom::reseed(INTEGER64 seed)
{
	/* ★ 符号つきから符号なしへは **2 の補数の再解釈**で渡す (C++20 以降は処理系依存でない)。
	 *   -1 と 0xFFFFFFFFFFFFFFFF が同じ列になるだけで、どちらも決定的。 */
	uint64_t x = (uint64_t)seed;
	for ( int i = 0 ; i < 4 ; ++i )
		s_[i] = sm64(x);
	/* splitmix64 が 4 語すべて 0 を返すことはまず無いが、0 状態は不動点なので念のため。 */
	if ( ( s_[0] | s_[1] | s_[2] | s_[3] ) == 0 )
		s_[0] = (uint64_t)0x9E3779B97F4A7C15ull;
}

uint64_t
ptRandom::next_u64()
{
	const uint64_t r = rotl64(s_[1] * 5, 7) * 9;   /* xoshiro256** の出力段 */
	const uint64_t t = s_[1] << 17;
	s_[2] ^= s_[0];
	s_[3] ^= s_[1];
	s_[1] ^= s_[2];
	s_[0] ^= s_[3];
	s_[2] ^= t;
	s_[3] = rotl64(s_[3], 45);
	return r;
}

INTEGER64
ptRandom::next_int(INTEGER64 a, INTEGER64 b)
{
	/* 候補の個数。⚠ b - a は符号つきでは溢れうる (a=INT64_MIN, b=INT64_MAX) ので
	 *   **符号なしで引く**。その最大の場合だけ span が 0 に回り込み、それは「2^64 通り =
	 *   64bit 全域」を意味するので next_u64() をそのまま使う。 */
	uint64_t span = (uint64_t)b - (uint64_t)a + 1u;
	uint64_t v;
	if ( span == 0 ) {
		v = next_u64();
	} else {
		/* 2 冪マスク + 棄却。剰余 (% span) だと候補数が 2 冪でないとき先頭側が濃くなる。
		 * ★ 棄却率は最悪でも 1/2 未満なので、期待消費は 2 語以下。 */
		uint64_t mask = span - 1;
		mask |= mask >> 1;  mask |= mask >> 2;  mask |= mask >> 4;
		mask |= mask >> 8;  mask |= mask >> 16; mask |= mask >> 32;
		do {	v = next_u64() & mask;
		} while ( v >= span );
	}
	return (INTEGER64)( (uint64_t)a + v );
}

double
ptRandom::next_flt(double a, double b)
{
	/* u ∈ [0,1)。2^53 で割るので、u は double が [0,1) で表せる格子にちょうど乗る。 */
	const double u = (double)( next_u64() >> 11 ) * (1.0 / 9007199254740992.0);   /* 2^53 */
	return a + (b - a) * u;
}

/* ---- 自前の自然対数 (#3576) ------------------------------------------------------
 * ⚠⚠ なぜ std::log を使わないかは pt/c++/ptRandom.h の宣言コメント。
 *   要点: 規格が正しい丸めを要求していないので glibc と Apple libm で最後の 1 bit が違いうる。
 *   ⇒ rand 系の「どの OS でも 1 ビット違わない」が **正規乱数だけ崩れる**。
 * ★ ここは IEEE 754 が丸めを規定している演算 (+ - * /) と frexp (丸めが起きない) だけ。 */
double
pt_log_det(double x)
{
	if ( !(x > 0.0) ) return 0.0;         /* 呼び手が通さない。念のための保険 */

	int    e = 0;
	double m = ::frexp(x, &e);            /* x = m * 2^e ・ m ∈ [0.5, 1) ・ **厳密** */

	/* m を [1/√2, √2) へ寄せる ⇒ z = (m-1)/(m+1) の |z| が 0.1716 以下になり級数が速い。
	 * ★ 定数は 1/√2 に最も近い double。比較にしか使わないので丸めの影響は無い。 */
	if ( m < 0.70710678118654752440 ) { m *= 2.0; e -= 1; }

	const double z  = (m - 1.0) / (m + 1.0);
	const double z2 = z * z;

	/* log(m) = 2*atanh(z) = 2z * (1 + z²/3 + z⁴/5 + … )。
	 * ★ 12 項で打ち切る: |z²| <= 0.02945 なので z^24 ≈ 1e-19 < double の分解能 (2^-53 ≈ 1.1e-16)。
	 * ⚠ Horner は **小さい項から**畳む (最後に 1.0 を足す) — 丸めの累積を抑える。 */
	double t = 1.0 / 23.0;
	t = t * z2 + 1.0 / 21.0;   t = t * z2 + 1.0 / 19.0;   t = t * z2 + 1.0 / 17.0;
	t = t * z2 + 1.0 / 15.0;   t = t * z2 + 1.0 / 13.0;   t = t * z2 + 1.0 / 11.0;
	t = t * z2 + 1.0 /  9.0;   t = t * z2 + 1.0 /  7.0;   t = t * z2 + 1.0 /  5.0;
	t = t * z2 + 1.0 /  3.0;   t = t * z2 + 1.0;

	/* ln2 を **2 語に分けて**足す。⚠ e が大きいと e*ln2 が log(m) を飲み込むので、
	 *   上位を先に足すと下位が落ちる。⇒ 小さい方 (LO) から足す。 */
	const double LN2_HI = 6.93147180369123816490e-01;
	const double LN2_LO = 1.90821492927058770002e-10;
	return ( 2.0 * z * t + (double)e * LN2_LO ) + (double)e * LN2_HI;
}


/* ---- 正規乱数 (#3576) ------------------------------------------------------------
 * Marsaglia polar: 単位円内に一様な (u,v) を棄却で採り、s = u²+v² から
 *   f = sqrt(-2 ln s / s) ・ 返すのは u*f (v*f は **捨てる**)。
 * ★ sqrt は IEEE 754 が正しい丸めを要求しているので libm のままでよい。log だけ自前。
 * ⚠ 2 つ目を捨てる理由は宣言コメント (持ち越すと呼び出し文脈で消費順が変わる)。 */
double
ptRandom::next_gauss()
{
	for ( ;; ) {
		const double u = next_flt(-1.0, 1.0);
		const double v = next_flt(-1.0, 1.0);
		const double s = u * u + v * v;
		if ( s >= 1.0 || s == 0.0 ) continue;          /* 円の外 / 原点は棄却 */
		return u * ::sqrt( -2.0 * pt_log_det(s) / s );
	}
}


/* ---- 引数検査 ----------------------------------------------------------------- */

/* 配列の i 番目 (ptCloud.cpp の pt_at と同じ理由で get_ix 経由 = 遅延要素も通る)。 */
static sPtr<pigData>
at(sPtr<pigDataArray> a, int i)
{
	return a->get_ix(thNEW(pigDataInteger,((INTEGER64)i)));
}

/* 軸 k の [a,b] を読む。整数か浮動小数点かは **両方の種別**で決まる (ptRandom.h の規則)。 */
static int
axis_from(sPtr<pigData> av, sPtr<pigData> bv, int k, int nd,
          const char *opn, ptRandAxis &ax, char *err, int errsz)
{
	/* ⚠ **数でないものを黙って 0 にしない**。get_flt() は配列でも文字列でも 0 を返すので、
	 *   ここで断らないと [[0,1],[2,3]] や "abc" が黙って幅 0 の区間になる。
	 * ★ 「数か」は is_int() || is_flt() で訊く (pigData.h)。配列だけを弾く形だと
	 *   文字列・null・cache ハンドルが素通りする。 */
	if ( ! ( av->is_int() || av->is_flt() ) || ! ( bv->is_int() || bv->is_flt() ) ) {
		if ( nd == 1 )
			::snprintf(err, errsz, "%s: a and b must be numbers", opn);
		else
			::snprintf(err, errsz, "%s: a[%d] and b[%d] must be numbers", opn, k, k);
		return 0;
	}
	ax.is_int = ( av->is_int() && bv->is_int() ) ? 1 : 0;
	if ( ax.is_int ) {
		ax.ia = av->get_int();
		ax.ib = bv->get_int();
		ax.fa = (double)ax.ia;
		ax.fb = (double)ax.ib;
		if ( ax.ia > ax.ib ) {
			if ( nd == 1 )
				::snprintf(err, errsz, "%s: the range is empty (a must not be greater than b)", opn);
			else
				::snprintf(err, errsz, "%s: the range of axis %d is empty"
				                       " (a[%d] must not be greater than b[%d])", opn, k, k, k);
			return 0;
		}
		return 1;
	}
	ax.ia = 0;
	ax.ib = 0;
	ax.fa = av->get_flt();
	ax.fb = bv->get_flt();
	/* ⚠ NaN は比較が全部偽なので、`fa > fb` では捕まらない。自分自身との比較で名指しする。 */
	if ( ax.fa != ax.fa || ax.fb != ax.fb ) {
		if ( nd == 1 )
			::snprintf(err, errsz, "%s: a and b must be finite numbers (got NaN)", opn);
		else
			::snprintf(err, errsz, "%s: a[%d] and b[%d] must be finite numbers (got NaN)", opn, k, k);
		return 0;
	}
	if ( ax.fa > ax.fb ) {
		if ( nd == 1 )
			::snprintf(err, errsz, "%s: the range is empty (a must not be greater than b)", opn);
		else
			::snprintf(err, errsz, "%s: the range of axis %d is empty"
			                       " (a[%d] must not be greater than b[%d])", opn, k, k, k);
		return 0;
	}
	return 1;
}

int
pt_rand_spec(sPtr<pigData> a, sPtr<pigData> b, sPtr<pigData> n, sPtr<pigData> s,
             int nd, const char *opn, ptRandSpec &out, char *err, int errsz)
{
	::memset(&out, 0, sizeof out);
	out.nd = nd;

	if ( ! a.is_notNull() || ! b.is_notNull() || ! n.is_notNull() || ! s.is_notNull() ) {
		/* ★ 個数そのものの検査は記述子 (nin=4・nreq=0) が済ませている。ここは配線の保険。 */
		::snprintf(err, errsz, "%s: needs (a, b, n, seed)", opn);
		return 0;
	}

	if ( nd == 1 ) {
		if ( ! axis_from(a, b, 0, nd, opn, out.ax[0], err, errsz) )
			return 0;
	} else {
		sPtr<pigDataArray> aa = a->obt_array();
		sPtr<pigDataArray> ba = b->obt_array();
		if ( ! aa.is_notNull() || ! ba.is_notNull() || aa->length() != nd || ba->length() != nd ) {
			::snprintf(err, errsz, "%s: a and b must each be an array of %d numbers"
			                       " (one interval per axis)", opn, nd);
			return 0;
		}
		for ( int k = 0 ; k < nd ; ++k )
			if ( ! axis_from(at(aa, k), at(ba, k), k, nd, opn, out.ax[k], err, errsz) )
				return 0;
	}

	/* ⚠ n と seed は整数だけ。浮動小数点を切り捨てると **違う引数が同じ結果**になる
	 *   (1.4 と 1.6 が同じシード)。「同じ引数なら同じ結果」の裏返しなので黙って通さない。 */
	if ( ! n->is_int() ) {
		::snprintf(err, errsz, "%s: n must be an integer", opn);
		return 0;
	}
	if ( ! s->is_int() ) {
		::snprintf(err, errsz, "%s: the seed must be an integer", opn);
		return 0;
	}
	out.n    = n->get_int();
	out.seed = s->get_int();
	if ( out.n < 0 ) {
		::snprintf(err, errsz, "%s: n must not be negative (got %lld)", opn, (long long)out.n);
		return 0;
	}
	return 1;
}

/* ---- 点群を組む (rand#pt2d / rand#pt3d の中身) ---------------------------------- */

sPtr<ptCloud>
pt_cloud_random(sPtr<pigData> a, sPtr<pigData> b, sPtr<pigData> n, sPtr<pigData> s,
                int dim, const char *opn, char *err, int errsz)
{
	ptRandSpec sp;
	if ( ! pt_rand_spec(a, b, n, s, dim, opn, sp, err, errsz) )
		return sPtr<ptCloud>();

	sPtr<ptCloud> pc = thNEW(ptCloud,());
	pc->set_dim(dim);
	std::vector<double> &X = pc->xyz();
	X.reserve((size_t)sp.n * (size_t)dim);

	/* ★ 並びは **点ごとに x, y(, z)**。列の消費順そのものが結果の一部なので、ここを変えると
	 *   同じ (a,b,n,s) が違う点群になる = キャッシュを跨いで結果が変わる。変えるときは
	 *   points の cache_version を上げること。
	 * ⚠ 整数軸は棄却でくじを余分に引くことがある (ptRandom::next_int)。⇒ ある軸の値は
	 *   **前の軸の区間にも依存する**。これも決定的だが「軸ごとに独立な列」ではない。 */
	ptRandom rng(sp.seed);
	for ( INTEGER64 i = 0 ; i < sp.n ; ++i )
		for ( int k = 0 ; k < dim ; ++k ) {
			const ptRandAxis &ax = sp.ax[k];
			X.push_back(ax.is_int ? (double)rng.next_int(ax.ia, ax.ib)
			                      : rng.next_flt(ax.fa, ax.fb));
		}
	return pc;   /* n == 0 は空の点群 (valid(p)=0 がそれを言う) */
}


/* ---- rand_gaussian の引数検査 (#3576) ------------------------------------------- */

int
pt_rand_gauss_spec(sPtr<pigData> c, sPtr<pigData> sigma, sPtr<pigData> n, sPtr<pigData> s,
                   int nd, const char *opn, ptGaussSpec &out, char *err, int errsz)
{
	::memset(&out, 0, sizeof out);
	out.nd = nd;

	if ( ! c.is_notNull() || ! sigma.is_notNull() || ! n.is_notNull() || ! s.is_notNull() ) {
		/* ★ 個数そのものの検査は記述子 (nin=4 / nreq 無し) が済ませている。ここは配線の保険。 */
		::snprintf(err, errsz, "%s: needs (center, sigma, n, seed)", opn);
		return 0;
	}

	/* ---- 中心 ---- */
	if ( nd == 1 ) {
		if ( ! c->is_int() && ! c->is_flt() ) {
			::snprintf(err, errsz, "%s: the center must be a number", opn);
			return 0;
		}
		out.c[0] = c->get_flt();
	} else {
		sPtr<pigDataArray> ca = c->obt_array();
		if ( ! ca.is_notNull() || ca->length() != nd ) {
			/* ⚠ 行は **第 1 引数の要素数**で決まっているので、ここへ来るのは
			 *   「配列でない」か「compact して数が変わった」形だけ。念のため言う。 */
			::snprintf(err, errsz, "%s: the center must be an array of %d numbers"
			                       " (one per axis)", opn, nd);
			return 0;
		}
		for ( int k = 0 ; k < nd ; ++k ) {
			sPtr<pigData> e = ca->get_ix(thNEW(pigDataInteger,((INTEGER64)k)));
			if ( ! e.is_notNull() || ( ! e->is_int() && ! e->is_flt() ) ) {
				::snprintf(err, errsz, "%s: the center of axis %d must be a number", opn, k);
				return 0;
			}
			out.c[k] = e->get_flt();
		}
	}

	/* ---- sigma ---- */
	if ( ! sigma->is_int() && ! sigma->is_flt() ) {
		::snprintf(err, errsz, "%s: sigma must be a number", opn);
		return 0;
	}
	out.sigma = sigma->get_flt();
	/* ⚠ sigma == 0 は **全点が中心**。退化だが定義でき、σ→0 の極限と一致するので通す。
	 *   負は意味が無いので明示エラー (黙って |sigma| にすると書き間違いが消える)。 */
	if ( out.sigma < 0.0 ) {
		::snprintf(err, errsz, "%s: sigma must not be negative (got %g)", opn, out.sigma);
		return 0;
	}

	/* ---- n と seed ---- (rand と同じ規約: 整数だけ) ---- */
	if ( ! n->is_int() ) { ::snprintf(err, errsz, "%s: n must be an integer", opn); return 0; }
	if ( ! s->is_int() ) { ::snprintf(err, errsz, "%s: the seed must be an integer", opn); return 0; }
	out.n    = n->get_int();
	out.seed = s->get_int();
	if ( out.n < 0 ) {
		::snprintf(err, errsz, "%s: n must not be negative (got %lld)", opn, (long long)out.n);
		return 0;
	}
	return 1;
}


/* ---- 正規分布で点群を組む (rand_gaussian#pt2d / #pt3d の中身) --------------------- */

sPtr<ptCloud>
pt_cloud_random_gauss(sPtr<pigData> c, sPtr<pigData> sigma, sPtr<pigData> n, sPtr<pigData> s,
                      int dim, const char *opn, char *err, int errsz)
{
	ptGaussSpec sp;
	if ( ! pt_rand_gauss_spec(c, sigma, n, s, dim, opn, sp, err, errsz) )
		return sPtr<ptCloud>();

	sPtr<ptCloud> pc = thNEW(ptCloud,());
	pc->set_dim(dim);
	std::vector<double> &X = pc->xyz();
	X.reserve((size_t)sp.n * (size_t)dim);

	/* ★ 並びは一様版と同じく **点ごとに x, y(, z)**。消費順そのものが結果の一部なので、
	 *   変えるときは points の cache_version を上げること。
	 * ⚠ next_gauss は棄却法なので **1 軸あたりの一様乱数の消費数は一定でない**。
	 *   決定的ではある (同じ種なら同じ棄却列) が、「軸ごとに独立な列」ではない
	 *   — 一様版の整数軸と同じ性質。 */
	ptRandom rng(sp.seed);
	for ( INTEGER64 i = 0 ; i < sp.n ; ++i )
		for ( int k = 0 ; k < dim ; ++k )
			X.push_back( sp.c[k] + sp.sigma * rng.next_gauss() );
	return pc;   /* n == 0 は空の点群 */
}


/* ---- 混合分布から点群を組む (#3577) ---------------------------------------------- */

sPtr<ptCloud>
pt_cloud_random_gauss_mix(sPtr<ptCloud> in, sPtr<pigData> sigma,
                          sPtr<pigData> n, sPtr<pigData> s,
                          const char *opn, char *err, int errsz)
{
	if ( ! in.is_notNull() ) {
		::snprintf(err, errsz, "%s: needs a point cloud as the first argument", opn);
		return sPtr<ptCloud>();
	}
	const int dim = in->dim();
	const int K   = in->np();
	if ( dim != 2 && dim != 3 ) {
		::snprintf(err, errsz, "%s: the point cloud must be 2D or 3D", opn);
		return sPtr<ptCloud>();
	}
	/* ⚠ 中心が 1 つも無いと **分布そのものが定義できない**。0 点の点群を黙って返すと
	 *   「n 点ほしいと言ったのに 0 点」が静かに通るので、明示エラーにする。 */
	if ( K <= 0 ) {
		::snprintf(err, errsz, "%s: the point cloud is empty — there is no center to draw from", opn);
		return sPtr<ptCloud>();
	}

	/* 中心は入力点群が持っているので、spec は sigma / n / seed だけを見る。
	 * ★ 中心の検査 (配列の長さ等) は要らない ⇒ ダミーの中心 0 を渡して使い回す。 */
	ptGaussSpec sp;
	{
		sPtr<pigData> zero = thNEW(pigDataFloat,(0.0));
		if ( ! pt_rand_gauss_spec(zero, sigma, n, s, 1, opn, sp, err, errsz) )
			return sPtr<ptCloud>();
	}

	sPtr<ptCloud> pc = thNEW(ptCloud,());
	pc->set_dim(dim);
	const std::vector<double> &C = in->xyz();
	std::vector<double> &X = pc->xyz();
	X.reserve((size_t)sp.n * (size_t)dim);

	/* ★★ 「重ね合わせてから n 点」= 混合分布からの標本。
	 *   n 回: **中心を一様に 1 つ選び**、そこから N(0, sigma) を 1 点。
	 *   ⇒ 「各入力点に n/K 点ずつ」でも「各点に n 点ずつ」でもない。
	 * ⚠ 消費順は **点ごとに [中心のくじ → 各軸のガウス]**。ここを変えると同じ引数が
	 *   違う点群になる ⇒ 変えるときは points の cache_version を上げること。
	 * ⚠ 中心のくじ (next_int) も軸のガウス (next_gauss) も **同じ 1 本の列**から引く。
	 *   ⇒ 入力点群の **順序**が結果に効く (どの中心が何番かで当たりが変わる)。 */
	ptRandom rng(sp.seed);
	for ( INTEGER64 i = 0 ; i < sp.n ; ++i ) {
		const INTEGER64 k = rng.next_int(0, (INTEGER64)K - 1);
		for ( int a = 0 ; a < dim ; ++a )
			X.push_back( C[(size_t)k * (size_t)dim + (size_t)a] + sp.sigma * rng.next_gauss() );
	}
	return pc;   /* n == 0 は空の点群 (K > 0 は確かめてある) */
}
