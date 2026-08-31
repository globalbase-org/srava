#ifndef ___common_geodesic_h___
#define ___common_geodesic_h___

/*
 * geodesic.h — 測地球 (geodesic sphere) の共通生成器 (ヘッダオンリー・カーネル非依存)。
 *
 * cgal.so と manifold.so の **両方** が同じこのヘッダを include し、同一アルゴリズムで球を
 * 生成する。狙いは「sphere / icosphere の頂点座標・面の並びをカーネル間で完全一致させ、
 * 体積を数値誤差レベルで一致させる」こと (2026-08-11 ひさ設計)。
 *
 * 種多面体を線形分割 (barycentric grid) して球面へ投影する:
 *   - sphere(r, seg)      = geodesic(SEED_OCTAHEDRON,  n=(seg+3)/4, r)   … 面数 8·n²
 *   - icosphere(r, subdiv)= geodesic(SEED_ICOSAHEDRON, n=2^subdiv,  r)   … 面数 20·n²
 * n は「種の 1 辺の分割数」。Manifold::Sphere の circularSegments→n 変換 (n=(seg+3)/4) に合わせた。
 *
 * ★カーネル間 bit 一致の根拠: 共有辺 AB 上の格子点は、隣接する 2 つの種三角形のどちらから
 *   計算しても `((n-i)·A + i·B)/n` の **同じ加算**になる (i↔n-i で対応・浮動小数加算は可換)。
 *   よって投影後の座標が bit 一致し、座標キーの重複排除で確実に 1 頂点へ統合される。
 *
 * 使い方 (Sink は各カーネルが用意する):
 *   struct Sink { int add_vertex(double x,double y,double z); void add_triangle(int a,int b,int c); };
 *   srava_geo::make_geodesic(srava_geo::SEED_ICOSAHEDRON, n, r, sink);
 * 頂点の重複排除はこのヘッダ内で行うので、Sink::add_vertex は「新頂点を足して index を返す」だけでよい。
 */

#include <map>
#include <cmath>

namespace srava_geo {

enum SeedKind { SEED_OCTAHEDRON = 0, SEED_ICOSAHEDRON = 1 };

/* ---- 種多面体 (単位球に載る頂点・CCW 外向きの三角形) ---- */

/* 正八面体: 6 頂点 (±軸) / 8 面。 */
static const double OCTA_V[6][3] = {
	{ 1, 0, 0}, {-1, 0, 0}, { 0, 1, 0}, { 0,-1, 0}, { 0, 0, 1}, { 0, 0,-1}
};
static const int OCTA_F[8][3] = {
	{4,0,2},{4,2,1},{4,1,3},{4,3,0},   /* +z 半球 (CCW 外向き) */
	{5,2,0},{5,1,2},{5,3,1},{5,0,3}    /* -z 半球 */
};

/* 正二十面体: 12 頂点 / 20 面 (Andreas Kähler の定番並び・CCW 外向き)。
 * 頂点は (0,±1,±t)/(±1,±t,0)/(±t,0,±1) を正規化して単位球へ。t=黄金比。 */
static const int ICOSA_F[20][3] = {
	{0,11, 5},{0, 5, 1},{0, 1, 7},{0, 7,10},{0,10,11},
	{1, 5, 9},{5,11, 4},{11,10,2},{10,7, 6},{7, 1, 8},
	{3, 9, 4},{3, 4, 2},{3, 2, 6},{3, 6, 8},{3, 8, 9},
	{4, 9, 5},{2, 4,11},{6, 2,10},{8, 6, 7},{9, 8, 1}
};

/* ICOSA の頂点を単位球で埋める (t は実行時に計算するので関数で返す)。 */
inline void icosa_vertices(double V[12][3]) {
	const double t = (1.0 + std::sqrt(5.0)) / 2.0;
	const double s = 1.0 / std::sqrt(1.0 + t * t);   /* 正規化係数 (全頂点で共通) */
	const double a = s, b = t * s;
	const double raw[12][3] = {
		{-a, b, 0},{ a, b, 0},{-a,-b, 0},{ a,-b, 0},
		{ 0,-a, b},{ 0, a, b},{ 0,-a,-b},{ 0, a,-b},
		{ b, 0,-a},{ b, 0, a},{-b, 0,-a},{-b, 0, a}
	};
	for (int i = 0; i < 12; ++i) { V[i][0]=raw[i][0]; V[i][1]=raw[i][1]; V[i][2]=raw[i][2]; }
}

/* ---- 生成本体 ---- */

template<class Sink>
void make_geodesic(int seed, int n, double r, Sink& sink) {
	if (n < 1) n = 1;

	/* 種の頂点表・面表を選ぶ。 */
	double OV[12][3];
	const int (*F)[3];
	int nf, nv;
	if (seed == SEED_ICOSAHEDRON) {
		icosa_vertices(OV);
		F = ICOSA_F; nf = 20; nv = 12;
	} else {
		for (int i = 0; i < 6; ++i) { OV[i][0]=OCTA_V[i][0]; OV[i][1]=OCTA_V[i][1]; OV[i][2]=OCTA_V[i][2]; }
		F = OCTA_F; nf = 8; nv = 6;
	}
	(void)nv;

	/* 座標キーで頂点を重複排除。
	 * ★ キーは double を **float へ落として**量子化する (2026-08-14)。旧実装は「共有辺の格子点は
	 *   隣接面でも同一式で計算されるので bit 一致する」前提で double をそのままキーにしていたが、
	 *   Apple clang (arm64) は既定 -ffp-contract=on で FMA 縮約するため同値のはずの格子点が
	 *   1ulp ずれ、溶接漏れが起きた (icosphere(1,2) が 162v のはずが 170v・非多様体になり
	 *   manifold が volume=0 を返す。x86 gcc では bit 一致していたので露見しなかった)。
	 *   格子点の間隔は O(r/n) で float の相対精度 (~6e-8) より桁違いに大きいので、float 量子化は
	 *   1ulp ジッタだけを吸収し、異なる格子点を誤って併合することはない。頂点の**値**は double の
	 *   まま (キーだけ量子化 = 出力座標は不変)。 */
	typedef std::pair<float, std::pair<float, float> > Key;
	std::map<Key, int> dedup;

	/* 単位球へ投影して r 倍 → Sink に追加 (既出なら既存 index)。 */
	// clang-format off
	#define GEO_EMIT(px,py,pz, outid) do {                                  \
		double _len = std::sqrt((px)*(px) + (py)*(py) + (pz)*(pz));         \
		double _s = (_len > 0) ? (r / _len) : 0;                            \
		double _x = (px)*_s, _y = (py)*_s, _z = (pz)*_s;                    \
		Key _k((float)_x, std::pair<float,float>((float)_y, (float)_z));    \
		std::map<Key,int>::iterator _it = dedup.find(_k);                   \
		if (_it != dedup.end()) { (outid) = _it->second; }                  \
		else { (outid) = sink.add_vertex(_x, _y, _z); dedup[_k] = (outid); }\
	} while (0)
	// clang-format on

	for (int f = 0; f < nf; ++f) {
		const double *A = OV[F[f][0]];
		const double *B = OV[F[f][1]];
		const double *C = OV[F[f][2]];

		/* 種三角形 (A,B,C) を n 分割。格子点 P(i,j) = ((n-i-j)·A + i·B + j·C)/n。
		 * row i (i=0..n) は j=0..n-i の (n-i+1) 頂点を持つ。i を outer, j を inner に取り、
		 * 隣接三角形と共有する格子点キーが bit 一致するよう **B,C の係数を i,j に固定**する。 */
		/* この面の格子頂点 index を一時保持 (行ごとに使い捨て・最大 (n+1) 個)。 */
		/* 2 行分だけ持てば十分だが、可読性優先で (n+1)×(n+1) 相当を都度 EMIT で引く。 */
		for (int i = 0; i < n; ++i) {
			for (int j = 0; j < n - i; ++j) {
				/* 4 隅の格子点 (投影後) を作り、上向き三角形と (あれば) 下向き三角形を張る。 */
				int v00, v10, v01, v11;
				double inv = 1.0 / (double)n;
				#define GEO_P(ii,jj, id) do {                                          \
					double _ka = (double)(n-(ii)-(jj)) * inv;                          \
					double _kb = (double)(ii) * inv;                                   \
					double _kc = (double)(jj) * inv;                                   \
					double _px = _ka*A[0] + _kb*B[0] + _kc*C[0];                       \
					double _py = _ka*A[1] + _kb*B[1] + _kc*C[1];                       \
					double _pz = _ka*A[2] + _kb*B[2] + _kc*C[2];                       \
					GEO_EMIT(_px,_py,_pz, id);                                         \
				} while (0)
				GEO_P(i,   j,   v00);
				GEO_P(i+1, j,   v10);
				GEO_P(i,   j+1, v01);
				sink.add_triangle(v00, v10, v01);        /* 上向き (種の CCW を継承) */
				if (j < n - i - 1) {
					GEO_P(i+1, j+1, v11);
					sink.add_triangle(v10, v11, v01);    /* 下向き */
				}
				#undef GEO_P
			}
		}
	}
	#undef GEO_EMIT
}

/* seg (円周分割数) → n (種 1 辺の分割数)。Manifold::Sphere と同式。既定 seg は呼び出し側が渡す。 */
inline int seg_to_n(int seg) { return (seg > 0) ? (seg + 3) / 4 : 8; }   /* 既定 seg=32 相当 = n=8 */

/* subdiv (細分回数) → n。n = 2^subdiv (面数は 4 倍刻み: 20·4^subdiv)。 */
inline int subdiv_to_n(int subdiv) {
	if (subdiv < 0) subdiv = 0;
	if (subdiv > 6) subdiv = 6;   /* 上限 (n=64・icosphere 面数 81920) */
	return 1 << subdiv;
}

}  /* namespace srava_geo */

#endif
