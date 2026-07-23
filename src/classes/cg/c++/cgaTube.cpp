/*
 * cgaTube — tube(path[, segs]) の計算本体(ptsCalcBody 派生)= パスに沿って丸断面を掃引した管。
 *
 * **次元ディスパッチ**: パス頂点の位置の長さで 3D / 2D を振り分ける(offset/transform 等と同方針)。
 *   - 3D: path = [[[x,y,z], r], ...] → 3D 折れ線まわりに丸断面を掃引した立体(cgMesh3D)。「蛇」。
 *   - 2D: path = [[[x,y],   r], ...] → 2D 折れ線を半径 r(=半幅)で太らせた帯領域(cgMesh2D)。「2D tube」。
 *         r は 3D と同じく**半径(半幅)**。各頂点で太さが変わる可変幅。丸ジョイント/丸キャップ。
 *
 * 3D アルゴリズム:
 *   - 断面 = 半径 r の円(segs 角形近似・既定 32。circle と同じ精度ピッチ)。
 *   - パスは折れ線そのまま(補間なし)。各頂点で接線に直交する面に断面リングを置く。
 *   - ねじれ防止: rotation-minimizing frame(double-reflection 法。Wang et al. 2008)で枠を運ぶ
 *     → 単純な固定軸投影で出るねじれ/破綻を回避。枠は double 計算 → 座標は K::FT 格納(revolve と同方針)。
 *   - 両端: 半径>0 なら平らなキャップ(中心へファン)で閉じる。半径=0 の端は自然に円錐状に尖って閉じる
 *     (リングが 1 頂点へ潰れる。revolve の軸頂点と同じ扱い)。
 *   巻き方向は外向き右手系(t,N,B)で統一。仕上げに符号付き体積が負なら全反転(外向き保証)。
 *
 * 2D アルゴリズム(stamp-and-union): 頂点ごとに円(半径 r・丸ジョイント/丸キャップ)、線分ごとに
 *   法線方向 ±r の台形を置き、Polygon_set_2 で union する。鋭角ターン・自己重なり・可変幅でも破綻しない
 *   (ストローク輪郭を直接たどる方式が抱える凹側の自己交差を回避)。r=0 端は台形が三角に潰れて尖る。
 *
 * **連続重複頂点は弾かずに間引く**(接線が定義できないので)。spline 等が区間境界で点を重複させても通る。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"cg/c++/cgMesh.h"
#include	"cg/c++/ptscgWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/cgaTube_.h"

#include	<CGAL/Polygon_mesh_processing/triangulate_faces.h>
#include	<CGAL/Polygon_mesh_processing/measure.h>        /* volume */
#include	<CGAL/Polygon_mesh_processing/orientation.h>    /* reverse_face_orientations */
#include	<CGAL/boost/graph/helpers.h>                    /* is_closed */
#include	<CGAL/Polygon_set_2.h>                          /* 2D tube: 帯のスタンプ union */
#include	<CGAL/Boolean_set_operations_2.h>
#include	<vector>
#include	<cmath>

CLASS_TINYSTATE(cg/c++/cgaTube,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	cgaTube_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<ptsWireCacheStreamWriter>	get_writer();
protected:
	virtual void	compute();
	sPtr<cgMesh>	mesh;   /* 3D=cgMesh3D / 2D=cgMesh2D(次元ディスパッチ) */
private:
	TS_DEFARGS
};

TS_END_IMPLEMENT

TS_BEGIN_INTERFACE
#include	"ts2/c++/sRptr.h"
#include	"ts2/c++/sArray.h"
#include	"ts2/c++/stdString.h"
class ptsObject;
class pigData;
class stdString;
class cgMesh;
class ptsWireCacheStreamWriter;
TS_END_INTERFACE

#endif


cgaTube_::cgaTube_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

namespace {
/* 最小限の double 3 ベクトル(枠計算用。最終座標のみ K::FT 化)。 */
struct V3 {
	double x, y, z;
	V3() : x(0), y(0), z(0) {}
	V3(double a, double b, double c) : x(a), y(b), z(c) {}
};
static V3    vadd(const V3& a, const V3& b) { return V3(a.x+b.x, a.y+b.y, a.z+b.z); }
static V3    vsub(const V3& a, const V3& b) { return V3(a.x-b.x, a.y-b.y, a.z-b.z); }
static V3    vscale(const V3& a, double s)  { return V3(a.x*s, a.y*s, a.z*s); }
static double vdot(const V3& a, const V3& b){ return a.x*b.x + a.y*b.y + a.z*b.z; }
static V3    vcross(const V3& a, const V3& b){ return V3(a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x); }
static double vlen(const V3& a)             { return std::sqrt(vdot(a,a)); }
static V3    vnorm(const V3& a)             { double L = vlen(a); return (L>0) ? vscale(a, 1.0/L) : a; }

/* 2D tube: 折れ線 P を半径 R(可変)で太らせた帯を Polygon_set_2 の union で作る。
 * 頂点ごとに円(丸ジョイント/丸キャップ)、線分ごとに法線 ±r の台形。P は dedup 済み(連続重複なし)・z は無視。 */
static sPtr<cgMesh> build_ribbon_2d(const std::vector<V3>& P, const std::vector<double>& R, int segs)
{
	typedef cgMesh::K                      K;
	typedef K::Point_2                     P2;
	typedef CGAL::Polygon_2<K>             Poly2;
	typedef CGAL::Polygon_with_holes_2<K>  Pwh2;
	typedef CGAL::Polygon_set_2<K>         PSet2;
	int n = (int)P.size();

	PSet2 acc;
	/* 頂点ごとの円(半径 r>0)。端も含めて置くので丸キャップになる。 */
	for ( int i = 0 ; i < n ; ++i ) {
		if ( R[i] <= 0.0 ) continue;
		Poly2 c;
		for ( int k = 0 ; k < segs ; ++k ) {
			double a = 2.0 * M_PI * (double)k / (double)segs;
			c.push_back(P2(K::FT(P[i].x + R[i]*std::cos(a)), K::FT(P[i].y + R[i]*std::sin(a))));
		}
		if ( c.is_clockwise_oriented() ) c.reverse_orientation();
		acc.join(c);
	}
	/* 線分ごとの台形(法線方向 ±r)。r=0 端は 2 隅が中心に潰れて三角(尖り)。 */
	for ( int i = 0 ; i < n-1 ; ++i ) {
		double dx = P[i+1].x - P[i].x, dy = P[i+1].y - P[i].y;
		double L = std::sqrt(dx*dx + dy*dy);
		if ( L == 0.0 ) continue;                 /* dedup 済みで通常来ない */
		double nx = -dy / L, ny = dx / L;         /* 進行方向左の単位法線 */
		double r0 = R[i], r1 = R[i+1];
		P2 corner[4] = {
			P2(K::FT(P[i].x   + nx*r0), K::FT(P[i].y   + ny*r0)),   /* 始点・左 */
			P2(K::FT(P[i+1].x + nx*r1), K::FT(P[i+1].y + ny*r1)),   /* 終点・左 */
			P2(K::FT(P[i+1].x - nx*r1), K::FT(P[i+1].y - ny*r1)),   /* 終点・右 */
			P2(K::FT(P[i].x   - nx*r0), K::FT(P[i].y   - ny*r0))    /* 始点・右 */
		};
		std::vector<P2> pts;                      /* 連続重複隅(r=0 で潰れる)を除いて構築 */
		for ( int j = 0 ; j < 4 ; ++j )
			if ( pts.empty() || pts.back() != corner[j] )
				pts.push_back(corner[j]);
		if ( pts.size() >= 2 && pts.front() == pts.back() ) pts.pop_back();
		if ( (int)pts.size() < 3 ) continue;      /* 退化(線分に潰れた) */
		Poly2 q(pts.begin(), pts.end());
		if ( ! q.is_simple() ) continue;
		if ( q.is_clockwise_oriented() ) q.reverse_orientation();
		acc.join(q);
	}

	sPtr<cgMesh2D> out = thNEW(cgMesh2D,());
	std::vector<Pwh2> res;
	res.resize(acc.number_of_polygons_with_holes());
	acc.polygons_with_holes(res.begin());
	out->regions() = res;
	return out;
}
} /* anonymous namespace */

void
cgaTube_::compute()
{
	typedef cgMesh::K K;
	typedef cgMesh::Mesh Mesh;
	typedef Mesh::Vertex_index VI;

	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<pigDataArray> path = ( na > 0 ) ? sPtr<pigDataArray>::d_cast((*args)[0])
	                                     : sPtr<pigDataArray>();
	int segs = ( na > 1 ) ? (int)(*args)[1]->get_int() : 32;   /* 円の辺数(精度ピッチ)。既定 32 */
	if ( segs < 3 ) segs = 3;
	if ( segs > 4096 ) segs = 4096;

	int nraw = path.is_notNull() ? path->length() : 0;
	if ( nraw < 2 ) {
		result = thNEW(pigDataError,(thNEW(stdString,(
		    "tube: needs >= 2 path vertices ([[[x,y,z],r],...] for 3D / [[[x,y],r],...] for 2D)"))));
		return;
	}

	/* ---- パス読み取り: 各要素 [位置, r]。位置の長さ(2/3)で 2D/3D を判定(先頭頂点で確定) ---- */
	std::vector<V3>     Praw(nraw);
	std::vector<double> Rraw(nraw);
	int dim = 0;
	for ( int i = 0 ; i < nraw ; ++i ) {
		sPtr<pigDataArray> pr = sPtr<pigDataArray>::d_cast(
		    path->get_ix(thNEW(pigDataInteger,((INTEGER64)i))));
		if ( ! pr.is_notNull() || pr->length() < 2 ) {
			result = thNEW(pigDataError,(thNEW(stdString,(
			    "tube: each vertex must be [pos, r] (pos=[x,y,z] or [x,y])"))));
			return;
		}
		sPtr<pigDataArray> pos = sPtr<pigDataArray>::d_cast(
		    pr->get_ix(thNEW(pigDataInteger,((INTEGER64)0))));
		int pl = pos.is_notNull() ? pos->length() : 0;
		if ( i == 0 ) dim = ( pl >= 3 ) ? 3 : 2;   /* 先頭頂点で次元を確定 */
		if ( pl < dim ) {
			result = thNEW(pigDataError,(thNEW(stdString,(
			    dim == 3 ? "tube: vertex position must be [x,y,z] (3D path)"
			             : "tube: vertex position must be [x,y] (2D path)"))));
			return;
		}
		Praw[i] = V3(pos->get_ix(thNEW(pigDataInteger,((INTEGER64)0)))->get_flt(),
		             pos->get_ix(thNEW(pigDataInteger,((INTEGER64)1)))->get_flt(),
		             dim == 3 ? pos->get_ix(thNEW(pigDataInteger,((INTEGER64)2)))->get_flt() : 0.0);
		Rraw[i] = pr->get_ix(thNEW(pigDataInteger,((INTEGER64)1)))->get_flt();
		if ( Rraw[i] < 0.0 ) {
			result = thNEW(pigDataError,(thNEW(stdString,("tube: radius must be >= 0"))));
			return;
		}
	}

	/* ---- 連続重複頂点を間引く(弾かない)。接線が定義できないため。spline 等の区間境界重複を吸収。
	 *      残す側の半径を採る(最初の出現)。 ---- */
	std::vector<V3>     P;
	std::vector<double> R;
	for ( int i = 0 ; i < nraw ; ++i ) {
		if ( i > 0 && vlen(vsub(Praw[i], P.back())) == 0.0 ) continue;
		P.push_back(Praw[i]);
		R.push_back(Rraw[i]);
	}
	int n = (int)P.size();
	if ( n < 2 ) {
		result = thNEW(pigDataError,(thNEW(stdString,(
		    "tube: needs >= 2 distinct path vertices (all given vertices coincide)"))));
		return;
	}

	/* ---- 2D: 折れ線を半径 r で太らせた帯領域(可変幅・丸ジョイント)。stamp-and-union。 ---- */
	if ( dim == 2 ) {
		mesh = build_ribbon_2d(P, R, segs);
		return;
	}

	/* ======================= 以下 3D: 折れ線まわりの掃引立体 ======================= */
	sPtr<cgMesh3D> m3 = thNEW(cgMesh3D,());

	/* ---- 各頂点の接線(隣接セグメント方向の平均。端点は片側) ---- */
	std::vector<V3> T(n);
	for ( int i = 0 ; i < n ; ++i ) {
		V3 din, dout;
		if ( i > 0 ) {
			din = vsub(P[i], P[i-1]);
			if ( vlen(din) == 0.0 ) {
				result = thNEW(pigDataError,(thNEW(stdString,("tube: duplicate consecutive path vertices"))));
				return;
			}
			din = vnorm(din);
		}
		if ( i < n-1 ) {
			dout = vsub(P[i+1], P[i]);
			if ( vlen(dout) == 0.0 ) {
				result = thNEW(pigDataError,(thNEW(stdString,("tube: duplicate consecutive path vertices"))));
				return;
			}
			dout = vnorm(dout);
		}
		V3 t = ( i == 0 ) ? dout : ( i == n-1 ) ? din : vadd(din, dout);
		t = vnorm(t);
		if ( vlen(t) == 0.0 ) {   /* 180°折り返し(din ≈ -dout) → 片側採用 */
			t = vnorm(dout.x==0&&dout.y==0&&dout.z==0 ? din : dout);
		}
		T[i] = t;
	}

	/* ---- rotation-minimizing frame(double reflection)で法線 N を運ぶ。B = T × N ---- */
	std::vector<V3> N(n), B(n);
	/* 初期法線: T[0] に直交する任意の単位ベクトル。 */
	{
		V3 up = ( std::fabs(T[0].z) < 0.9 ) ? V3(0,0,1) : V3(0,1,0);
		V3 r0 = vsub(up, vscale(T[0], vdot(up, T[0])));
		N[0] = vnorm(r0);
		B[0] = vnorm(vcross(T[0], N[0]));
	}
	for ( int i = 0 ; i < n-1 ; ++i ) {
		V3 v1 = vsub(P[i+1], P[i]);
		double c1 = vdot(v1, v1);
		V3 rL, tL;
		if ( c1 > 0.0 ) {
			rL = vsub(N[i], vscale(v1, (2.0/c1) * vdot(v1, N[i])));
			tL = vsub(T[i], vscale(v1, (2.0/c1) * vdot(v1, T[i])));
		} else { rL = N[i]; tL = T[i]; }
		V3 v2 = vsub(T[i+1], tL);
		double c2 = vdot(v2, v2);
		V3 rNext = ( c2 > 0.0 ) ? vsub(rL, vscale(v2, (2.0/c2) * vdot(v2, rL))) : rL;
		N[i+1] = vnorm(rNext);
		B[i+1] = vnorm(vcross(T[i+1], N[i+1]));
	}

	/* ---- 円周サンプルの cos/sin ---- */
	std::vector<double> cs(segs), sn(segs);
	for ( int k = 0 ; k < segs ; ++k ) {
		double a = 2.0 * M_PI * (double)k / (double)segs;
		cs[k] = std::cos(a); sn[k] = std::sin(a);
	}

	Mesh& m = m3->mesh();

	/* ---- 各頂点に断面リング(r>0)or 軸頂点(r==0)を割当 ---- */
	std::vector<std::vector<VI> > rv(n);   /* r>0: segs 頂点 / r==0: 1 頂点(尖端) */
	for ( int i = 0 ; i < n ; ++i ) {
		if ( R[i] == 0.0 ) {
			rv[i].push_back(m.add_vertex(K::Point_3(K::FT(P[i].x), K::FT(P[i].y), K::FT(P[i].z))));
		} else {
			rv[i].resize(segs);
			for ( int k = 0 ; k < segs ; ++k ) {
				V3 off = vadd(vscale(N[i], R[i]*cs[k]), vscale(B[i], R[i]*sn[k]));
				V3 p   = vadd(P[i], off);
				rv[i][k] = m.add_vertex(K::Point_3(K::FT(p.x), K::FT(p.y), K::FT(p.z)));
			}
		}
	}

	/* ---- 側壁: 隣接リング L=i, U=i+1 を角度方向に帯状に縫合 ----
	 * 面 [Lk, Lk1, Uk1, Uk] は外向き右手系。軸頂点側は同一 VI に潰れて三角になる。 */
	for ( int i = 0 ; i < n-1 ; ++i ) {
		bool fullL = ( (int)rv[i].size()   == segs );
		bool fullU = ( (int)rv[i+1].size() == segs );
		if ( ! fullL && ! fullU ) {
			result = thNEW(pigDataError,(thNEW(stdString,(
			    "tube: two consecutive zero-radius vertices (degenerate segment)"))));
			return;
		}
		for ( int k = 0 ; k < segs ; ++k ) {
			int k1 = (k + 1) % segs;
			VI Lk  = fullL ? rv[i][k]    : rv[i][0];
			VI Lk1 = fullL ? rv[i][k1]   : rv[i][0];
			VI Uk  = fullU ? rv[i+1][k]  : rv[i+1][0];
			VI Uk1 = fullU ? rv[i+1][k1] : rv[i+1][0];
			std::vector<VI> f;
			f.push_back(Lk);
			if ( Lk1 != f.back() ) f.push_back(Lk1);
			if ( Uk1 != f.back() && Uk1 != f.front() ) f.push_back(Uk1);
			if ( Uk  != f.back() && Uk  != f.front() ) f.push_back(Uk);
			if ( (int)f.size() >= 3 ) m.add_face(f);
		}
	}

	/* ---- 平キャップ: 端リングが r>0 なら中心頂点へファンで閉じる(外向き) ----
	 * 始端は -T 向き(逆順)、終端は +T 向き。r==0 の端は側壁ファンで既に閉じている。 */
	if ( (int)rv[0].size() == segs ) {
		VI c0 = m.add_vertex(K::Point_3(K::FT(P[0].x), K::FT(P[0].y), K::FT(P[0].z)));
		for ( int k = 0 ; k < segs ; ++k ) {
			int k1 = (k + 1) % segs;
			std::vector<VI> f; f.push_back(c0); f.push_back(rv[0][k1]); f.push_back(rv[0][k]);
			m.add_face(f);
		}
	}
	if ( (int)rv[n-1].size() == segs ) {
		VI c1 = m.add_vertex(K::Point_3(K::FT(P[n-1].x), K::FT(P[n-1].y), K::FT(P[n-1].z)));
		for ( int k = 0 ; k < segs ; ++k ) {
			int k1 = (k + 1) % segs;
			std::vector<VI> f; f.push_back(c1); f.push_back(rv[n-1][k]); f.push_back(rv[n-1][k1]);
			m.add_face(f);
		}
	}

	CGAL::Polygon_mesh_processing::triangulate_faces(m);   /* 側壁の四角形を三角化 */

	/* 念のため外向き保証: 閉じていて符号付き体積が負なら全反転。 */
	if ( CGAL::is_closed(m) ) {
		if ( CGAL::Polygon_mesh_processing::volume(m) < K::FT(0) )
			CGAL::Polygon_mesh_processing::reverse_face_orientations(m);
	}
	mesh = m3;
}

sPtr<ptsWireCacheStreamWriter>
cgaTube_::get_writer()
{
	return thNEW(ptscgWireCacheStreamWriterMesh,(parent, target, mesh));
}
