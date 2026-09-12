#ifndef ___common_meshprops_h___
#define ___common_meshprops_h___

/*
 * meshprops.h — 三角形メッシュの **素性を訊く** 計算 (ヘッダオンリー・カーネル非依存)。
 *               bbox / centroid / area と、valid の中身 (閉じているか・自己交差が無いか)。
 *
 * geodesic.h / solids.h / tube.h / affine.h と同じ方針 (#3474 / #3486)。
 * ★ **依存は素の配列だけ** — pigData にも幾何カーネルにも依存しない。
 *
 * ---- なぜ括り出すのか (#3487) ----
 * bbox / centroid / valid / area は「→value」で 2D 型を要さないのに cgal / manifold の
 * 2 本にしか無かった。無いと、値の素性を訊くためだけに別カーネルへ cast させることになる
 * (そして #3478 で見たとおり **cast が通らない値では確認手段そのものが消える**)。
 *
 * ★★ **valid は「共通の定義」を先に決めないと配れない**。決めずに配ると「同じ op が
 *   カーネルごとに別のことを答える」ようになり、#3478 で見た「宣言と実態のずれ」を
 *   自分で作ることになる。決めた定義 (2026-09-05・docs/srava_function_reference.md の
 *   記述をそのまま持ち上げたもの):
 *
 *     valid(m) = 1  ⟺  ① 空でない  ∧  ② 閉じている (境界辺が無い 2-多様体)
 *                       ∧  ③ 自己交差が無い
 *
 *   ★ 「① 空でない」を入れる理由: この op の使い道は bbox / centroid のガード
 *     (「空集合はエラー → valid でガード」と関数リファレンスが書いている) で、
 *     空集合ではその 2 つが定義できないから。⚠ ここは **既存 2 本が食い違っていた**
 *     箇所でもある (cgal は空を 1・manifold は空を 0 と答えていた)。定義に合わせて揃えた。
 *
 *   ★ **定義は 1 つ・答え方はカーネルごと**でよい。厳密カーネル (cgal / nef) は CGAL の
 *     厳密述語で、occt は OCCT の検査器で、double のカーネル (geogram / cherchi) はこの
 *     ヘッダの実装で、同じ 3 条件を答える。openvdb は距離場なので ②③ が構造的に成り立つ
 *     (境界も自己交差も表現できない) ため ① だけを見る — これは「別のことを答えている」
 *     のではなく「その表現では ②③ が恒真」という事実。
 *
 * ---- 自己交差の判定について ----
 * ⚠ この実装は **double の述語**で、厳密ではない (geogram / cherchi の座標が double なので
 *   もともと厳密には答えられない)。cgal の CGAL::Polygon_mesh_processing::does_self_intersect
 *   を正解とした突き合わせは test/srava_meshprops.sh が見る。
 * ⚠ **頂点を共有する三角形どうしは交差とみなさない** (隣接面は必ず辺や頂点で接するため)。
 *   よって「共有頂点でだけ触れ合う (pinch)」形は検出できない。CGAL も隣接面は除外する。
 */

#include <cmath>
#include <vector>
#include <unordered_map>
#include <stdint.h>
#include <string.h>

namespace srava_mesh {

/* 素の配列で受ける三角形メッシュ。coords は 3*nv・tris は 3*nt の頂点番号。 */
struct TriView {
	const double   *coords;
	int             nv;
	const uint32_t *tris;
	int             nt;
	TriView(const double *c, int n, const uint32_t *t, int m)
	    : coords(c), nv(n), tris(t), nt(m) {}
	const double* p(uint32_t i) const { return coords + 3*(size_t)i; }
};

/* ---- 軸平行バウンディングボックス。空なら 0 を返し mn/mx は 0 で埋める ---------- */
inline int bbox(const TriView& m, double mn[3], double mx[3]) {
	if ( m.nv <= 0 ) {
		for ( int k = 0 ; k < 3 ; ++k ) mn[k] = mx[k] = 0.0;
		return 0;
	}
	for ( int k = 0 ; k < 3 ; ++k ) { mn[k] = mx[k] = m.coords[k]; }
	for ( int i = 1 ; i < m.nv ; ++i )
		for ( int k = 0 ; k < 3 ; ++k ) {
			double v = m.coords[3*(size_t)i + k];
			if ( v < mn[k] ) mn[k] = v;
			if ( v > mx[k] ) mx[k] = v;
		}
	return 1;
}

/* ---- 表面積 = 三角形の面積の和 ------------------------------------------------- */
inline double area(const TriView& m) {
	double s = 0.0;
	for ( int f = 0 ; f < m.nt ; ++f ) {
		const double *A = m.p(m.tris[3*f]), *B = m.p(m.tris[3*f+1]), *C = m.p(m.tris[3*f+2]);
		double ux = B[0]-A[0], uy = B[1]-A[1], uz = B[2]-A[2];
		double vx = C[0]-A[0], vy = C[1]-A[1], vz = C[2]-A[2];
		double cx = uy*vz - uz*vy, cy = uz*vx - ux*vz, cz = ux*vy - uy*vx;
		s += 0.5 * std::sqrt(cx*cx + cy*cy + cz*cz);
	}
	return s;
}

/* ---- 体積重心 (発散定理: 各三角形と原点で四面体に分け、符号付き体積で加重平均) ----
 * cgMesh3D::op_centroid と同じ式。体積 0 (退化・空) は原点を返して 0。 */
inline int centroid(const TriView& m, double out[3]) {
	double cx = 0, cy = 0, cz = 0, vol = 0;
	for ( int f = 0 ; f < m.nt ; ++f ) {
		const double *A = m.p(m.tris[3*f]), *B = m.p(m.tris[3*f+1]), *C = m.p(m.tris[3*f+2]);
		double v = ( A[0]*(B[1]*C[2] - B[2]*C[1])
		           - A[1]*(B[0]*C[2] - B[2]*C[0])
		           + A[2]*(B[0]*C[1] - B[1]*C[0]) ) / 6.0;
		vol += v;
		cx += v * (A[0] + B[0] + C[0]) / 4.0;
		cy += v * (A[1] + B[1] + C[1]) / 4.0;
		cz += v * (A[2] + B[2] + C[2]) / 4.0;
	}
	if ( vol != 0.0 ) { out[0] = cx/vol; out[1] = cy/vol; out[2] = cz/vol; return 1; }
	out[0] = out[1] = out[2] = 0.0;
	return 0;
}

/* ---- 閉じているか: 各無向辺がちょうど 2 回・かつ向きが逆どうしで現れる -----------
 * 境界辺 (1 回) も非多様体辺 (3 回以上) も、同じ向きの重複 (向きが揃っていない) も弾く。 */
inline int is_closed(const TriView& m) {
	if ( m.nt <= 0 ) return 0;
	/* key = 無向辺 (min,max)。値の下位 16 bit = 正向き数 / 上位 = 逆向き数。 */
	std::unordered_map<uint64_t, uint32_t> e;
	e.reserve((size_t)m.nt * 2);
	for ( int f = 0 ; f < m.nt ; ++f ) {
		for ( int k = 0 ; k < 3 ; ++k ) {
			uint32_t a = m.tris[3*f + k], b = m.tris[3*f + (k+1)%3];
			if ( a == b ) return 0;   /* 退化三角形 */
			uint64_t key = ( a < b ) ? ((uint64_t)a << 32 | b) : ((uint64_t)b << 32 | a);
			e[key] += ( a < b ) ? 1u : 0x10000u;
		}
	}
	for ( std::unordered_map<uint64_t,uint32_t>::const_iterator it = e.begin() ; it != e.end() ; ++it )
		if ( (it->second & 0xffffu) != 1u || (it->second >> 16) != 1u )
			return 0;
	return 1;
}

/* ================= 自己交差判定 =================================================
 * ★ 広域探索は一様格子 (三角形の AABB をセルへ登録)・狭域は Möller (1997) の
 *   三角形どうしの交差判定。頂点番号を共有する組は隣接なので飛ばす。
 * ⚠ double の述語なので厳密ではない (座標がもともと double のカーネル向け)。 */
namespace detail {

inline void sub3(const double a[3], const double b[3], double r[3])
{ r[0]=a[0]-b[0]; r[1]=a[1]-b[1]; r[2]=a[2]-b[2]; }
inline void cross3(const double a[3], const double b[3], double r[3])
{ r[0]=a[1]*b[2]-a[2]*b[1]; r[1]=a[2]*b[0]-a[0]*b[2]; r[2]=a[0]*b[1]-a[1]*b[0]; }
inline double dot3(const double a[3], const double b[3])
{ return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }

/* 同一平面上の 2 三角形。最大成分の軸を落として 2D にし、
 * 辺どうしの交差 or 一方の頂点が他方の内部、で判定する。 */
inline int coplanar_tri_tri(const double N[3],
                            const double V0[3], const double V1[3], const double V2[3],
                            const double U0[3], const double U1[3], const double U2[3])
{
	double a[3] = { std::fabs(N[0]), std::fabs(N[1]), std::fabs(N[2]) };
	int i0, i1;
	if ( a[0] > a[1] ) { if ( a[0] > a[2] ) { i0 = 1; i1 = 2; } else { i0 = 0; i1 = 1; } }
	else               { if ( a[2] > a[1] ) { i0 = 0; i1 = 1; } else { i0 = 0; i1 = 2; } }

	const double *V[3] = { V0, V1, V2 };
	const double *U[3] = { U0, U1, U2 };

	/* 辺どうしの交差 (2D の線分交差・端点接触も交差とみなす) */
	for ( int i = 0 ; i < 3 ; ++i ) {
		double p0[2] = { V[i][i0], V[i][i1] }, p1[2] = { V[(i+1)%3][i0], V[(i+1)%3][i1] };
		for ( int j = 0 ; j < 3 ; ++j ) {
			double q0[2] = { U[j][i0], U[j][i1] }, q1[2] = { U[(j+1)%3][i0], U[(j+1)%3][i1] };
			double d1 = (p1[0]-p0[0])*(q0[1]-p0[1]) - (p1[1]-p0[1])*(q0[0]-p0[0]);
			double d2 = (p1[0]-p0[0])*(q1[1]-p0[1]) - (p1[1]-p0[1])*(q1[0]-p0[0]);
			double d3 = (q1[0]-q0[0])*(p0[1]-q0[1]) - (q1[1]-q0[1])*(p0[0]-q0[0]);
			double d4 = (q1[0]-q0[0])*(p1[1]-q0[1]) - (q1[1]-q0[1])*(p1[0]-q0[0]);
			if ( ((d1>0)!=(d2>0)) && ((d3>0)!=(d4>0)) ) return 1;
		}
	}
	/* 包含 (辺が交わらないなら、内部にあるか外にあるかのどちらか) */
	for ( int s = 0 ; s < 2 ; ++s ) {
		const double **T = s ? U : V, **P = s ? V : U;
		double ax = T[0][i0], ay = T[0][i1], bx = T[1][i0], by = T[1][i1], cx = T[2][i0], cy = T[2][i1];
		double px = P[0][i0], py = P[0][i1];
		double s1 = (bx-ax)*(py-ay) - (by-ay)*(px-ax);
		double s2 = (cx-bx)*(py-by) - (cy-by)*(px-bx);
		double s3 = (ax-cx)*(py-cy) - (ay-cy)*(px-cx);
		if ( (s1>=0 && s2>=0 && s3>=0) || (s1<=0 && s2<=0 && s3<=0) ) return 1;
	}
	return 0;
}

/* 区間の重なりから交差を決める (Möller の interval overlap)。 */
inline int edge_interval(double VV0,double VV1,double VV2, double D0,double D1,double D2,
                         double D0D1,double D0D2, double *a,double *b,double *c,
                         double *x0,double *x1)
{
	if ( D0D1 > 0.0 )      { *a=VV2; *b=(VV0-VV2)*D2; *c=(VV1-VV2)*D2; *x0=D2-D0; *x1=D2-D1; }
	else if ( D0D2 > 0.0 ) { *a=VV1; *b=(VV0-VV1)*D1; *c=(VV2-VV1)*D1; *x0=D1-D0; *x1=D1-D2; }
	else if ( D1*D2 > 0.0 || D0 != 0.0 )
	                       { *a=VV0; *b=(VV1-VV0)*D0; *c=(VV2-VV0)*D0; *x0=D0-D1; *x1=D0-D2; }
	else if ( D1 != 0.0 )  { *a=VV1; *b=(VV0-VV1)*D1; *c=(VV2-VV1)*D1; *x0=D1-D0; *x1=D1-D2; }
	else if ( D2 != 0.0 )  { *a=VV2; *b=(VV0-VV2)*D2; *c=(VV1-VV2)*D2; *x0=D2-D0; *x1=D2-D1; }
	else                   return 0;   /* 同一平面 — 呼び手が coplanar 側で扱う */
	return 1;
}

inline int tri_tri(const double V0[3], const double V1[3], const double V2[3],
                   const double U0[3], const double U1[3], const double U2[3])
{
	double e1[3], e2[3], N1[3], N2[3];
	sub3(V1,V0,e1); sub3(V2,V0,e2); cross3(e1,e2,N1);
	double d1 = -dot3(N1,V0);
	double du0 = dot3(N1,U0)+d1, du1 = dot3(N1,U1)+d1, du2 = dot3(N1,U2)+d1;
	double du0du1 = du0*du1, du0du2 = du0*du2;
	if ( du0du1 > 0.0 && du0du2 > 0.0 ) return 0;   /* 片側に寄っている */

	sub3(U1,U0,e1); sub3(U2,U0,e2); cross3(e1,e2,N2);
	double d2 = -dot3(N2,U0);
	double dv0 = dot3(N2,V0)+d2, dv1 = dot3(N2,V1)+d2, dv2 = dot3(N2,V2)+d2;
	double dv0dv1 = dv0*dv1, dv0dv2 = dv0*dv2;
	if ( dv0dv1 > 0.0 && dv0dv2 > 0.0 ) return 0;

	double D[3]; cross3(N1,N2,D);
	double maxv = std::fabs(D[0]); int index = 0;
	if ( std::fabs(D[1]) > maxv ) { maxv = std::fabs(D[1]); index = 1; }
	if ( std::fabs(D[2]) > maxv ) {                          index = 2; }
	double vp0 = V0[index], vp1 = V1[index], vp2 = V2[index];
	double up0 = U0[index], up1 = U1[index], up2 = U2[index];

	double a,b,c,x0,x1, dd,e,f,y0,y1;
	if ( ! edge_interval(vp0,vp1,vp2, dv0,dv1,dv2, dv0dv1,dv0dv2, &a,&b,&c,&x0,&x1) )
		return coplanar_tri_tri(N1, V0,V1,V2, U0,U1,U2);
	if ( ! edge_interval(up0,up1,up2, du0,du1,du2, du0du1,du0du2, &dd,&e,&f,&y0,&y1) )
		return coplanar_tri_tri(N1, V0,V1,V2, U0,U1,U2);

	double xx = x0*x1, yy = y0*y1, xxyy = xx*yy;
	double isect1[2], isect2[2];
	double tmp = a*xxyy;
	isect1[0] = tmp + b*x1*yy;  isect1[1] = tmp + c*x0*yy;
	tmp = dd*xxyy;
	isect2[0] = tmp + e*xx*y1;  isect2[1] = tmp + f*xx*y0;
	if ( isect1[0] > isect1[1] ) { double t = isect1[0]; isect1[0] = isect1[1]; isect1[1] = t; }
	if ( isect2[0] > isect2[1] ) { double t = isect2[0]; isect2[0] = isect2[1]; isect2[1] = t; }
	if ( isect1[1] < isect2[0] || isect2[1] < isect1[0] ) return 0;
	return 1;
}

}  /* namespace detail */

/* ---- 頂点番号を **位置で**正規化する ------------------------------------------
 * ★★ 隣接面を「頂点番号を共有するか」で判定すると、**同じ座標の頂点が複数の番号で
 *   入っているメッシュ**で誤検出する。実例 (2026-09-05): manifold の色つきメッシュは
 *   GetMeshGL64 が **プロパティ (色) の境目で頂点を分裂させる**ので、色の継ぎ目を挟む
 *   2 面は同じ辺を共有しているのに番号が違い、隣接と見なされずに交差判定へ回って
 *   「接している」= 交差と判定されてしまう (色を付けただけで valid が 0 になった)。
 *   ⇒ **座標が完全一致する頂点を同じものと見なしてから**隣接を判定する。
 *   ⚠ 完全一致だけを溶接する (許容誤差で寄せない)。継ぎ目の分裂は同じ double を複製した
 *     ものなので完全一致で足り、近いだけの別頂点を潰すと本物の交差を見逃す。 */
inline void weld_indices(const TriView& m, std::vector<uint32_t>& canon) {
	canon.resize((size_t)m.nv);
	std::unordered_map<uint64_t, uint32_t> seen;
	seen.reserve((size_t)m.nv * 2);
	for ( int i = 0 ; i < m.nv ; ++i ) {
		const double *p = m.p((uint32_t)i);
		/* double のビット列 3 本を混ぜた鍵。衝突しても下で座標を照合するので安全側。 */
		uint64_t h = 1469598103934665603ull;
		for ( int k = 0 ; k < 3 ; ++k ) {
			uint64_t b; ::memcpy(&b, &p[k], sizeof b);
			h = (h ^ b) * 1099511628211ull;
		}
		std::unordered_map<uint64_t,uint32_t>::iterator it = seen.find(h);
		if ( it == seen.end() ) { seen[h] = (uint32_t)i; canon[i] = (uint32_t)i; }
		else {
			const double *q = m.p(it->second);
			canon[i] = ( q[0]==p[0] && q[1]==p[1] && q[2]==p[2] ) ? it->second : (uint32_t)i;
		}
	}
}

/* 自己交差があれば 1。**位置が同じ頂点を共有する組 (= 隣接面) は対象外**。 */
inline int self_intersects(const TriView& m) {
	if ( m.nt < 2 ) return 0;
	std::vector<uint32_t> canon;
	weld_indices(m, canon);

	double mn[3], mx[3];
	if ( ! bbox(m, mn, mx) ) return 0;

	/* 三角形の AABB を作り、平均対角からセルの大きさを決める。 */
	std::vector<double> lo((size_t)m.nt*3), hi((size_t)m.nt*3);
	double diagsum = 0.0;
	for ( int f = 0 ; f < m.nt ; ++f ) {
		const double *P[3] = { m.p(m.tris[3*f]), m.p(m.tris[3*f+1]), m.p(m.tris[3*f+2]) };
		for ( int k = 0 ; k < 3 ; ++k ) {
			double a = P[0][k], b = P[1][k], c = P[2][k];
			double l = a < b ? a : b;  l = l < c ? l : c;
			double h = a > b ? a : b;  h = h > c ? h : c;
			lo[3*(size_t)f+k] = l; hi[3*(size_t)f+k] = h;
		}
		double dx = hi[3*(size_t)f]-lo[3*(size_t)f];
		double dy = hi[3*(size_t)f+1]-lo[3*(size_t)f+1];
		double dz = hi[3*(size_t)f+2]-lo[3*(size_t)f+2];
		diagsum += std::sqrt(dx*dx+dy*dy+dz*dz);
	}
	double ext[3] = { mx[0]-mn[0], mx[1]-mn[1], mx[2]-mn[2] };
	double span = std::sqrt(ext[0]*ext[0]+ext[1]*ext[1]+ext[2]*ext[2]);
	double cell = diagsum / (double)m.nt;
	if ( !(cell > 0.0) ) cell = ( span > 0.0 ) ? span : 1.0;
	/* セル数を抑える (1 軸 256 まで)。 */
	for ( int k = 0 ; k < 3 ; ++k )
		if ( ext[k] / cell > 256.0 ) cell = ext[k] / 256.0;

	int n[3];
	for ( int k = 0 ; k < 3 ; ++k ) {
		n[k] = (int)(ext[k] / cell) + 1;
		if ( n[k] < 1 ) n[k] = 1;
	}
	std::unordered_map<uint64_t, std::vector<int> > grid;
	grid.reserve((size_t)m.nt * 2);
	for ( int f = 0 ; f < m.nt ; ++f ) {
		int a[3], b[3];
		for ( int k = 0 ; k < 3 ; ++k ) {
			a[k] = (int)((lo[3*(size_t)f+k] - mn[k]) / cell); if ( a[k] < 0 ) a[k] = 0; if ( a[k] >= n[k] ) a[k] = n[k]-1;
			b[k] = (int)((hi[3*(size_t)f+k] - mn[k]) / cell); if ( b[k] < 0 ) b[k] = 0; if ( b[k] >= n[k] ) b[k] = n[k]-1;
		}
		for ( int i = a[0] ; i <= b[0] ; ++i )
		for ( int j = a[1] ; j <= b[1] ; ++j )
		for ( int k = a[2] ; k <= b[2] ; ++k )
			grid[((uint64_t)i<<42) | ((uint64_t)j<<21) | (uint64_t)k].push_back(f);
	}

	for ( std::unordered_map<uint64_t, std::vector<int> >::const_iterator it = grid.begin() ;
	      it != grid.end() ; ++it ) {
		const std::vector<int>& v = it->second;
		for ( size_t x = 0 ; x + 1 < v.size() ; ++x )
		for ( size_t y = x + 1 ; y < v.size() ; ++y ) {
			int f = v[x], g = v[y];
			/* AABB が離れていれば飛ばす */
			int sep = 0;
			for ( int k = 0 ; k < 3 ; ++k )
				if ( hi[3*(size_t)f+k] < lo[3*(size_t)g+k] || hi[3*(size_t)g+k] < lo[3*(size_t)f+k] ) { sep = 1; break; }
			if ( sep ) continue;
			/* 位置が同じ頂点を共有する組 (= 隣接面) は対象外 */
			int shared = 0;
			for ( int p = 0 ; p < 3 && !shared ; ++p )
				for ( int q = 0 ; q < 3 ; ++q )
					if ( canon[m.tris[3*f+p]] == canon[m.tris[3*g+q]] ) { shared = 1; break; }
			if ( shared ) continue;
			if ( detail::tri_tri(m.p(m.tris[3*f]), m.p(m.tris[3*f+1]), m.p(m.tris[3*f+2]),
			                     m.p(m.tris[3*g]), m.p(m.tris[3*g+1]), m.p(m.tris[3*g+2])) )
				return 1;
		}
	}
	return 0;
}

/* ---- valid の本体: ① 空でない ∧ ② 閉じている ∧ ③ 自己交差が無い ---------------- */
inline int valid(const TriView& m) {
	if ( m.nt <= 0 || m.nv <= 0 ) return 0;
	if ( ! is_closed(m) )         return 0;
	if ( self_intersects(m) )     return 0;
	return 1;
}

}  /* namespace srava_mesh */

#endif
