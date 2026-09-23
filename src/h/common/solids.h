#ifndef ___common_solids_h___
#define ___common_solids_h___

/*
 * solids.h — 基本立体 (角錐 / 円柱 / 円錐 / トーラス / 正四面体) の共通生成器
 *            (ヘッダオンリー・カーネル非依存)。
 *
 * geodesic.h (測地球) / tube.h (掃引管) と **同じ方針** (#3474)。すべての幾何カーネルの
 * モジュールが同じこのヘッダを include し、同一アルゴリズムで立体を生成する。狙いは
 *   ① 基本立体の欠落でカーネル選択が裏返るのを止める (pyramid が cgal にしか無いと
 *      `r ||| pyramid(...)` は cgal でしか実行できない = 式全体のカーネルが決まってしまう)
 *   ② 頂点座標・三角形の並びをカーネル間で一致させ、体積を bit 一致させる
 * こと。基本立体は **閉形式で生成できる**のでカーネル差が出る余地がない。
 *
 * ⚠ occt だけは事情が違う。厳密な B-rep で円柱・円錐・トーラスを持てるので、
 *   このヘッダは使わず解析曲面で作る (一致検査は閉形式=体積・表面積で行う)。
 *   ただし **平面多面体** (pyramid / tetrahedron) は occt でも平面 Face の集まりなので
 *   メッシュ系と厳密に一致する (prism と同じ扱い)。
 *
 * Sink は geodesic.h と同じ形:
 *   struct Sink { int add_vertex(double x,double y,double z); void add_triangle(int a,int b,int c); };
 * ★ geodesic.h と違い **重複頂点は出さない** (閉形式で index を自分で組むため)。
 *   Sink 側で重複排除する必要はない。
 *
 * ---- 座標系の規約 (既存 op に合わせる。ここが揃っていないとカーネル一致が取れない) ----
 *   prism(n,h,r)     … 底面は z=0 の XY 平面・天面は z=h。**原点中心ではない**。
 *                      prism(n,h,r) ≡ extrude(ngon(n,r),h)。
 *   pyramid(n,h,r)   … 底面は z=0 の XY 平面・頂点 (apex) は z=h。**原点中心ではない**。
 *   cylinder(r,h,seg)… **原点中心**・軸は +Z (z=-h/2〜+h/2)。occt の cylinder と同じ。
 *   cone(r,h,seg)    … **原点中心**・軸は +Z。底面 z=-h/2 (半径 r)・apex は z=+h/2。
 *                      cylinder の片方の半径を 0 にしたものなので cylinder に合わせる。
 *   torus(R,r,seg)   … **原点中心**・軸は +Z (穴が Z 方向に空く)。occt の torus と同じ。
 *   tetrahedron(r)   … **原点中心**・外接球半径 r。
 * ⚠ pyramid だけ原点中心でないのは既存の cgal 実装と prism に合わせたため
 *   (occt の prism は逆に原点中心で、これは **既存のカーネル間不一致**。別途起票)。
 *
 * 円周の頂点は ngon / prism と同じ **角度 2πk/n・+X 始点・CCW**。
 * 蓋 (cap) は **中心頂点からの扇** で張る (cgal の CGAL::make_pyramid と同じ位相・
 * 正 n 角錐が 6 頂点 8 面になるのはこのため)。
 */

#include <cmath>

namespace srava_geo {

/* M_PI は環境依存 (MSVC/Cygwin の一部で未定義) なのでここで持つ (tube.h と同じ)。 */
const double SOLID_PI = 3.14159265358979323846;

/* 円周分割数の正規化。**0 = 未指定 → 既定 32**。
 * ★★ #3530: 以前はここに「1,2 は 3 へ丸める」という *第 2 の規約* が同居していた。
 *   3 未満の可否は op 側の共通検査 (common/segs.h の check_segs) が一手に決めるので、
 *   ここに来る seg は 0 か 3 以上しかない。⇒ 丸めの枝は消した (残すと規約が 2 つになる)。 */
inline int solid_segs(int seg) { return (seg >= 3) ? seg : 32; }

/* ---- 内部ヘルパ: 円周リングを 1 枚張る ---------------------------------------
 * 中心 (0,0,z)・半径 r・n 分割のリングを追加し、リング先頭の index を返す。
 * 頂点は k=0..n-1 の順 (角度 2πk/n・CCW)。 */
template<class Sink>
int solid_ring(Sink& sink, int n, double r, double z) {
	int base = -1;
	for (int k = 0; k < n; ++k) {
		double a = 2.0 * SOLID_PI * (double)k / (double)n;
		int id = sink.add_vertex(r * std::cos(a), r * std::sin(a), z);
		if (k == 0) base = id;
	}
	return base;
}

/* リングに蓋をする。up=1 なら +Z を向く蓋 (CCW)、up=0 なら -Z を向く蓋。
 * center = 中心頂点の index、ring = リング先頭の index。 */
template<class Sink>
void solid_cap(Sink& sink, int center, int ring, int n, int up) {
	for (int k = 0; k < n; ++k) {
		int a = ring + k;
		int b = ring + ((k + 1) % n);
		if (up) sink.add_triangle(center, a, b);
		else    sink.add_triangle(center, b, a);
	}
}

/* ---- 正 n 角錐 pyramid(n, h, r) ---------------------------------------------
 * 底面 = z=0 の正 n 角形 (外接円半径 r)・apex = (0,0,h)。
 * 頂点は n(リング) + 1(apex) + 1(底面中心) = n+2、面は n(側面) + n(底面) = 2n。 */
template<class Sink>
void make_pyramid(int n, double h, double r, Sink& sink) {
	if (n < 3) n = 3;
	int ring   = solid_ring(sink, n, r, 0.0);
	int apex   = sink.add_vertex(0.0, 0.0, h);
	int center = sink.add_vertex(0.0, 0.0, 0.0);
	for (int k = 0; k < n; ++k) {                      /* 側面 (外向き CCW) */
		int a = ring + k, b = ring + ((k + 1) % n);
		sink.add_triangle(a, b, apex);
	}
	solid_cap(sink, center, ring, n, 0);               /* 底面 (-Z 向き) */
}

/* ---- 正 n 角柱 prism(n, h, r) -------------------------------------------------
 * 底面 = z=0 の正 n 角形 (外接円半径 r)・天面 = z=h。prism(n,h,r) ≡ extrude(ngon(n,r), h)。
 * 頂点は 2n(リング) + 2(蓋の中心) = 2n+2、面は 2n(側面) + 2n(蓋) = 4n
 * (CGAL::make_regular_prism と同じ位相 — 正六角柱が 14 頂点 24 面になるのはこのため)。 */
template<class Sink>
void make_prism(int n, double h, double r, Sink& sink) {
	if (n < 3) n = 3;
	int bot = solid_ring(sink, n, r, 0.0);
	int top = solid_ring(sink, n, r, h);
	int cb  = sink.add_vertex(0.0, 0.0, 0.0);
	int ct  = sink.add_vertex(0.0, 0.0, h);
	for (int k = 0; k < n; ++k) {                      /* 側面 (外向き) */
		int k1 = (k + 1) % n;
		sink.add_triangle(bot + k, bot + k1, top + k1);
		sink.add_triangle(bot + k, top + k1, top + k);
	}
	solid_cap(sink, cb, bot, n, 0);                    /* 底 (-Z) */
	solid_cap(sink, ct, top, n, 1);                    /* 天 (+Z) */
}

/* ---- 円柱 cylinder(r, h, seg) ------------------------------------------------
 * 原点中心・軸 +Z。側面は seg 枚の四角形を 2 三角形へ (対角線の取り方も共通)。
 * 頂点 = 2·seg + 2、面 = 4·seg。 */
template<class Sink>
void make_cylinder(double r, double h, int seg, Sink& sink) {
	int n = solid_segs(seg);
	int bot = solid_ring(sink, n, r, -0.5 * h);
	int top = solid_ring(sink, n, r,  0.5 * h);
	int cb  = sink.add_vertex(0.0, 0.0, -0.5 * h);
	int ct  = sink.add_vertex(0.0, 0.0,  0.5 * h);
	for (int k = 0; k < n; ++k) {                      /* 側面 (外向き) */
		int k1 = (k + 1) % n;
		sink.add_triangle(bot + k, bot + k1, top + k1);
		sink.add_triangle(bot + k, top + k1, top + k);
	}
	solid_cap(sink, cb, bot, n, 0);                    /* 底 (-Z) */
	solid_cap(sink, ct, top, n, 1);                    /* 天 (+Z) */
}

/* ---- 円錐 cone(r, h, seg) ----------------------------------------------------
 * 原点中心・軸 +Z。底面 z=-h/2 (半径 r)・apex は z=+h/2。
 * 頂点 = seg + 2、面 = 2·seg。pyramid の「円周分割版」だが **原点中心**なので z が違う。 */
template<class Sink>
void make_cone(double r, double h, int seg, Sink& sink) {
	int n      = solid_segs(seg);
	int ring   = solid_ring(sink, n, r, -0.5 * h);
	int apex   = sink.add_vertex(0.0, 0.0,  0.5 * h);
	int center = sink.add_vertex(0.0, 0.0, -0.5 * h);
	for (int k = 0; k < n; ++k) {                      /* 側面 (外向き) */
		int a = ring + k, b = ring + ((k + 1) % n);
		sink.add_triangle(a, b, apex);
	}
	solid_cap(sink, center, ring, n, 0);               /* 底面 (-Z 向き) */
}

/* ---- トーラス torus(R, r, seg) -----------------------------------------------
 * 原点中心・軸 +Z (穴が Z 方向)。R=大円半径・r=管半径。大円も管断面も **同じ seg** で
 * 分割する (sphere(r,seg) / circle(r,segs) / tube(path,segs) と同じく分割の knob は 1 つ)。
 *   P(u,v) = ((R + r·cos v)·cos u, (R + r·cos v)·sin u, r·sin v)
 * 頂点 = seg²、面 = 2·seg²。∂u × ∂v が外向きなので (i,j)→(i+1,j)→(i+1,j+1) が CCW 外向き。 */
template<class Sink>
void make_torus(double R, double r, int seg, Sink& sink) {
	int n = solid_segs(seg);
	int base = -1;
	for (int i = 0; i < n; ++i) {
		double u = 2.0 * SOLID_PI * (double)i / (double)n;
		double cu = std::cos(u), su = std::sin(u);
		for (int j = 0; j < n; ++j) {
			double v = 2.0 * SOLID_PI * (double)j / (double)n;
			double cv = std::cos(v), sv = std::sin(v);
			double rad = R + r * cv;
			int id = sink.add_vertex(rad * cu, rad * su, r * sv);
			if (i == 0 && j == 0) base = id;
		}
	}
	for (int i = 0; i < n; ++i) {
		int i1 = (i + 1) % n;
		for (int j = 0; j < n; ++j) {
			int j1 = (j + 1) % n;
			int a = base + i  * n + j;
			int b = base + i1 * n + j;
			int c = base + i1 * n + j1;
			int d = base + i  * n + j1;
			sink.add_triangle(a, b, c);
			sink.add_triangle(a, c, d);
		}
	}
}

/* ---- 正四面体 tetrahedron(r) -------------------------------------------------
 * 原点中心・外接球半径 r。立方体の対角 4 頂点 (1,1,1)/(1,-1,-1)/(-1,1,-1)/(-1,-1,1) を
 * r/√3 倍する (閉形式・分割数を持たない)。頂点 4・面 4。 */
template<class Sink>
void make_tetrahedron(double r, Sink& sink) {
	const double s = r / std::sqrt(3.0);
	int v0 = sink.add_vertex( s,  s,  s);
	int v1 = sink.add_vertex( s, -s, -s);
	int v2 = sink.add_vertex(-s,  s, -s);
	int v3 = sink.add_vertex(-s, -s,  s);
	sink.add_triangle(v0, v1, v2);   /* 4 面とも外向き CCW (面心と法線の内積が正) */
	sink.add_triangle(v0, v2, v3);
	sink.add_triangle(v0, v3, v1);
	sink.add_triangle(v1, v3, v2);
}

}  /* namespace srava_geo */

#endif
