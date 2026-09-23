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

/* ---- 閉じているか: 各無向辺がちょうど 2 回・かつ向きが逆どうしで現れる -----------
 * 境界辺 (1 回) も非多様体辺 (3 回以上) も、同じ向きの重複 (向きが揃っていない) も弾く。
 *
 * ★★ #3537: 辺は **溶接後の頂点番号** (weld_indices) で数える。生の番号で数えると
 *   *同じ座標の頂点が複数の番号で入っているメッシュ*で継ぎ目が見えない。
 *   実例 (2026-09-15 実測): 2 球の xor で得た「交線の円で接する三日月 2 つ」は
 *     重複頂点 106 / 溶接後に **4 回使われる辺が 108 本** = 交線の円に沿った非多様体
 *   なのに、生の番号では継ぎ目が 2 本の別々の円に見えてどの辺も 2 回になり、
 *   **閉じている**と答えていた。⇒ 判定の材料が実体からずれていた (同じ理由で
 *   self_intersects / nshells / nparts はもともと溶接している。ここだけ揃っていなかった)。
 *
 * ⚠ この歯抜けは **別の欠陥に隠されていた**。#3537 を直すまでは、その接する xor を
 *   self_intersects が *誤検出* して valid=0 になっており、答えだけは合っていた
 *   (厳密な有理数で確かめると、疑われていた三角形対は交差していない)。
 *   ⇒ 誤検出を止めた瞬間に valid=1 になって、初めてこちらの穴が出た。
 * ★ 溶接しても他の模型は 1 つも変わらない (box / sphere / cylinder / torus / union /
 *   revolve とその回転 / 自己交差 tube は **重複頂点 0**・全辺が 2 回)。 */
inline int is_closed(const TriView& m) {
	if ( m.nt <= 0 ) return 0;
	std::vector<uint32_t> canon;
	weld_indices(m, canon);
	/* key = 無向辺 (min,max)。値の下位 16 bit = 正向き数 / 上位 = 逆向き数。 */
	std::unordered_map<uint64_t, uint32_t> e;
	e.reserve((size_t)m.nt * 2);
	for ( int f = 0 ; f < m.nt ; ++f ) {
		for ( int k = 0 ; k < 3 ; ++k ) {
			uint32_t a = canon[m.tris[3*f + k]], b = canon[m.tris[3*f + (k+1)%3]];
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

/* ★★ #3537: 平面距離を **三角形の大きさに対する相対 ε** で 0 とみなす。
 *
 * ---- なぜ要るか (2026-09-14 の実測) ----
 * @revolve@ で作った立体を **回転させる**と valid が 0 になっていた。誤検出していたのは
 * manifold でも cgal でもなく *この述語*。誤検出した三角形対を厳密な有理数で計算すると
 * **交差していない**。2 枚は 1e-16 で同一平面だった (平面距離 ±1.3〜1.8e-16 / 辺長 ≈ 0.588
 * ⇒ 相対 2.6e-16 = 丸め誤差そのもの)。
 *
 *   軸に揃っているうち  平面距離が **厳密に 0** → edge_interval が 0 を返し
 *                       coplanar_tri_tri が走って正しく判定される
 *   回すと              距離が ±1e-16 の雑音になり D0 != 0 なので **一般分岐**へ落ちる
 *                       → 除算なし版が微小量 4 つの積 (~1e-63) を扱って桁落ち → 誤って 1
 *
 * ⚠ 近接判定ではない。厳密に計算した Möller の区間は ~1e-63 で相対 4 倍離れており、
 *   「ぎりぎり接している」のではなく *一般分岐の算術が成り立っていない*。
 * ★ @cylinder@ が無事だったのは蓋が **扇** (中心頂点を全員が共有) で同一平面の対がすべて
 *   隣接扱いになり判定に回らないため。@revolve@ の円環の蓋には中心が無い。
 *
 * ---- ★★ 尺度の取り方 (ここを 1 度間違えた) ----
 * 素朴には @N・U + d@ の *足し合わせた項の大きさ* (静的フィルタの permanent) を尺度にしたく
 * なるが、これは **原点の取り方で壊れる**。平面が原点の近くを通ると @N・U@ と @d@ が打ち消し
 * 合って permanent 自体が 1e-16 に潰れ、雑音との比が 1 に近づいて判定できなくなる。
 *   実測 (rot30 の tri 10 x 64): 距離 2.776e-17 に対し permanent 1.9e-16 ⇒ 相対 **0.143**
 *   ⇒ 「相対誤差」のつもりが座標系の原点を見ていた。
 * ⇒ 尺度は **|N| x |U - V0|** を使う。これは式の値が取りうる大きさそのもの (内積の上界) で、
 *   *平行移動でも拡大縮小でも変わらない*。同じ対で相対 4.7e-17 となり雑音として落ちる。
 * ★ 併せて距離も @N・(U - V0)@ の形で計算する — 大きな 2 項の引き算を作らないので、
 *   値そのものの桁落ちも消える (数学的には @N・U + d@ と同じ)。
 */
inline double plane_dist_scale(const double N[3], const double W[3])
{
	double n = std::sqrt(N[0]*N[0] + N[1]*N[1] + N[2]*N[2]);
	double w = std::sqrt(W[0]*W[0] + W[1]*W[1] + W[2]*W[2]);
	return n * w;
}

/* 係数。N 自身が外積で作られていて数 eps の相対誤差を持ち、そこへ 3 積 3 和が乗るので
 * 丸め誤差の上界は数 eps x 尺度。⚠ 大きすぎると **本当の交差を見落とす**ので、
 * 真陽性 (自己交差する tube・test/srava_meshprops.sh と test/srava_selfintersect.sh) を
 * 対照にして決めてある。 */
const double PLANE_DIST_EPS = 16.0 * 2.220446049250313e-16;   /* 16 * DBL_EPSILON */

/* 平面 (N, 基点 B) から点 P までの符号付き距離 (|N| 倍)。雑音は 0 に丸める。 */
inline double plane_dist(const double N[3], const double B[3], const double P[3])
{
	double W[3]; sub3(P, B, W);
	double d = dot3(N, W);
	return ( std::fabs(d) <= PLANE_DIST_EPS * plane_dist_scale(N, W) ) ? 0.0 : d;
}

inline int tri_tri(const double V0[3], const double V1[3], const double V2[3],
                   const double U0[3], const double U1[3], const double U2[3])
{
	double e1[3], e2[3], N1[3], N2[3];
	sub3(V1,V0,e1); sub3(V2,V0,e2); cross3(e1,e2,N1);
	double du0 = plane_dist(N1,V0,U0), du1 = plane_dist(N1,V0,U1), du2 = plane_dist(N1,V0,U2);
	double du0du1 = du0*du1, du0du2 = du0*du2;
	if ( du0du1 > 0.0 && du0du2 > 0.0 ) return 0;   /* 片側に寄っている */

	sub3(U1,U0,e1); sub3(U2,U0,e2); cross3(e1,e2,N2);
	double dv0 = plane_dist(N2,U0,V0), dv1 = plane_dist(N2,U0,V1), dv2 = plane_dist(N2,U0,V2);
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

/* ---- 符号つき体積 (発散定理) — #3527 ------------------------------------------
 * ★ 向きの揃った閉じた 2-多様体では、外殻は法線が外を向くので **正**・空洞の境界は内を
 *   向くので **負**。⇒ 中空の箱は「外殻 − 空洞」が素直に出る
 *   (cgal の PMP::volume / Manifold::Volume() と同じ約束)。
 * ⚠⚠ **閉じていないメッシュに体積は無い**。この関数は閉性を見ずに数を返すので、
 *   呼び側が topology().closed で **門を掛けること**。掛けないと開いた / 平たいメッシュにも
 *   *意味の無い値* が出る (cgal の cgaVolume が同じ理由で明示エラーにしている。
 *   2026-09-15 の実測: 同一平面の delaunay に 6.666… を返していた)。
 * ⚠ 空集合は 0。門に掛けない — ブールの結果が空になるのは普通のことで volume(A &&& B) は 0。
 * ★ centroid() が同じ式の重み付き版を持っているが、あちらは重心が目的で早期に割り算が入る。
 *   ⚠ 片方だけ式を直さないこと。
 */
inline double volume(const TriView& m) {
	/* ★★ **Kahan で足す**。topology() は同じ式を使いながら「符号だけ要るので Kahan は
	 *   使わない」と書いてあるが、こちらは *値* が要るので事情が違う。
	 *   ⚠ 実測 (2026-09-17): 素朴な総和だと 2x2x2 の箱が **7.9999999999999991** になり、
	 *     cgal (厳密) と Manifold::Volume() の 8 に対して 4 ulp ずれた。三角形 12 枚でこれなので、
	 *     掃引規模では効く。⇒ 「同じ式だから同じ精度」ではない。 */
	double s = 0.0, comp = 0.0;
	for ( int f = 0 ; f < m.nt ; ++f ) {
		const double *a = m.p(m.tris[3*f]), *b = m.p(m.tris[3*f+1]), *c = m.p(m.tris[3*f+2]);
		double u[3], v[3], w[3];
		detail::sub3(b, a, u); detail::sub3(c, a, v); detail::cross3(u, v, w);
		const double term = detail::dot3(w, a) / 6.0;
		const double y = term - comp;
		const double t = s + y;
		comp = ( t - s ) - y;
		s = t;
	}
	return s;
}

/* ================= 位相 (#3514) =================================================
 * ★★ **なぜ「連結成分」を 2 つの名前で数えるのか** — 面の連結成分 (シェル) と立体の
 *   連結成分 (塊) は *違う数*になる。中空の箱は塊 1 個・シェル 2 枚。xor の N 球
 *   (同心の入れ子シェル・wiki @Srava_kernel_sweep_20260911-2@ §5.4) は
 *   **塊 N/2 個・シェル N 枚**。どちらを数えているか曖昧なまま「成分数」と呼ぶと、
 *   カーネルごとに別の数を答える op ができる。⇒ *2 つとも名前を付けて両方返す*。
 *
 *   nef の @nparts@ は SNC の **marked volume** = 塊なので、こちらの @nparts@ も塊に
 *   合わせる (docs の命名規約「約束が同じなら同じ名前」)。ライブラリが直接くれるのは
 *   シェルの方 (Manifold::Decompose / PMP::connected_components / GEO::get_connected_components)
 *   なので、**塊はシェルから導く**。
 *
 * ★ 導き方 = **符号つき体積の符号**。向きの揃った閉じた 2-多様体では、外殻は法線が外を
 *   向くので発散定理の積分が **正**・空洞の境界は内を向くので **負**になる。
 *   ⇒ 塊の数 = 符号つき体積が正のシェルの数。包含判定 (レイキャスト) は要らない。
 *   ⚠ シェルの **入れ子関係**までは出ない (どの空洞がどの塊のものか)。塊を *取り出す*
 *     (@part@) にはそれが要るので、ここでは **数えるだけ**にしてある。
 *
 * ★ 種数は Euler 標数から。連結で閉じた向きづけ可能な曲面は chi = 2 - 2g なので
 *   g_i = 1 - chi_i/2 ・全体はその総和 (= 取っ手の総数)。
 *   ⚠ 閉じていないメッシュでは chi と種数の関係が成り立たない ⇒ closed=0 を返して
 *     呼び側が明示エラーにする (黙って意味の無い整数を返さない)。
 */
struct Topology {
	int nshells;   /* 境界シェル = 面の連結成分の数 */
	int nparts;    /* 塊 = 符号つき体積が正のシェルの数 (nef の marked volume と同じ約束) */
	int genus;     /* 全シェルの種数の和 sum(1 - chi_i/2) */
	int closed;    /* 1 = 全シェルが閉じた 2-多様体 (0 なら genus は意味を持たない) */
	Topology() : nshells(0), nparts(0), genus(0), closed(0) {}
};

namespace detail {

/* union-find (経路圧縮のみ・再帰なし)。 */
inline uint32_t uf_find(std::vector<uint32_t>& par, uint32_t x) {
	while ( par[x] != x ) { par[x] = par[par[x]]; x = par[x]; }
	return x;
}
inline void uf_union(std::vector<uint32_t>& par, uint32_t a, uint32_t b) {
	a = uf_find(par, a); b = uf_find(par, b);
	if ( a != b ) par[b] = a;
}

}  /* namespace detail */

/* ================= 殻の分解 (#3527 段 4) =======================================
 * ★★★ **分解は 1 本**。以前は union-find が topology() の中にだけ在り、取り出す側
 *   (part / shell) は各カーネルが自前で書いていた。⇒ #3527 が潰したのはまさにこれで、
 *   @nparts(mf-mesh3d)@ は meshprops の double・@part(mf-mesh3d,i)@ は Nef の SNC と、
 *   **数える側と取り出す側が別の分解を見ていた**。
 *   ⇒ ここを共有すれば「Σ volume(part(m,i)) == volume(m)」が *構造的に* 成り立つ。
 * ⚠ 殻の番号は **面の走査順** = 実装依存。「i 番目」を版を跨いで信用しないこと (#3527 ③)。
 *   位置で指す口 (part_at / shell_at) が本命。
 */
struct Shells {
	std::vector<uint32_t>	canon;  /* nv 個: 溶接後の代表頂点番号 (weld_indices と同じ) */
	std::vector<int>	face;   /* nt 個: 面 → 殻番号 */
	std::vector<int>	vert;   /* nv 個: 頂点 → 殻番号 (-1 = どの面からも届かない) */
	std::vector<double>	vol;    /* n 個: 殻ごとの符号つき体積 (正 = 外殻 / 負 = 空洞) */
	int			n;      /* 殻の数 (= topology().nshells) */
	/* ★ 殻 → 面 の索引 (CSR)。殻 c の面は faceIdx[faceStart[c] .. faceStart[c+1])。
	 * ⚠ これが無いと「ある殻の面だけ見る」処理が毎回 **全面を走査**することになり、
	 *   入れ子 (殻ごとに他の殻の巻き数を測る) が O(殻² × 面) に膨らむ。
	 *   索引を持つと Σ|殻| = 面数 なので **O(殻 × 面)** に収まる。 */
	std::vector<int>	faceStart;  /* n+1 個 */
	std::vector<int>	faceIdx;    /* nt 個 */
	Shells() : n(0) {}
};

/* 面 / 頂点を殻へ割り当て、殻ごとの符号つき体積を出す。
 * ★ 連結性は **溶接後の頂点番号**で見る (self_intersects / is_closed と同じ理由 — 同じ
 *   座標の頂点が別番号で入っていると、繋がっている面が別殻に割れて数が増える)。 */
inline void shells(const TriView& m, Shells& s) {
	s.canon.clear(); s.face.clear(); s.vert.clear(); s.vol.clear(); s.n = 0;
	if ( m.nt <= 0 || m.nv <= 0 ) return;

	weld_indices(m, s.canon);

	/* ---- (1) 溶接してから三角形の辺で繋ぐ ---- */
	std::vector<uint32_t> par((size_t)m.nv);
	for ( int i = 0 ; i < m.nv ; ++i ) par[(size_t)i] = (uint32_t)i;
	for ( int i = 0 ; i < m.nv ; ++i )
		if ( s.canon[(size_t)i] != (uint32_t)i ) detail::uf_union(par, s.canon[(size_t)i], (uint32_t)i);
	for ( int f = 0 ; f < m.nt ; ++f ) {
		detail::uf_union(par, m.tris[3*f], m.tris[3*f+1]);
		detail::uf_union(par, m.tris[3*f], m.tris[3*f+2]);
	}

	/* ---- (2) 根 → 密な殻番号 (⚠ **面の走査順**で振る = 索引の実装依存の出どころ) ---- */
	std::unordered_map<uint32_t,int> id;
	id.reserve((size_t)m.nt);
	for ( int f = 0 ; f < m.nt ; ++f ) {
		uint32_t r = detail::uf_find(par, m.tris[3*f]);
		if ( id.find(r) == id.end() ) { int n = (int)id.size(); id[r] = n; }
	}
	s.n = (int)id.size();
	if ( s.n <= 0 ) return;

	/* ---- (3) 面 → 殻 と 符号つき体積 (発散定理。符号だけ要るので Kahan は使わない) ---- */
	s.face.assign((size_t)m.nt, -1);
	s.vert.assign((size_t)m.nv, -1);
	s.vol.assign((size_t)s.n, 0.0);
	for ( int f = 0 ; f < m.nt ; ++f ) {
		const int ci = id[detail::uf_find(par, m.tris[3*f])];
		s.face[(size_t)f] = ci;
		const double *a = m.p(m.tris[3*f]), *b = m.p(m.tris[3*f+1]), *c = m.p(m.tris[3*f+2]);
		double u[3], v[3], w[3];
		detail::sub3(b, a, u); detail::sub3(c, a, v); detail::cross3(u, v, w);
		s.vol[(size_t)ci] += detail::dot3(w, a) / 6.0;
	}

	/* ---- (4) 頂点 → 殻 ---- */
	for ( int i = 0 ; i < m.nv ; ++i ) {
		std::unordered_map<uint32_t,int>::const_iterator it
		    = id.find(detail::uf_find(par, (uint32_t)i));
		if ( it != id.end() ) s.vert[(size_t)i] = it->second;
	}

	/* ---- (5) 殻 → 面 の索引 (計数ソート・面の順は元のまま保つ) ---- */
	s.faceStart.assign((size_t)s.n + 1, 0);
	for ( int f = 0 ; f < m.nt ; ++f ) ++s.faceStart[(size_t)s.face[(size_t)f] + 1];
	for ( int c = 0 ; c < s.n ; ++c ) s.faceStart[(size_t)c + 1] += s.faceStart[(size_t)c];
	s.faceIdx.assign((size_t)m.nt, 0);
	{
		std::vector<int> at(s.faceStart.begin(), s.faceStart.end() - 1);
		for ( int f = 0 ; f < m.nt ; ++f ) s.faceIdx[(size_t)at[(size_t)s.face[(size_t)f]]++] = f;
	}
}

/* シェル数 / 塊数 / 総種数をまとめて数える。
 * ★ 分解そのものは shells() に持たせてある (取り出す側と共有するため)。ここが足すのは
 *   **数えるために要る量** (頂点数 / 辺数 / 閉性) だけ。 */
inline Topology topology(const TriView& m) {
	Topology t;
	Shells s;
	shells(m, s);
	if ( s.n <= 0 ) return t;
	t.nshells = s.n;

	std::vector<int> nv((size_t)s.n, 0), ne((size_t)s.n, 0), nf((size_t)s.n, 0);
	for ( int f = 0 ; f < m.nt ; ++f ) ++nf[(size_t)s.face[(size_t)f]];

	/* ---- 頂点数 (溶接後・三角形から参照されているものだけ) ---- */
	{
		std::vector<char> used((size_t)m.nv, 0);
		for ( int f = 0 ; f < m.nt ; ++f )
			for ( int k = 0 ; k < 3 ; ++k ) used[s.canon[m.tris[3*f+k]]] = 1;
		for ( int i = 0 ; i < m.nv ; ++i ) {
			if ( ! used[(size_t)i] ) continue;
			if ( s.vert[(size_t)i] >= 0 ) ++nv[(size_t)s.vert[(size_t)i]];
		}
	}

	/* ---- 辺数と閉性 (無向辺がちょうど 正 1 + 逆 1) ---- */
	{
		std::unordered_map<uint64_t,uint32_t> e;
		e.reserve((size_t)m.nt * 2);
		int degenerate = 0;
		for ( int f = 0 ; f < m.nt ; ++f ) {
			const int ci = s.face[(size_t)f];
			for ( int k = 0 ; k < 3 ; ++k ) {
				uint32_t a = s.canon[m.tris[3*f + k]], b = s.canon[m.tris[3*f + (k+1)%3]];
				if ( a == b ) { degenerate = 1; continue; }
				uint64_t key = ( a < b ) ? ((uint64_t)a << 32 | b) : ((uint64_t)b << 32 | a);
				if ( e.find(key) == e.end() ) { e[key] = 0u; ++ne[(size_t)ci]; }
				e[key] += ( a < b ) ? 1u : 0x10000u;
			}
		}
		t.closed = degenerate ? 0 : 1;
		for ( std::unordered_map<uint64_t,uint32_t>::const_iterator it = e.begin() ;
		      t.closed && it != e.end() ; ++it )
			if ( (it->second & 0xffffu) != 1u || (it->second >> 16) != 1u ) t.closed = 0;
	}

	/* ---- 集計 ---- */
	for ( int c = 0 ; c < s.n ; ++c ) {
		if ( s.vol[(size_t)c] > 0.0 ) ++t.nparts;
		t.genus += 1 - ( nv[(size_t)c] - ne[(size_t)c] + nf[(size_t)c] ) / 2;
	}
	if ( ! t.closed ) t.genus = 0;   /* 意味を持たない値は返さない (呼び側が closed を見る) */
	return t;
}

/* ---- 点 p から見た殻の **巻き数** (winding number) ------------------------------
 * ★ 立体角の総和 / 4π。三角形ごとに Van Oosterom & Strackee (1983) の
 *     tan(Ω/2) = det[a b c] / ( |a||b||c| + (a·b)|c| + (a·c)|b| + (b·c)|a| )
 *   を足す (a,b,c = p から見た 3 頂点)。
 * ★★ なぜレイキャストでなく巻き数か: レイは **向きを選ばねばならず**、選んだ向きが
 *   頂点や辺を掠めると数え損なう ⇒ 「掠めたら選び直す」規約が要り、その規約が
 *   *入力依存で分岐する* (同じ形でも座標が少し違うと別の道を通る)。巻き数は向きを選ばず
 *   **全ての面を均等に**使うので、そういう分岐が 1 つも要らない。
 *   ⚠ 代わりに毎回 O(その殻の面数) かかる。入れ子は殻数² 回呼ぶので |体積| で枝刈りする。
 * 返り: 正の向きの閉じた殻なら 内側 **+1** / 外側 **0**。空洞の殻 (内向き) は 内側 **-1**。
 * ⚠ p が殻の **面の上** に載っていると ±0.5 付近になり判定できない。⇒ 代表点は
 *   「他の殻の上に載っていない」ことが前提 (= 殻どうしが接していない = valid)。 */
namespace detail {

/* 点 p から三角形 (A,B,C) を見込む **立体角** (符号つき・ステラジアン)。
 * ★★ 式は 1 か所だけに置く — shell_winding() と classify_point() が両方これを呼ぶ。
 *   ⚠ 写すと *片方だけ直して黙ってずれる* 型の欠陥になる (extract_faces を共通化したのと
 *     同じ理由)。 */
inline double tri_solid_angle(const double A[3], const double B[3], const double C[3],
                              const double p[3]) {
	double a[3], b[3], c[3];
	sub3(A, p, a); sub3(B, p, b); sub3(C, p, c);
	const double la = std::sqrt(dot3(a,a));
	const double lb = std::sqrt(dot3(b,b));
	const double lc = std::sqrt(dot3(c,c));
	double bc[3]; cross3(b, c, bc);
	const double num = dot3(a, bc);
	const double den = la*lb*lc + dot3(a,b)*lc + dot3(a,c)*lb + dot3(b,c)*la;
	return 2.0 * std::atan2(num, den);
}

const double FOUR_PI = 4.0 * 3.14159265358979323846;

}  /* namespace detail */

inline double shell_winding(const TriView& m, const Shells& s, int shell, const double p[3]) {
	if ( shell < 0 || shell >= s.n || (int)s.faceStart.size() <= shell + 1 ) return 0.0;
	double sum = 0.0;
	/* ★ CSR で **その殻の面だけ**を回す (全面走査だと入れ子が O(殻² × 面) になる)。 */
	for ( int k = s.faceStart[(size_t)shell] ; k < s.faceStart[(size_t)shell + 1] ; ++k ) {
		const int f = s.faceIdx[(size_t)k];
		sum += detail::tri_solid_angle(m.p(m.tris[3*f]), m.p(m.tris[3*f+1]),
		                               m.p(m.tris[3*f+2]), p);
	}
	return sum / detail::FOUR_PI;
}

/* ---- 殻の **入れ子**: parent[k] = 殻 k を直接包む殻 / -1 = 最も外側 --------------
 * ★★★ ここが「シェルの入れ子関係までは出ない」と書いてあった穴 (#3514 → #3527 段 4)。
 *   cgal が @PMP::volume_connected_components@ で貰っているものを double で書いたもの。
 * ★ 「直接」= 包む殻のうち **|符号つき体積| が最小**のもの。入れ子が深い形
 *   (空洞の中の島の中の空洞) でも正しく組める。⇒ ringprops.h の nesting() と同じ形。
 * ★ 枝刈り「包むなら必ず |体積| が大きい」は 2D と同じ。巻き数が O(面数) なので、
 *   これが無いと殻数² × 面数 かかる。
 * ⚠ 代表点は殻 k の **最初に現れる面の 0 番頂点**。⇒ 他の殻の面の上に載っていないこと
 *   (殻どうしが接している値では入れ子はそもそも定義できない)。 */
inline void nesting(const TriView& m, const Shells& s, std::vector<int>& parent) {
	parent.assign((size_t)(s.n > 0 ? s.n : 0), -1);
	if ( s.n <= 1 ) return;

	std::vector<int> rep((size_t)s.n, -1);
	for ( int f = 0 ; f < m.nt ; ++f ) {
		const int c = s.face[(size_t)f];
		if ( c >= 0 && rep[(size_t)c] < 0 ) rep[(size_t)c] = f;
	}
	for ( int r = 0 ; r < s.n ; ++r ) {
		if ( rep[(size_t)r] < 0 ) continue;
		const double *p  = m.p(m.tris[3*rep[(size_t)r]]);
		const double  ar = std::fabs(s.vol[(size_t)r]);
		int    best  = -1;
		double bestA = 0.0;
		for ( int k = 0 ; k < s.n ; ++k ) {
			if ( k == r ) continue;
			const double ak = std::fabs(s.vol[(size_t)k]);
			if ( ak <= ar ) continue;                    /* 包むなら必ず大きい */
			if ( best >= 0 && ak >= bestA ) continue;    /* 既に見つけた方が内側 */
			if ( std::fabs(shell_winding(m, s, k, p)) < 0.5 ) continue;
			best = k; bestA = ak;
		}
		parent[(size_t)r] = best;
	}
}

/* ---- i 番目の塊を作る殻番号 (外殻 1 枚 + その **直接の** 空洞) --------------------
 * ★ 索引 i は **符号つき体積が正の殻の走査順** ⇒ topology().nparts と同じ列を数えている。
 *   *数える側と取り出す側が同じ分解を見る* ことが #3527 の決定そのもの。
 * ★ 検定できる形: **Σ |volume(part(m,i))| == |volume(m)|** (符号なしで成立)。
 *   ⚠ 殻のほうは **符号つきでしか**成立しない (shell は「値を分割する片」ではない)。
 *     この 2 式が別であることが part と shell が別物である理由そのもの。
 * ⚠ 「直接の」空洞だけ — 空洞の中にまた島があれば、その島は **別の塊**。
 * 返り: 1 = 取り出せた / 0 = i が範囲外。 */
inline int part_shells(const TriView& m, const Shells& s, int i, std::vector<int>& out) {
	out.clear();
	if ( i < 0 || s.n <= 0 ) return 0;
	int outer = -1, seen = 0;
	for ( int c = 0 ; c < s.n ; ++c ) {
		if ( !(s.vol[(size_t)c] > 0.0) ) continue;
		if ( seen == i ) { outer = c; break; }
		++seen;
	}
	if ( outer < 0 ) return 0;

	std::vector<int> parent;
	nesting(m, s, parent);
	out.push_back(outer);
	for ( int c = 0 ; c < s.n ; ++c ) {
		if ( c == outer ) continue;
		if ( parent[(size_t)c] != outer ) continue;
		if ( s.vol[(size_t)c] < 0.0 ) out.push_back(c);
	}
	return 1;
}

/* ---- 点 p を **含む** 塊の番号 --------------------------------------------------
 * ★★ @shell_at@ が「いちばん近い殻」なのに @part_at@ は「**含む**塊」— 非対称に見えるが
 *   理由がある。**立体は内側を持ち、曲面は持たない**。殻は面の連結成分 (曲面) なので
 *   「含む」が定義できず最近傍しか言えない。塊は立体なので内外が言える。
 *   ⇒ どちらも「その位置に在る片を指す」という 1 つの規約の、次元による 2 つの姿。
 * ★ 塊の巻き数 = その塊を作る殻の巻き数の **和**。外殻 +1 / 空洞 -1 なので
 *   **材料の中でだけ 1** になり、空洞の中では 0 になる。
 *   ⇒ 「空洞の中に塊は無い」と正しく答えられる (最近傍だと空洞の中でも塊が返ってしまう)。
 * 返り: >=0 塊の番号 / -1 どの塊も含まない / -2 2 つ以上が含む (入力が valid でない)。 */
inline int part_at(const TriView& m, const Shells& s, const double p[3]) {
	if ( s.n <= 0 ) return -1;
	std::vector<int> parent;
	nesting(m, s, parent);
	std::vector<double> w((size_t)s.n, 0.0);
	for ( int c = 0 ; c < s.n ; ++c ) w[(size_t)c] = shell_winding(m, s, c, p);

	int found = -1, nfound = 0, idx = 0;
	for ( int c = 0 ; c < s.n ; ++c ) {
		if ( !(s.vol[(size_t)c] > 0.0) ) continue;
		double sum = w[(size_t)c];
		for ( int k = 0 ; k < s.n ; ++k )
			if ( k != c && parent[(size_t)k] == c && s.vol[(size_t)k] < 0.0 ) sum += w[(size_t)k];
		if ( std::fabs(sum) >= 0.5 ) { found = idx; ++nfound; }
		++idx;
	}
	if ( nfound == 0 ) return -1;
	if ( nfound >  1 ) return -2;
	return found;
}

namespace detail {

/* 点と三角形の距離² (Ericson, Real-Time Collision Detection §5.1.5 の領域分け)。 */
inline double point_tri_dist2(const double p[3], const double a[3],
                              const double b[3], const double c[3]) {
	double ab[3], ac[3], ap[3];
	sub3(b, a, ab); sub3(c, a, ac); sub3(p, a, ap);
	const double d1 = dot3(ab, ap), d2 = dot3(ac, ap);
	double q[3];
	if ( d1 <= 0.0 && d2 <= 0.0 ) { sub3(p, a, q); return dot3(q, q); }          /* 頂点 a */

	double bp[3]; sub3(p, b, bp);
	const double d3 = dot3(ab, bp), d4 = dot3(ac, bp);
	if ( d3 >= 0.0 && d4 <= d3 ) { sub3(p, b, q); return dot3(q, q); }           /* 頂点 b */

	const double vc = d1*d4 - d3*d2;
	if ( vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0 ) {                                /* 辺 ab */
		const double den = d1 - d3;
		const double v = ( den != 0.0 ) ? d1/den : 0.0;
		for ( int k = 0 ; k < 3 ; ++k ) q[k] = p[k] - (a[k] + v*ab[k]);
		return dot3(q, q);
	}
	double cp[3]; sub3(p, c, cp);
	const double d5 = dot3(ab, cp), d6 = dot3(ac, cp);
	if ( d6 >= 0.0 && d5 <= d6 ) { sub3(p, c, q); return dot3(q, q); }           /* 頂点 c */

	const double vb = d5*d2 - d1*d6;
	if ( vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0 ) {                                /* 辺 ac */
		const double den = d2 - d6;
		const double w = ( den != 0.0 ) ? d2/den : 0.0;
		for ( int k = 0 ; k < 3 ; ++k ) q[k] = p[k] - (a[k] + w*ac[k]);
		return dot3(q, q);
	}
	const double va = d3*d6 - d5*d4;
	if ( va <= 0.0 && (d4-d3) >= 0.0 && (d5-d6) >= 0.0 ) {                      /* 辺 bc */
		const double den = (d4-d3) + (d5-d6);
		const double w = ( den != 0.0 ) ? (d4-d3)/den : 0.0;
		for ( int k = 0 ; k < 3 ; ++k ) q[k] = p[k] - (b[k] + w*(c[k]-b[k]));
		return dot3(q, q);
	}
	const double den = va + vb + vc;                                            /* 面の内部 */
	if ( den == 0.0 ) { sub3(p, a, q); return dot3(q, q); }
	const double v = vb/den, w = vc/den;
	for ( int k = 0 ; k < 3 ; ++k ) q[k] = p[k] - (a[k] + v*ab[k] + w*ac[k]);
	return dot3(q, q);
}

}  /* namespace detail */

/* ---- 点 p に **いちばん近い** 殻 --------------------------------------------------
 * ★ 番号と 2 通り要るわけ: 番号は「列挙のため」で **指す先が無い**。モデルの書き方を
 *   変えると殻の集合そのものが変わるので、番号は当然別の殻を指す (#3527 ③)。
 * ⚠⚠ 同距離の殻が 2 つ以上あるときは **断る** (-2)。黙って片方を選ぶと「同じ式に
 *   2 通りの値」になる (#3516 / #3518-1 で潰してきた穴)。
 * ⚠ cgal は EPECK の厳密比較で同距離を見るが、こちらは double。**相対許容差**で見る。
 *   ★ 向きは安全側 — 許容差を持たせると *断る側* に倒れる。厳密比較にすると丸めで
 *     同距離が同距離に見えなくなり、**黙って片方を選ぶ**という一番まずい形になる。
 * 返り: >=0 殻番号 / -1 面が無い / -2 同距離が 2 つ以上。 */
/* ★★ #3553: 点から **面の集合** までの最短距離 (符号なし)。
 *   ⇒ occt / cgal / geogram / openvdb の @distance_at@ と **同じ定義** (符号を付けない・
 *     内側でも正)。mf / ch はこれで初めて持つ (それまで distance_at が無かった)。
 * ⚠⚠ **総当たり** (AABB を持たない) ので O(nt)。数万面までは実用だが、大きなメッシュを
 *   何度も測るなら cgal (AABB_tree) / geogram (MeshFacetsAABB) を使うこと。
 *   ⇒ 「無いより桁が違うほど良い」ための実装で、速さで並べるための実装ではない。
 * ★ 空なら -1 (呼び手が理由つきで断る)。 */
inline double distance_at(const TriView& m, const double p[3]) {
	if ( m.nt <= 0 ) return -1.0;
	double best = -1.0;
	for ( int t = 0 ; t < m.nt ; ++t ) {
		const double d2 = detail::point_tri_dist2(p, m.p(m.tris[3*(size_t)t + 0]),
		                                             m.p(m.tris[3*(size_t)t + 1]),
		                                             m.p(m.tris[3*(size_t)t + 2]));
		if ( best < 0.0 || d2 < best ) best = d2;
	}
	return ( best < 0.0 ) ? -1.0 : std::sqrt(best);
}

/* ---- 点 p の **立体に対する位置** (#3579) ----------------------------------------
 * 返り: **0 = 境界ちょうど / -1 = 内側 (開) / +1 = 外側**
 *
 * ★★★ なぜ 3 値か — 境界ちょうどの点を「内側」に入れるか「外側」に入れるかは
 *   **一意に決まらない**。この木は同じ形の縮退を 3 回とも「黙って片方を選ばない」で
 *   解いている (section の共面は極限を両方返す / shell_at の同距離は明示エラー /
 *   openvdb #3491 は零交差の帯で oracle を訊かない)。⇒ ここでも判定器に答えさせず、
 *   **第 3 の集合として切り出す** (ひさ 2026-09-22・#3575)。
 *   ⚠ shell_at 式の「明示エラー」は採れない — 整数格子の点群では **97% が境界に載る**
 *     (mac 実測: rand([0,0,0],[2,2,2],200,7) が 194/200) のでほぼ必ずエラーになる。
 *
 * ★ 許容差の向きは shell_at に倣い **「ちょうど」を広げる側**へ倒す。疑わしい点は
 *   境界 (0) に入れる = *利用者が明示的に決めねばならない側*へ寄せる。
 *   ⚠ 逆向き (疑わしきを内か外へ) は「黙って片方を選ぶ」ことになる。
 *
 * ★★ 内外は **巻き数の総和**で見る。全三角形を足すと 外殻 +1 / 空洞 -1 なので
 *   **材料の中でだけ ±1**・空洞の中と外は 0 になる ⇒ 「空洞の中は外」と正しく答える。
 *   ⇒ 殻ごとの分解 (Shells) は要らない。部分ごとの巻き数が要るのは part_at だけ。
 *   ⚠ レイキャストにしない理由は shell_winding のコメントに在る (向きを選ぶ規約が
 *     入力依存で分岐する)。判定を書き直さないこと。
 *
 * ★ 距離と巻き数を **1 周で**両方求める (三角形を 2 度回らない)。10^5 点 x 面数を歩くので
 *   走査回数がそのまま効く。⚠ 呼び手は先に **bbox で枝刈り**すること。
 *
 * @tol_abs は **絶対**の許容差 (呼び手が尺度を掛けて渡す)。負なら境界を判定しない。 */
inline int classify_point(const TriView& m, const double p[3], double tol_abs) {
	if ( m.nt <= 0 ) return 1;          /* 空の立体: すべて外側 */
	double best = -1.0, wsum = 0.0;
	for ( int f = 0 ; f < m.nt ; ++f ) {
		const double *A = m.p(m.tris[3*(size_t)f]),
		             *B = m.p(m.tris[3*(size_t)f + 1]),
		             *C = m.p(m.tris[3*(size_t)f + 2]);
		const double d2 = detail::point_tri_dist2(p, A, B, C);
		if ( best < 0.0 || d2 < best ) best = d2;
		wsum += detail::tri_solid_angle(A, B, C, p);
	}
	if ( tol_abs >= 0.0 && best >= 0.0 && best <= tol_abs * tol_abs ) return 0;
	return ( std::fabs(wsum / detail::FOUR_PI) >= 0.5 ) ? -1 : 1;
}

inline int shell_at(const TriView& m, const Shells& s, const double p[3]) {
	if ( s.n <= 0 || m.nt <= 0 ) return -1;
	std::vector<double> best((size_t)s.n, 0.0);
	std::vector<char>   seen((size_t)s.n, 0);
	for ( int f = 0 ; f < m.nt ; ++f ) {
		const int c = s.face[(size_t)f];
		if ( c < 0 ) continue;
		const double d = detail::point_tri_dist2(p, m.p(m.tris[3*f]),
		                                            m.p(m.tris[3*f+1]),
		                                            m.p(m.tris[3*f+2]));
		if ( ! seen[(size_t)c] || d < best[(size_t)c] ) { best[(size_t)c] = d; seen[(size_t)c] = 1; }
	}
	int win = -1;
	for ( int c = 0 ; c < s.n ; ++c ) {
		if ( ! seen[(size_t)c] ) continue;
		if ( win < 0 || best[(size_t)c] < best[(size_t)win] ) win = c;
	}
	if ( win < 0 ) return -1;
	/* 同距離 (相対許容差) が他にもあれば断る。 */
	const double bw = best[(size_t)win];
	int nwin = 0;
	for ( int c = 0 ; c < s.n ; ++c ) {
		if ( ! seen[(size_t)c] ) continue;
		const double d = best[(size_t)c];
		const double scale = ( d > bw ? d : bw );
		if ( std::fabs(d - bw) <= 1e-12 * ( scale > 1.0 ? scale : 1.0 ) ) ++nwin;
	}
	return ( nwin > 1 ) ? -2 : win;
}

/* ---- 面の部分集合から新しいメッシュを起こす (part / shell の共通部) ---------------
 * ⚠⚠ **共通化しておくこと**。cgal が @cg_extract_faces@ を 1 本にしてあるのと同じ理由で、
 *   part 側と shell 側に写すと片方だけ直したときに黙ってずれる。
 * ★ 頂点は **使われたものだけ**を元の順で詰め直し、@keep[新番号] = 元の番号@ を返す。
 *   ⇒ 呼び側は色などの頂点属性を同じ並びで写せる (捨てると往復で情報が落ちる)。
 * ⚠ **溶接はしない**。元の頂点の同一性をそのまま保つ (manifold の色の継ぎ目は重複頂点 +
 *   merge ベクタで表されており、ここで潰すと色が壊れる)。 */
inline void extract_faces(const TriView& m, const std::vector<int>& faces,
                          std::vector<double>& coords, std::vector<uint32_t>& tris,
                          std::vector<uint32_t>& keep) {
	coords.clear(); tris.clear(); keep.clear();
	std::vector<int> nix((size_t)(m.nv > 0 ? m.nv : 0), -1);
	for ( size_t k = 0 ; k < faces.size() ; ++k ) {
		const int f = faces[k];
		if ( f < 0 || f >= m.nt ) continue;
		for ( int j = 0 ; j < 3 ; ++j ) {
			const uint32_t o = m.tris[3*(size_t)f + j];
			if ( (int)o < 0 || (int)o >= m.nv ) continue;
			if ( nix[(size_t)o] < 0 ) {
				nix[(size_t)o] = (int)keep.size();
				keep.push_back(o);
				const double *q = m.p(o);
				coords.push_back(q[0]); coords.push_back(q[1]); coords.push_back(q[2]);
			}
			tris.push_back((uint32_t)nix[(size_t)o]);
		}
	}
}

}  /* namespace srava_mesh */

#endif
