/*
 * cgaRevolve — revolve(poly2d, angle) の計算本体(ptsCalcBody 派生)= 2D→3D の回転体(旋盤)。
 * 入力 cgMesh2D のプロファイル(x=Y 軸からの半径≥0, y=高さ)を Y 軸まわりに回して立体化。
 *   3D 点 = (px·cosθ, py, px·sinθ)。角度は cos/sin の double を K::FT に格納(EPECK 座標は維持)。
 *   全周 360°=端キャップ不要(角度方向ラップ)。部分角(<360)=ラップせず両端に CDT 三角化のキャップ。
 * sphere は半円プロファイルの revolve に相当。穴あきプロファイルも対応。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"cg/c++/cgMesh.h"
#include	"cg/c++/ptscgWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/cgaRevolve_.h"

#include	<CGAL/Constrained_Delaunay_triangulation_2.h>
#include	<CGAL/Triangulation_face_base_with_info_2.h>
#include	<CGAL/Polygon_mesh_processing/triangulate_faces.h>
#include	<math.h>
#include	<map>
#include	<list>
#include	<vector>

CLASS_TINYSTATE(cg/c++/cgaRevolve,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	cgaRevolve_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<ptsWireCacheStreamWriter>	get_writer();
protected:
	virtual void	compute();
	sPtr<cgMesh3D>	mesh;
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
class cgMesh3D;
class ptsWireCacheStreamWriter;
TS_END_INTERFACE

#endif


cgaRevolve_::cgaRevolve_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

namespace {
typedef cgMesh::K RK;
struct RFaceInfo { int nest; bool in_domain() const { return nest >= 0 && (nest % 2) == 1; } };
typedef CGAL::Triangulation_vertex_base_2<RK>				RVb;
typedef CGAL::Triangulation_face_base_with_info_2<RFaceInfo, RK>		RFbb;
typedef CGAL::Constrained_triangulation_face_base_2<RK, RFbb>		RFb;
typedef CGAL::Triangulation_data_structure_2<RVb, RFb>			RTDS;
typedef CGAL::Constrained_Delaunay_triangulation_2<RK, RTDS, CGAL::Exact_predicates_tag> RCDT;

static void rmark(RCDT& cdt) {
	for ( RCDT::All_faces_iterator f = cdt.all_faces_begin() ; f != cdt.all_faces_end() ; ++f )
		f->info().nest = -1;
	std::list<RCDT::Edge> border;
	std::list<RCDT::Face_handle> q; q.push_back(cdt.infinite_face());
	cdt.infinite_face()->info().nest = 0;
	/* flood: 制約を跨ぐたび nest+1 */
	while ( ! q.empty() || ! border.empty() ) {
		if ( q.empty() ) {
			RCDT::Edge e = border.front(); border.pop_front();
			RCDT::Face_handle n = e.first->neighbor(e.second);
			if ( n->info().nest != -1 ) continue;
			n->info().nest = e.first->info().nest + 1;
			q.push_back(n);
		}
		while ( ! q.empty() ) {
			RCDT::Face_handle fh = q.front(); q.pop_front();
			for ( int i = 0 ; i < 3 ; ++i ) {
				RCDT::Edge e(fh, i);
				RCDT::Face_handle n = fh->neighbor(i);
				if ( n->info().nest != -1 ) continue;
				if ( cdt.is_constrained(e) ) border.push_back(e);
				else { n->info().nest = fh->info().nest; q.push_back(n); }
			}
		}
	}
}
} /* anonymous namespace */

void
cgaRevolve_::compute()
{
	typedef cgMesh::K K;
	typedef cgMesh::Mesh Mesh;
	typedef Mesh::Vertex_index VI;
	typedef cgMesh2D::Polygon_2 Poly2;

	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<cgMesh2D> in    = ( na > 0 ) ? sPtr<cgMesh2D>::d_cast((*args)[0]) : sPtr<cgMesh2D>();
	double         angle = ( na > 1 ) ? (*args)[1]->get_flt() : 360.0;
	int            nseg  = ( na > 2 ) ? (int)(*args)[2]->get_int() : 32;   /* 全周の分割数(回転ピッチ) */
	if ( nseg < 3 ) nseg = 3;
	if ( nseg > 4096 ) nseg = 4096;   /* 上限(暴走防止) */

	mesh = thNEW(cgMesh3D,());
	if ( ! in.is_notNull() ) {
		result = thNEW(pigDataError,(thNEW(stdString,("revolve: needs a 2D polygon"))));
		return;
	}
	if ( angle <= 0.0 ) {
		result = thNEW(pigDataError,(thNEW(stdString,("revolve: angle must be > 0"))));
		return;
	}
	if ( angle > 360.0 ) angle = 360.0;

	const int FULLSEGS = nseg;   /* 全周 360°相当の分割数(ユーザ指定・既定 32) */
	bool full = ( angle >= 360.0 );
	double aRad = ( full ? 2.0 * M_PI : angle * M_PI / 180.0 );
	int segs = full ? FULLSEGS : (int)(FULLSEGS * angle / 360.0 + 0.5);
	if ( segs < 2 ) segs = 2;
	int nPos = full ? segs : segs + 1;   /* 角度位置の個数(部分角は両端含む) */

	std::vector<double> cs(nPos), sn(nPos);
	for ( int j = 0 ; j < nPos ; ++j ) {
		double t = full ? (2.0 * M_PI * (double)j / (double)segs)
		                : (aRad * (double)j / (double)segs);
		cs[j] = ::cos(t); sn[j] = ::sin(t);
	}

	Mesh& m = mesh->mesh();
	std::vector<cgMesh2D::Pwh_2>& regs = in->regions();

	for ( std::size_t r = 0 ; r < regs.size() ; ++r ) {
		const cgMesh2D::Pwh_2& pwh = regs[r];
		std::vector<const Poly2*> plist;
		plist.push_back(&pwh.outer_boundary());
		for ( cgMesh2D::Pwh_2::Hole_const_iterator hi = pwh.holes_begin() ; hi != pwh.holes_end() ; ++hi )
			plist.push_back(&(*hi));

		/* 全リングをフラット化(グローバル頂点 g)。各 g に軸頂点 or nPos 個の回転コピーを割当。 */
		std::vector<K::Point_2> gp;
		std::vector<std::pair<int,int> > ringRange;   /* (start, count) per ring */
		bool badRadius = false;
		for ( std::size_t pi = 0 ; pi < plist.size() ; ++pi ) {
			const Poly2& ring = *plist[pi];
			int start = (int)gp.size(), cnt = 0;
			for ( Poly2::Vertex_const_iterator it = ring.vertices_begin() ; it != ring.vertices_end() ; ++it ) {
				if ( CGAL::to_double(it->x()) < 0.0 ) badRadius = true;
				gp.push_back(*it); cnt++;
			}
			ringRange.push_back(std::make_pair(start, cnt));
		}
		if ( badRadius ) {
			result = thNEW(pigDataError,(thNEW(stdString,("revolve: profile x (radius) must be >= 0"))));
			return;
		}
		int ng = (int)gp.size();
		std::vector<int> isAxis(ng);
		std::vector<VI>  axisV(ng);
		std::vector<std::vector<VI> > rotV(ng);
		for ( int g = 0 ; g < ng ; ++g ) {
			double x = CGAL::to_double(gp[g].x()), y = CGAL::to_double(gp[g].y());
			if ( x == 0.0 ) {
				isAxis[g] = 1;
				axisV[g]  = m.add_vertex(K::Point_3(K::FT(0.0), K::FT(y), K::FT(0.0)));
			} else {
				isAxis[g] = 0;
				rotV[g].resize(nPos);
				for ( int j = 0 ; j < nPos ; ++j )
					rotV[g][j] = m.add_vertex(K::Point_3(K::FT(x*cs[j]), K::FT(y), K::FT(x*sn[j])));
			}
		}

		/* 側壁: 各リングの辺 i→i2 を角度方向に帯状に。全周はラップ、部分角は j=0..segs-1。 */
		for ( std::size_t ri = 0 ; ri < ringRange.size() ; ++ri ) {
			int start = ringRange[ri].first, cnt = ringRange[ri].second;
			for ( int e = 0 ; e < cnt ; ++e ) {
				int gi = start + e, gj = start + (e + 1) % cnt;
				if ( isAxis[gi] && isAxis[gj] ) continue;
				int jmax = full ? segs : segs;   /* セグメント数(全周は wrap で segs 本) */
				for ( int j = 0 ; j < jmax ; ++j ) {
					int j2 = full ? (j + 1) % segs : (j + 1);
					VI a = isAxis[gi] ? axisV[gi] : rotV[gi][j];
					VI b = isAxis[gj] ? axisV[gj] : rotV[gj][j];
					VI c = isAxis[gj] ? axisV[gj] : rotV[gj][j2];
					VI d = isAxis[gi] ? axisV[gi] : rotV[gi][j2];
					std::vector<VI> f;
					f.push_back(a);
					if ( b != f.back() ) f.push_back(b);
					if ( c != f.back() && c != f.front() ) f.push_back(c);
					if ( d != f.back() && d != f.front() ) f.push_back(d);
					if ( (int)f.size() >= 3 ) m.add_face(f);
				}
			}
		}

		/* 部分角: 両端(j=0 と j=segs)に CDT 三角化のキャップ。CDT 頂点 → グローバル g を対応。 */
		if ( ! full ) {
			RCDT cdt;
			std::map<RCDT::Vertex_handle, int> vh2g;
			for ( std::size_t pi = 0 ; pi < plist.size() ; ++pi ) {
				const Poly2& ring = *plist[pi];
				int start = ringRange[pi].first, cnt = ringRange[pi].second;
				std::vector<RCDT::Vertex_handle> vh; vh.reserve(cnt);
				int e = 0;
				for ( Poly2::Vertex_const_iterator it = ring.vertices_begin() ; it != ring.vertices_end() ; ++it, ++e ) {
					RCDT::Vertex_handle h = cdt.insert(*it);
					vh.push_back(h); vh2g[h] = start + e;
				}
				for ( int i = 0 ; i < cnt ; ++i )
					cdt.insert_constraint(vh[i], vh[(i + 1) % cnt]);
			}
			rmark(cdt);
			for ( RCDT::Finite_faces_iterator fc = cdt.finite_faces_begin() ; fc != cdt.finite_faces_end() ; ++fc ) {
				if ( ! fc->info().in_domain() ) continue;
				int ga = vh2g[fc->vertex(0)], gb = vh2g[fc->vertex(1)], gc = vh2g[fc->vertex(2)];
				/* θ=0 キャップ(法線 -tangent=-z 方向)= 逆順。θ=angle キャップ = 順。 */
				#define RVAT(g, j) ( isAxis[g] ? axisV[g] : rotV[g][j] )
				std::vector<VI> c0; c0.push_back(RVAT(ga,0)); c0.push_back(RVAT(gc,0)); c0.push_back(RVAT(gb,0));
				std::vector<VI> cA; cA.push_back(RVAT(ga,segs)); cA.push_back(RVAT(gb,segs)); cA.push_back(RVAT(gc,segs));
				#undef RVAT
				m.add_face(c0);
				m.add_face(cA);
			}
		}
	}
	CGAL::Polygon_mesh_processing::triangulate_faces(m);
}

sPtr<ptsWireCacheStreamWriter>
cgaRevolve_::get_writer()
{
	return thNEW(ptscgWireCacheStreamWriterMesh,(parent, target, mesh));
}
