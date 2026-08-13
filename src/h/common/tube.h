#ifndef ___common_tube_h___
#define ___common_tube_h___

/*
 * tube.h — 折れ線に沿った掃引管 (tube) の共通生成器 (ヘッダオンリー・カーネル非依存)。
 *
 * cgal.so と manifold.so の **両方** が同じこのヘッダを include し、同一アルゴリズムで管を
 * 生成する (geodesic.h と同じ方針・#3415)。狙いは
 *   ① 手書きの掃引幾何 (rotation-minimizing frame・断面リング配置・キャップ) を 1 箇所に持つ
 *   ② 頂点座標・三角形の並びをカーネル間で一致させ、体積を数値誤差レベルで揃える
 * こと。CGAL 依存だったのは「Surface_mesh コンテナ」と「2D の Polygon_set_2 合併」だけで、
 * 掃引そのものは CGAL の機能ではない (ブール演算も使っていない) ため丸ごと括り出せる。
 *
 * 提供するもの (すべて namespace srava_geo):
 *   - tube_dedup()    … 連続重複頂点の間引き (接線が定義できないため。弾かずに間引く)
 *   - tube_frames()   … 各頂点の接線 T と rotation-minimizing frame (N,B)
 *                       (double-reflection 法・Wang et al. 2008)
 *   - make_tube_3d()  … 3D: 断面リング + 側壁 + 平キャップ を三角形で Sink へ流す
 *   - make_tube_2d()  … 2D: stamp-and-union の **スタンプ (円・台形) を輪郭列で** Sink へ流す
 *                       (合併そのものは各カーネルの 2D ブールが行う)
 *
 * Sink (3D) は geodesic.h と同じ形:
 *   struct Sink { int add_vertex(double x,double y,double z); void add_triangle(int a,int b,int c); };
 * Sink (2D):
 *   struct Sink2 { void add_ring(const double *xy, int npts); };   // xy = x,y の flat 配列・CCW
 *
 * ★三角形を直接出す (四角形 + 後段三角化ではない) のは、分割対角線の取り方をカーネル間で
 *   一致させるため。四角形は一般に非平面なので、対角線が違うと体積がわずかにずれる。
 */

#include <vector>
#include <cmath>

namespace srava_geo {

/* 掃引の枠計算用の最小限の double 3 ベクトル (最終座標のみ各カーネルの型へ落とす)。 */
struct TubeV3 {
	double x, y, z;
	TubeV3() : x(0), y(0), z(0) {}
	TubeV3(double a, double b, double c) : x(a), y(b), z(c) {}
};

inline TubeV3 tv_add  (const TubeV3& a, const TubeV3& b) { return TubeV3(a.x+b.x, a.y+b.y, a.z+b.z); }
inline TubeV3 tv_sub  (const TubeV3& a, const TubeV3& b) { return TubeV3(a.x-b.x, a.y-b.y, a.z-b.z); }
inline TubeV3 tv_scale(const TubeV3& a, double s)        { return TubeV3(a.x*s, a.y*s, a.z*s); }
inline double tv_dot  (const TubeV3& a, const TubeV3& b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
inline TubeV3 tv_cross(const TubeV3& a, const TubeV3& b) {
	return TubeV3(a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x);
}
inline double tv_len (const TubeV3& a) { return std::sqrt(tv_dot(a,a)); }
inline TubeV3 tv_norm(const TubeV3& a) { double L = tv_len(a); return (L > 0) ? tv_scale(a, 1.0/L) : a; }

/* M_PI は環境依存 (MSVC/Cygwin の一部で未定義) なのでここで持つ。 */
const double TUBE_PI = 3.14159265358979323846;

/* 生成の結果コード。エラー文言は呼び出し側 (各カーネルの op) が持つ。 */
enum TubeStatus {
	TUBE_OK = 0,
	TUBE_ERR_DUP_VERTEX  = 1,   /* 連続重複頂点 (接線が定義できない。dedup 後は起きない) */
	TUBE_ERR_ZERO_SEGMENT = 2   /* 半径 0 の頂点が連続 (退化セグメント) */
};

/* 連続重複頂点を **弾かずに間引く**。spline 等が区間境界で点を重複させても通す。
 * 残す側の半径を採る (最初の出現)。出力 P/R は clear してから詰める。 */
inline void
tube_dedup(const std::vector<TubeV3>& Praw, const std::vector<double>& Rraw,
           std::vector<TubeV3>& P, std::vector<double>& R)
{
	P.clear(); R.clear();
	for ( size_t i = 0 ; i < Praw.size() ; ++i ) {
		if ( ! P.empty() && tv_len(tv_sub(Praw[i], P.back())) == 0.0 ) continue;
		P.push_back(Praw[i]);
		R.push_back(Rraw[i]);
	}
}

/* 各頂点の接線 T (隣接セグメント方向の平均・端点は片側) と、それに沿って運ぶ
 * rotation-minimizing frame (N,B)。単純な固定軸投影で出るねじれ/破綻を回避する。
 * T/N/B は n 要素に resize される。連続重複頂点があれば TUBE_ERR_DUP_VERTEX。 */
inline int
tube_frames(const std::vector<TubeV3>& P,
            std::vector<TubeV3>& T, std::vector<TubeV3>& N, std::vector<TubeV3>& B)
{
	int n = (int)P.size();
	T.assign((size_t)n, TubeV3());
	N.assign((size_t)n, TubeV3());
	B.assign((size_t)n, TubeV3());

	for ( int i = 0 ; i < n ; ++i ) {
		TubeV3 din, dout;
		if ( i > 0 ) {
			din = tv_sub(P[i], P[i-1]);
			if ( tv_len(din) == 0.0 ) return TUBE_ERR_DUP_VERTEX;
			din = tv_norm(din);
		}
		if ( i < n-1 ) {
			dout = tv_sub(P[i+1], P[i]);
			if ( tv_len(dout) == 0.0 ) return TUBE_ERR_DUP_VERTEX;
			dout = tv_norm(dout);
		}
		TubeV3 t = ( i == 0 ) ? dout : ( i == n-1 ) ? din : tv_add(din, dout);
		t = tv_norm(t);
		if ( tv_len(t) == 0.0 ) {   /* 180°折り返し (din ≈ -dout) → 片側採用 */
			t = tv_norm(( dout.x == 0 && dout.y == 0 && dout.z == 0 ) ? din : dout);
		}
		T[i] = t;
	}

	/* 初期法線: T[0] に直交する任意の単位ベクトル。 */
	{
		TubeV3 up = ( std::fabs(T[0].z) < 0.9 ) ? TubeV3(0,0,1) : TubeV3(0,1,0);
		TubeV3 r0 = tv_sub(up, tv_scale(T[0], tv_dot(up, T[0])));
		N[0] = tv_norm(r0);
		B[0] = tv_norm(tv_cross(T[0], N[0]));
	}
	/* double reflection: セグメントで 1 回、接線差で 1 回反射させて N を運ぶ。 */
	for ( int i = 0 ; i < n-1 ; ++i ) {
		TubeV3 v1 = tv_sub(P[i+1], P[i]);
		double c1 = tv_dot(v1, v1);
		TubeV3 rL, tL;
		if ( c1 > 0.0 ) {
			rL = tv_sub(N[i], tv_scale(v1, (2.0/c1) * tv_dot(v1, N[i])));
			tL = tv_sub(T[i], tv_scale(v1, (2.0/c1) * tv_dot(v1, T[i])));
		} else { rL = N[i]; tL = T[i]; }
		TubeV3 v2 = tv_sub(T[i+1], tL);
		double c2 = tv_dot(v2, v2);
		TubeV3 rNext = ( c2 > 0.0 ) ? tv_sub(rL, tv_scale(v2, (2.0/c2) * tv_dot(v2, rL))) : rL;
		N[i+1] = tv_norm(rNext);
		B[i+1] = tv_norm(tv_cross(T[i+1], N[i+1]));
	}
	return TUBE_OK;
}

/* ---- 3D: 折れ線 P まわりに半径 R (可変) の丸断面を掃引した閉多様体 ----
 * 断面 = segs 角形近似の円。パスは折れ線そのまま (補間なし)。
 * 半径 0 の端はリングが 1 頂点へ潰れて円錐状に尖り、半径 > 0 の端は平キャップで閉じる。
 * 巻き方向は外向き右手系 (t,N,B) で統一。P/R は dedup 済みで n >= 2 であること。 */
template<class Sink>
int make_tube_3d(const std::vector<TubeV3>& P, const std::vector<double>& R, int segs, Sink& sink)
{
	int n = (int)P.size();
	if ( n < 2 ) return TUBE_ERR_DUP_VERTEX;
	if ( segs < 3 ) segs = 3;

	std::vector<TubeV3> T, N, B;
	int st = tube_frames(P, T, N, B);
	if ( st != TUBE_OK ) return st;

	/* 円周サンプルの cos/sin。 */
	std::vector<double> cs((size_t)segs), sn((size_t)segs);
	for ( int k = 0 ; k < segs ; ++k ) {
		double a = 2.0 * TUBE_PI * (double)k / (double)segs;
		cs[(size_t)k] = std::cos(a); sn[(size_t)k] = std::sin(a);
	}

	/* 各頂点に断面リング (r>0) or 軸頂点 (r==0・尖端) を割当。 */
	std::vector<std::vector<int> > rv((size_t)n);
	for ( int i = 0 ; i < n ; ++i ) {
		if ( R[(size_t)i] == 0.0 ) {
			rv[(size_t)i].push_back(sink.add_vertex(P[i].x, P[i].y, P[i].z));
		} else {
			rv[(size_t)i].resize((size_t)segs);
			for ( int k = 0 ; k < segs ; ++k ) {
				TubeV3 off = tv_add(tv_scale(N[i], R[(size_t)i]*cs[(size_t)k]),
				                    tv_scale(B[i], R[(size_t)i]*sn[(size_t)k]));
				TubeV3 p   = tv_add(P[i], off);
				rv[(size_t)i][(size_t)k] = sink.add_vertex(p.x, p.y, p.z);
			}
		}
	}

	/* 側壁: 隣接リング L=i, U=i+1 を角度方向に帯状に縫合。
	 * 四角形 [Lk, Lk1, Uk1, Uk] は外向き右手系。対角線 (Lk1,Uk) で 2 三角形に固定分割する
	 * (★カーネル間で分割を一致させるため。この向きは旧 cgal 実装の CGAL::triangulate_faces が
	 *  選んでいた対角線と同じで、box との corefinement union の結果も一致する)。
	 * 軸頂点側は同一 index に潰れて三角形 1 枚になる。 */
	for ( int i = 0 ; i < n-1 ; ++i ) {
		bool fullL = ( (int)rv[(size_t)i].size()   == segs );
		bool fullU = ( (int)rv[(size_t)i+1].size() == segs );
		if ( ! fullL && ! fullU ) return TUBE_ERR_ZERO_SEGMENT;
		for ( int k = 0 ; k < segs ; ++k ) {
			int k1 = (k + 1) % segs;
			int Lk  = fullL ? rv[(size_t)i][(size_t)k]      : rv[(size_t)i][0];
			int Lk1 = fullL ? rv[(size_t)i][(size_t)k1]     : rv[(size_t)i][0];
			int Uk  = fullU ? rv[(size_t)i+1][(size_t)k]    : rv[(size_t)i+1][0];
			int Uk1 = fullU ? rv[(size_t)i+1][(size_t)k1]   : rv[(size_t)i+1][0];
			/* 重複 index を落として輪を作る (r=0 側で 4→3 に潰れる)。 */
			int f[4]; int nf = 0;
			f[nf++] = Lk;
			if ( Lk1 != f[nf-1] )                        f[nf++] = Lk1;
			if ( Uk1 != f[nf-1] && Uk1 != f[0] )         f[nf++] = Uk1;
			if ( Uk  != f[nf-1] && Uk  != f[0] )         f[nf++] = Uk;
			if ( nf == 3 ) {
				sink.add_triangle(f[0], f[1], f[2]);
			} else if ( nf == 4 ) {
				sink.add_triangle(f[0], f[1], f[3]);
				sink.add_triangle(f[1], f[2], f[3]);
			}
		}
	}

	/* 平キャップ: 端リングが r>0 なら中心頂点へファンで閉じる (外向き)。
	 * 始端は -T 向き (逆順)、終端は +T 向き。r==0 の端は側壁ファンで既に閉じている。 */
	if ( (int)rv[0].size() == segs ) {
		int c0 = sink.add_vertex(P[0].x, P[0].y, P[0].z);
		for ( int k = 0 ; k < segs ; ++k ) {
			int k1 = (k + 1) % segs;
			sink.add_triangle(c0, rv[0][(size_t)k1], rv[0][(size_t)k]);
		}
	}
	if ( (int)rv[(size_t)n-1].size() == segs ) {
		int c1 = sink.add_vertex(P[n-1].x, P[n-1].y, P[n-1].z);
		for ( int k = 0 ; k < segs ; ++k ) {
			int k1 = (k + 1) % segs;
			sink.add_triangle(c1, rv[(size_t)n-1][(size_t)k], rv[(size_t)n-1][(size_t)k1]);
		}
	}
	return TUBE_OK;
}

/* ---- 2D: 折れ線 P を半径 (半幅) R で太らせた帯のスタンプ列 ----
 * stamp-and-union: 頂点ごとに円 (丸ジョイント/丸キャップ)、線分ごとに法線 ±r の台形を出す。
 * 合併は各カーネルの 2D ブールが行う (CGAL=Polygon_set_2 / Manifold=CrossSection)。
 * 鋭角ターン・自己重なり・可変幅でも破綻しない (輪郭を直接たどる方式の凹側自己交差を回避)。
 * 出す輪郭はすべて CCW。r=0 端は台形が三角に潰れて尖る。 */
template<class Sink2>
int make_tube_2d(const std::vector<TubeV3>& P, const std::vector<double>& R, int segs, Sink2& sink)
{
	int n = (int)P.size();
	if ( segs < 3 ) segs = 3;

	std::vector<double> xy;

	/* 頂点ごとの円 (半径 r>0)。端も含めて置くので丸キャップになる。 */
	for ( int i = 0 ; i < n ; ++i ) {
		double r = R[(size_t)i];
		if ( r <= 0.0 ) continue;
		xy.clear();
		xy.reserve((size_t)segs * 2);
		for ( int k = 0 ; k < segs ; ++k ) {
			double a = 2.0 * TUBE_PI * (double)k / (double)segs;
			xy.push_back(P[i].x + r*std::cos(a));
			xy.push_back(P[i].y + r*std::sin(a));
		}
		sink.add_ring(&xy[0], segs);
	}

	/* 線分ごとの台形 (法線方向 ±r)。r=0 端は 2 隅が中心に潰れて三角 (尖り)。 */
	for ( int i = 0 ; i < n-1 ; ++i ) {
		double dx = P[i+1].x - P[i].x, dy = P[i+1].y - P[i].y;
		double L = std::sqrt(dx*dx + dy*dy);
		if ( L == 0.0 ) continue;                 /* dedup 済みで通常来ない */
		double nx = -dy / L, ny = dx / L;         /* 進行方向左の単位法線 */
		double r0 = R[(size_t)i], r1 = R[(size_t)i+1];
		/* 右→左の順に回ると CCW (進行方向左が +n なので、右側を先に辿る)。 */
		double corner[4][2] = {
			{ P[i].x   - nx*r0, P[i].y   - ny*r0 },   /* 始点・右 */
			{ P[i+1].x - nx*r1, P[i+1].y - ny*r1 },   /* 終点・右 */
			{ P[i+1].x + nx*r1, P[i+1].y + ny*r1 },   /* 終点・左 */
			{ P[i].x   + nx*r0, P[i].y   + ny*r0 }    /* 始点・左 */
		};
		xy.clear();
		for ( int j = 0 ; j < 4 ; ++j ) {         /* 連続重複隅 (r=0 で潰れる) を除いて構築 */
			size_t m = xy.size();
			if ( m >= 2 && xy[m-2] == corner[j][0] && xy[m-1] == corner[j][1] ) continue;
			xy.push_back(corner[j][0]);
			xy.push_back(corner[j][1]);
		}
		size_t m = xy.size();
		if ( m >= 4 && xy[0] == xy[m-2] && xy[1] == xy[m-1] ) { xy.pop_back(); xy.pop_back(); }
		int np = (int)(xy.size() / 2);
		if ( np < 3 ) continue;                   /* 退化 (線分に潰れた) */
		sink.add_ring(&xy[0], np);
	}
	return TUBE_OK;
}

}  /* namespace srava_geo */

#endif
