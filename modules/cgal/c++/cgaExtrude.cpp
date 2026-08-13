/*
 * cgaExtrude — extrude(poly2d, h) の計算本体(ptsCalcBody 派生)= 2D→3D の橋。
 * 入力 cgMesh2D の各穴あき多角形を高さ h で押し出して角柱メッシュ(cgMesh3D)を作る。
 *   - 天/底キャップ: 外周+穴を制約にした CDT(制約付き Delaunay)で材料側の三角形を集める(穴=トンネル)。
 *   - 側壁: 外周(CCW)と各穴(CW)の各辺を四角形で。穴の壁は内向き(トンネル面)。
 * prism(n,h,r) は ngon の extrude と同形。穴ありも対応(2D difference の結果を立体化できる)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"cg/c++/cgMesh.h"
#include	"cg/c++/ptscgWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/cgaExtrude_.h"

#include	<CGAL/Constrained_Delaunay_triangulation_2.h>
#include	<CGAL/Triangulation_face_base_with_info_2.h>
#include	<CGAL/Polygon_mesh_processing/triangulate_faces.h>
#include	<map>
#include	<list>
#include	<vector>

CLASS_TINYSTATE(cg/c++/cgaExtrude,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	cgaExtrude_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

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
TS_END_INTERFACE

#endif


cgaExtrude_::cgaExtrude_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

namespace {
typedef cgMesh::K K;
struct FaceInfo { int nest; bool in_domain() const { return nest >= 0 && (nest % 2) == 1; } };
typedef CGAL::Triangulation_vertex_base_2<K>				Vb;
typedef CGAL::Triangulation_face_base_with_info_2<FaceInfo, K>		Fbb;
typedef CGAL::Constrained_triangulation_face_base_2<K, Fbb>		Fb;
typedef CGAL::Triangulation_data_structure_2<Vb, Fb>			TDS;
typedef CGAL::Constrained_Delaunay_triangulation_2<K, TDS, CGAL::Exact_predicates_tag> CDT;

/* CGAL 標準: 無限面から制約辺を跨ぐたび nest を +1 する flood fill。奇数 nest = 材料(in_domain)。 */
static void mark_domains(CDT& cdt, CDT::Face_handle start, int index, std::list<CDT::Edge>& border) {
	if ( start->info().nest != -1 ) return;
	std::list<CDT::Face_handle> queue;
	queue.push_back(start);
	while ( ! queue.empty() ) {
		CDT::Face_handle fh = queue.front(); queue.pop_front();
		if ( fh->info().nest != -1 ) continue;
		fh->info().nest = index;
		for ( int i = 0 ; i < 3 ; ++i ) {
			CDT::Edge e(fh, i);
			CDT::Face_handle n = fh->neighbor(i);
			if ( n->info().nest == -1 ) {
				if ( cdt.is_constrained(e) ) border.push_back(e);
				else queue.push_back(n);
			}
		}
	}
}
static void mark_domains(CDT& cdt) {
	for ( CDT::All_faces_iterator f = cdt.all_faces_begin() ; f != cdt.all_faces_end() ; ++f )
		f->info().nest = -1;
	std::list<CDT::Edge> border;
	mark_domains(cdt, cdt.infinite_face(), 0, border);
	while ( ! border.empty() ) {
		CDT::Edge e = border.front(); border.pop_front();
		CDT::Face_handle n = e.first->neighbor(e.second);
		if ( n->info().nest == -1 )
			mark_domains(cdt, n, e.first->info().nest + 1, border);
	}
}
} /* anonymous namespace */

void
cgaExtrude_::compute()
{
	typedef cgMesh::Mesh Mesh;
	typedef Mesh::Vertex_index VI;
	typedef cgMesh2D::Polygon_2 Poly2;

	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<cgMesh2D> in = ( na > 0 ) ? sPtr<cgMesh2D>::d_cast((*args)[0]) : sPtr<cgMesh2D>();
	double h = ( na > 1 ) ? (*args)[1]->get_flt() : 1.0;

	mesh = thNEW(cgMesh3D,());
	if ( ! in.is_notNull() ) {
		result = thNEW(pigDataError,(thNEW(stdString,("extrude: needs a 2D polygon"))));
		return;
	}
	if ( h == 0.0 ) {
		result = thNEW(pigDataError,(thNEW(stdString,("extrude: height must be non-zero"))));
		return;
	}

	Mesh& m = mesh->mesh();
	std::vector<cgMesh2D::Pwh_2>& regs = in->regions();

	for ( std::size_t r = 0 ; r < regs.size() ; ++r ) {
		const cgMesh2D::Pwh_2& pwh = regs[r];
		if ( pwh.outer_boundary().size() < 3 ) continue;

		/* 全リング(外周 CCW + 穴 CW)を CDT に制約挿入。頂点ハンドルをリングごとに保持。 */
		CDT cdt;
		std::vector<std::vector<CDT::Vertex_handle> > rings;
		std::vector<const Poly2*> plist;
		plist.push_back(&pwh.outer_boundary());
		for ( cgMesh2D::Pwh_2::Hole_const_iterator hi = pwh.holes_begin() ; hi != pwh.holes_end() ; ++hi )
			plist.push_back(&(*hi));
		for ( std::size_t pi = 0 ; pi < plist.size() ; ++pi ) {
			const Poly2& ring = *plist[pi];
			int n = (int)ring.size();
			if ( n < 3 ) continue;
			std::vector<CDT::Vertex_handle> vh;
			vh.reserve(n);
			for ( Poly2::Vertex_const_iterator it = ring.vertices_begin() ; it != ring.vertices_end() ; ++it )
				vh.push_back(cdt.insert(*it));
			for ( int i = 0 ; i < n ; ++i )
				cdt.insert_constraint(vh[i], vh[(i + 1) % n]);
			rings.push_back(vh);
		}
		mark_domains(cdt);

		/* 各 CDT 頂点に 底(z=0)/天(z=h)の Surface_mesh 頂点を割り当てる。 */
		std::map<CDT::Vertex_handle, std::pair<VI,VI> > vmap;
		for ( CDT::Finite_vertices_iterator v = cdt.finite_vertices_begin() ; v != cdt.finite_vertices_end() ; ++v ) {
			VI bot = m.add_vertex(K::Point_3(v->point().x(), v->point().y(), K::FT(0.0)));
			VI top = m.add_vertex(K::Point_3(v->point().x(), v->point().y(), K::FT(h)));
			vmap[v] = std::make_pair(bot, top);
		}

		/* キャップ: 材料側(in_domain)三角形を 天(CCW=+z)/ 底(逆順=-z)に。 */
		for ( CDT::Finite_faces_iterator f = cdt.finite_faces_begin() ; f != cdt.finite_faces_end() ; ++f ) {
			if ( ! f->info().in_domain() ) continue;
			std::pair<VI,VI>& a = vmap[f->vertex(0)];
			std::pair<VI,VI>& b = vmap[f->vertex(1)];
			std::pair<VI,VI>& c = vmap[f->vertex(2)];
			std::vector<VI> topF; topF.push_back(a.second); topF.push_back(b.second); topF.push_back(c.second);
			std::vector<VI> botF; botF.push_back(a.first);  botF.push_back(c.first);  botF.push_back(b.first);
			m.add_face(topF);
			m.add_face(botF);
		}

		/* 側壁: 各リングの辺 a→b を四角形(bot[a],bot[b],top[b],top[a])で。外周は外向き・穴は内向き。 */
		for ( std::size_t ri = 0 ; ri < rings.size() ; ++ri ) {
			std::vector<CDT::Vertex_handle>& ring = rings[ri];
			int n = (int)ring.size();
			for ( int i = 0 ; i < n ; ++i ) {
				std::pair<VI,VI>& va = vmap[ring[i]];
				std::pair<VI,VI>& vb = vmap[ring[(i + 1) % n]];
				std::vector<VI> q;
				q.push_back(va.first); q.push_back(vb.first); q.push_back(vb.second); q.push_back(va.second);
				m.add_face(q);
			}
		}
	}
	CGAL::Polygon_mesh_processing::triangulate_faces(m);   /* 側壁の四角形を三角化 */
}

/* この演算の結果 (#3406, 2026-07-30 メモ: get_body/get_result を統一)。エラー時は
 * compute() が result にエラー値を残して mesh を未設定のまま return するので result 優先。
 * 成功時は result=thNULL のままmeshを返す。保存(Writer 起動)は agent が出力 pigDataCache
 * の set_body 経由で行う。 */
sPtr<pigData>
cgaExtrude_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
