/*
 * cgMesh3D — 3D Surface_mesh(EPECK)の多態メソッド実装(Step B)。
 * ブーリアン(corefinement)・アフィン変換・codec・get_str/factory をここに集約。CGAL リンク側で compile。
 * cgaUnion 等の計算本体はこれらを virtual 越しに呼ぶだけ(次元非依存)。
 */
#include	"cg/c++/cgMeshCgal.h"
#include	<CGAL/Side_of_triangle_mesh.h>   /* ★ #3527: part_at の **厳密** な内外判定 */
#include	<unordered_map>                   /* ★ #3527: face_verts の 頂点 → 索引 */
#include	"cg/c++/cgaMeshCodec.h"
#include	"ts2/c++/stdString.h"
#include	"common/blockframe.h"   /* ★ #3507: SNC のブロック列を終端まで読み捨てる */
#include	"common/geodesic.h"   /* sphere/icosphere の測地球生成 (manifold と共通) */
#include	"common/affine.h"     /* ★ #3533: 平面 → 枠 の表は plane_frame() 1 箇所 */

#include	<CGAL/Polygon_mesh_processing/IO/polygon_mesh_io.h>   /* ★ #3535②: 拡張子判別 + soup repair */
#include	<CGAL/Constrained_Delaunay_triangulation_2.h>          /* ★ #3535②: extrude/revolve のキャップ */
#include	<CGAL/Triangulation_face_base_with_info_2.h>
#include	<list>
#include	<map>
#include	<CGAL/Polygon_mesh_processing/corefinement.h>
#include	<CGAL/Aff_transformation_3.h>
#include	<CGAL/Polygon_mesh_processing/orientation.h>   /* reverse_face_orientations */
#include	<CGAL/boost/graph/IO/polygon_mesh_io.h>        /* 拡張子で OFF/STL/OBJ/PLY 書き出し */
#include	<sstream>                       /* 3D offset = Minkowski(球) */
#include	<CGAL/boost/graph/convert_nef_polyhedron_to_polygon_mesh.h>
#include	<CGAL/boost/graph/generators.h>                /* make_icosahedron */
#include	<CGAL/boost/graph/copy_face_graph.h>           /* combine(単純連結・corefinement なし) */
#include	<boost/property_map/property_map.hpp>          /* combine の face_to_face_map(色保持) */
#include	<CGAL/Polygon_mesh_processing/triangulate_faces.h>
#include	<CGAL/Polygon_mesh_processing/measure.h>   /* area / volume */
#include	<CGAL/Polygon_mesh_processing/self_intersections.h>   /* does_self_intersect(valid 検査) */
#include	<CGAL/Polygon_mesh_processing/autorefinement.h>       /* autorefine(repair) */
#include	<CGAL/Polygon_mesh_processing/connected_components.h>  /* #3514: 面の連結成分 (シェル) */
#include	<CGAL/Polygon_mesh_slicer.h>                          /* section(平面で切る) */
#include	<CGAL/Polygon_2.h>
#include	<CGAL/Polygon_with_holes_2.h>
#include	<CGAL/Polygon_repair/repair.h>                        /* even-odd で断面を塗り領域化 */
#include	<CGAL/number_utils.h>                                 /* to_double */
#include	<CGAL/boost/graph/helpers.h>               /* is_closed */
#include	<CGAL/AABB_tree.h>                          /* 近接(closest_point) */
#include	<CGAL/AABB_traits_3.h>
#include	<CGAL/AABB_face_graph_triangle_primitive.h>
#include	<CGAL/Exact_predicates_inexact_constructions_kernel.h>   /* SDF レイ投射用の double カーネル */
#include	<map>
#include	<vector>
#include	<algorithm>
#include	<thread>
#include	<optional>
#include	<variant>
#include	<string>
#include	<cmath>
#include	<stdio.h>      /* AMF/3MF 書き出し(自前 XML + zip) */
#include	"common/mesh3mf.h"   /* AMF/3MF ライタ本体(manifold.so と共有) */
#include	<string.h>     /* strrchr */
#include	<strings.h>    /* strcasecmp */
#include	<stdint.h>     /* uint32_t(zip ヘッダ) */

/* ★★ #3545: ctor / dtor の実体はここ (ヘッダに置かない理由は cgMesh.h の宣言のところ)。 */
cgMesh3D::cgMesh3D(sPtr<pigInfo> i)
    : cgMesh(i), box_(new cgMesh3DBox()) {}

cgMesh3D::~cgMesh3D() { delete box_; }

/* ★★ #3545 段 2: 素の三角形スープを積む。**CGAL の受け皿はここだけ**。
 *   ⚠ cgTriSink を使う生成 op (cone / cylinder / torus / tetrahedron / pyramid) は
 *     ここへ配列を渡すだけになり、CGAL のヘッダを 1 枚も引かなくなる。
 *   ★ 既存の面は消さずに **足す** (Sink 契約が「積む」ものなので、それに揃える)。 */
void
cgMesh3D::build_from_triangles(const double *xyz, int nv, const int *idx, int nt)
{
	Mesh& m = box().m;
	std::vector<Mesh::Vertex_index> vs;
	vs.reserve((size_t)nv);
	for ( int i = 0 ; i < nv ; ++i )
		vs.push_back(m.add_vertex(Point_3(xyz[i*3], xyz[i*3+1], xyz[i*3+2])));
	for ( int t = 0 ; t < nt ; ++t )
		m.add_face(vs[idx[t*3]], vs[idx[t*3+1]], vs[idx[t*3+2]]);
}

/* ★★ #3545 段 2: 測地球 (sphere / icosphere)。生成器 (common/geodesic.h) はカーネル非依存で、
 *   CGAL に触るのは下の cga_make_geodesic だけ ⇒ op は分割数と半径を渡すだけになる。 */
void
cgMesh3D::build_geodesic(int seed, int n, double r)
{
	cga_make_geodesic(box().m, seed, n, r);
}

/* ---- get_str(RTTI/vtable anchor)。基底 cgMesh::get_str は cgMesh.cpp に ---- */
/* ★★ #3535②: プリミティブの構成 (op から引き取った)。理由は cgMesh.h の宣言のところ。
 *   ⚠ 検査はしない — 呼び手 (op) が自分の文言でエラーを返す。ここは作るだけ。 */
void
cgMesh3D::build_box(double w, double h, double d)
{
	Mesh& m = box().m;
	typedef Point_3 cgP;
	CGAL::make_hexahedron(
		cgP(0,0,0), cgP(w,0,0), cgP(w,h,0), cgP(0,h,0),
		cgP(0,0,d), cgP(w,0,d), cgP(w,h,d), cgP(0,h,d), m);
	CGAL::Polygon_mesh_processing::triangulate_faces(m);
}

/* ★★ #3535②: **制約付き Delaunay (CDT)** の道具一式 (cgaExtrude.cpp から移した)。
 *   extrude / revolve のキャップ三角形化で共用する。⚠ op 側に置くと CGAL の可変大域状態
 *   (_error_handler / _error_behaviour) の実体が cgal.so にできる (理由は cgMesh.h の宣言)。 */
namespace {
typedef K CgK;
struct CgFaceInfo { int nest; bool in_domain() const { return nest >= 0 && (nest % 2) == 1; } };
typedef CGAL::Triangulation_vertex_base_2<CgK>                            CgVb;
typedef CGAL::Triangulation_face_base_with_info_2<CgFaceInfo, CgK>        CgFbb;
typedef CGAL::Constrained_triangulation_face_base_2<CgK, CgFbb>           CgFb;
typedef CGAL::Triangulation_data_structure_2<CgVb, CgFb>                  CgTDS;
typedef CGAL::Constrained_Delaunay_triangulation_2<CgK, CgTDS, CGAL::Exact_predicates_tag> CgCDT;

/* CGAL 標準: 無限面から制約辺を跨ぐたび nest を +1 する flood fill。奇数 nest = 材料(in_domain)。 */
void cg_mark_domains(CgCDT& cdt, CgCDT::Face_handle start, int index, std::list<CgCDT::Edge>& border) {
	if ( start->info().nest != -1 ) return;
	std::list<CgCDT::Face_handle> queue;
	queue.push_back(start);
	while ( ! queue.empty() ) {
		CgCDT::Face_handle fh = queue.front(); queue.pop_front();
		if ( fh->info().nest != -1 ) continue;
		fh->info().nest = index;
		for ( int i = 0 ; i < 3 ; ++i ) {
			CgCDT::Edge e(fh, i);
			CgCDT::Face_handle n = fh->neighbor(i);
			if ( n->info().nest == -1 ) {
				if ( cdt.is_constrained(e) ) border.push_back(e);
				else queue.push_back(n);
			}
		}
	}
}
void cg_mark_domains(CgCDT& cdt) {
	for ( CgCDT::All_faces_iterator f = cdt.all_faces_begin() ; f != cdt.all_faces_end() ; ++f )
		f->info().nest = -1;
	std::list<CgCDT::Edge> border;
	cg_mark_domains(cdt, cdt.infinite_face(), 0, border);
	while ( ! border.empty() ) {
		CgCDT::Edge e = border.front(); border.pop_front();
		CgCDT::Face_handle n = e.first->neighbor(e.second);
		if ( n->info().nest == -1 )
			cg_mark_domains(cdt, n, e.first->info().nest + 1, border);
	}
}
} /* anonymous namespace */

/* ★★ #3535②: revolve のキャップ用 CDT (cgaRevolve.cpp から移した)。
 *   ⚠ extrude 側の cg_mark_domains とは **flood の仕方が違う** ので畳まない
 *   (こちらは境界を跨ぐたびに nest を継ぐ実装)。⇒ 別物として残す。 */
namespace {
typedef K RvK;
struct RvFaceInfo { int nest; bool in_domain() const { return nest >= 0 && (nest % 2) == 1; } };
typedef CGAL::Triangulation_vertex_base_2<RvK>				RvVb;
typedef CGAL::Triangulation_face_base_with_info_2<RvFaceInfo, RvK>		RvFbb;
typedef CGAL::Constrained_triangulation_face_base_2<RvK, RvFbb>		RvFb;
typedef CGAL::Triangulation_data_structure_2<RvVb, RvFb>			RvTDS;
typedef CGAL::Constrained_Delaunay_triangulation_2<RvK, RvTDS, CGAL::Exact_predicates_tag> RvCDT;

void rv_mark(RvCDT& cdt) {
	for ( RvCDT::All_faces_iterator f = cdt.all_faces_begin() ; f != cdt.all_faces_end() ; ++f )
		f->info().nest = -1;
	std::list<RvCDT::Edge> border;
	std::list<RvCDT::Face_handle> q; q.push_back(cdt.infinite_face());
	cdt.infinite_face()->info().nest = 0;
	/* flood: 制約を跨ぐたび nest+1 */
	while ( ! q.empty() || ! border.empty() ) {
		if ( q.empty() ) {
			RvCDT::Edge e = border.front(); border.pop_front();
			RvCDT::Face_handle n = e.first->neighbor(e.second);
			if ( n->info().nest != -1 ) continue;
			n->info().nest = e.first->info().nest + 1;
			q.push_back(n);
		}
		while ( ! q.empty() ) {
			RvCDT::Face_handle fh = q.front(); q.pop_front();
			for ( int i = 0 ; i < 3 ; ++i ) {
				RvCDT::Edge e(fh, i);
				RvCDT::Face_handle n = fh->neighbor(i);
				if ( n->info().nest != -1 ) continue;
				if ( cdt.is_constrained(e) ) border.push_back(e);
				else { n->info().nest = fh->info().nest; q.push_back(n); }
			}
		}
	}
}
} /* anonymous namespace */

/* ★★ #3535②: 断面を world Y まわりに回した立体 (枠が既定のときの経路)。
 *   返り 0 = 作れた / 1 = プロファイルの x (半径) が負 (文言は呼び手が持つ)。
 *   ⚠ 部分角のキャップは CDT で三角形化する ⇒ op 側には置けない
 *     (理由は cgMesh.h の build_box の宣言のところ)。 */
int
cgMesh3D::build_revolve(sPtr<cgMesh2D> in, int nPos, const std::vector<double>& cs,
                        const std::vector<double>& sn, int full, int segs)
{
	typedef Mesh::Vertex_index VI;
	typedef Polygon_2 Poly2;
	Mesh& m = box().m;
	std::vector<Pwh_2>& regs = cg_regions(in);

	for ( std::size_t r = 0 ; r < regs.size() ; ++r ) {
		const Pwh_2& pwh = regs[r];
		std::vector<const Poly2*> plist;
		plist.push_back(&pwh.outer_boundary());
		for ( Pwh_2::Hole_const_iterator hi = pwh.holes_begin() ; hi != pwh.holes_end() ; ++hi )
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
		if ( badRadius )
			return 1;   /* ★ 文言は呼び手 (op) が持つ */
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
			RvCDT cdt;
			std::map<RvCDT::Vertex_handle, int> vh2g;
			for ( std::size_t pi = 0 ; pi < plist.size() ; ++pi ) {
				const Poly2& ring = *plist[pi];
				int start = ringRange[pi].first, cnt = ringRange[pi].second;
				std::vector<RvCDT::Vertex_handle> vh; vh.reserve(cnt);
				int e = 0;
				for ( Poly2::Vertex_const_iterator it = ring.vertices_begin() ; it != ring.vertices_end() ; ++it, ++e ) {
					RvCDT::Vertex_handle h = cdt.insert(*it);
					vh.push_back(h); vh2g[h] = start + e;
				}
				for ( int i = 0 ; i < cnt ; ++i )
					cdt.insert_constraint(vh[i], vh[(i + 1) % cnt]);
			}
			rv_mark(cdt);
			for ( RvCDT::Finite_faces_iterator fc = cdt.finite_faces_begin() ; fc != cdt.finite_faces_end() ; ++fc ) {
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
	return 0;
}

void
cgMesh3D::build_extrude(sPtr<cgMesh2D> in, double h)
{
	typedef Mesh::Vertex_index VI;
	typedef Polygon_2 Poly2;
	Mesh& m = box().m;
	std::vector<Pwh_2>& regs = cg_regions(in);

	for ( std::size_t r = 0 ; r < regs.size() ; ++r ) {
		const Pwh_2& pwh = regs[r];
		if ( pwh.outer_boundary().size() < 3 ) continue;

		/* 全リング(外周 CCW + 穴 CW)を CDT に制約挿入。頂点ハンドルをリングごとに保持。 */
		CgCDT cdt;
		std::vector<std::vector<CgCDT::Vertex_handle> > rings;
		std::vector<const Poly2*> plist;
		plist.push_back(&pwh.outer_boundary());
		for ( Pwh_2::Hole_const_iterator hi = pwh.holes_begin() ; hi != pwh.holes_end() ; ++hi )
			plist.push_back(&(*hi));
		for ( std::size_t pi = 0 ; pi < plist.size() ; ++pi ) {
			const Poly2& ring = *plist[pi];
			int n = (int)ring.size();
			if ( n < 3 ) continue;
			std::vector<CgCDT::Vertex_handle> vh;
			vh.reserve(n);
			for ( Poly2::Vertex_const_iterator it = ring.vertices_begin() ; it != ring.vertices_end() ; ++it )
				vh.push_back(cdt.insert(*it));
			for ( int i = 0 ; i < n ; ++i )
				cdt.insert_constraint(vh[i], vh[(i + 1) % n]);
			rings.push_back(vh);
		}
		cg_mark_domains(cdt);

		/* 各 CDT 頂点に 底(z=0)/天(z=h)の Surface_mesh 頂点を割り当てる。 */
		std::map<CgCDT::Vertex_handle, std::pair<VI,VI> > vmap;
		for ( CgCDT::Finite_vertices_iterator v = cdt.finite_vertices_begin() ; v != cdt.finite_vertices_end() ; ++v ) {
			VI bot = m.add_vertex(K::Point_3(v->point().x(), v->point().y(), K::FT(0.0)));
			VI top = m.add_vertex(K::Point_3(v->point().x(), v->point().y(), K::FT(h)));
			vmap[v] = std::make_pair(bot, top);
		}

		/* キャップ: 材料側(in_domain)三角形を 天(CCW=+z)/ 底(逆順=-z)に。 */
		for ( CgCDT::Finite_faces_iterator f = cdt.finite_faces_begin() ; f != cdt.finite_faces_end() ; ++f ) {
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
			std::vector<CgCDT::Vertex_handle>& ring = rings[ri];
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

/* ★ #3535②: tube を積む Sink (cgaTube.cpp から移した。理由は cgMesh.h の宣言のところ)。 */
namespace {
struct CgTubeSink {
	typedef Mesh::Vertex_index   VI;
	Mesh&            m;
	std::vector<VI>  vi;
	CgTubeSink(Mesh& mm) : m(mm) {}
	int add_vertex(double x, double y, double z) {
		vi.push_back(m.add_vertex(K::Point_3(K::FT(x), K::FT(y), K::FT(z))));
		return (int)vi.size() - 1;
	}
	void add_triangle(int a, int b, int c) {
		std::vector<VI> f;
		f.push_back(vi[(size_t)a]); f.push_back(vi[(size_t)b]); f.push_back(vi[(size_t)c]);
		m.add_face(f);
	}
};
} /* anonymous namespace */

int
cgMesh3D::build_tube(const std::vector<srava_geo::TubeV3>& P,
                     const std::vector<double>& R, int segs)
{
	CgTubeSink sink(box().m);
	int st = srava_geo::make_tube_3d(P, R, segs, sink);
	if ( st != srava_geo::TUBE_OK )
		return st;
	/* 念のため外向き保証: 閉じていて符号付き体積が負なら全反転。 */
	if ( CGAL::is_closed(box().m) ) {
		if ( CGAL::Polygon_mesh_processing::volume(box().m) < K::FT(0) )
			CGAL::Polygon_mesh_processing::reverse_face_orientations(box().m);
	}
	return st;
}

/* ★ #3535②: 三角形スープの取り出し (vcaVoxelize.cpp から移した)。
 *   理由は cgMesh.h の to_soup の宣言のところ。 */
void
cgMesh3D::to_soup(std::vector<double>& xyz, std::vector<int>& tris) const
{
	const Mesh& m = box().m;
	std::vector<int> vidx(m.number_of_vertices() + 1, 0);
	xyz.reserve((size_t)m.number_of_vertices() * 3);
	int next = 0;
	for ( Mesh::Vertex_index v : m.vertices() ) {
		if ( (size_t)v >= vidx.size() ) vidx.resize((size_t)v + 1, 0);
		vidx[(size_t)v] = next++;
		xyz.push_back(CGAL::to_double(m.point(v).x()));
		xyz.push_back(CGAL::to_double(m.point(v).y()));
		xyz.push_back(CGAL::to_double(m.point(v).z()));
	}
	for ( Mesh::Face_index f : m.faces() ) {
		int c[3]; int n = 0;
		for ( Mesh::Vertex_index v : CGAL::vertices_around_face(m.halfedge(f), m) ) {
			if ( n < 3 ) c[n] = vidx[(size_t)v];
			++n;
		}
		if ( n == 3 ) { tris.push_back(c[0]); tris.push_back(c[1]); tris.push_back(c[2]); }
	}
}

int
cgMesh3D::read_file(const char *path)
{
	/* ★ #3535②: CGAL の IO に触るのは **この .so だけ** (理由は cgMesh.h の宣言のところ)。 */
	bool ok = CGAL::Polygon_mesh_processing::IO::read_polygon_mesh(std::string(path), box().m);
	return ( ok && box().m.number_of_vertices() != 0 ) ? 1 : 0;
}

void
cgMesh3D::build_regular_prism(int n, double h, double r)
{
	Mesh& m = box().m;
	CGAL::make_regular_prism((unsigned)n, m, Point_3(0,0,0), h, r, true);
	/* CGAL は高さを Y 軸に作る。extrude/box(Z=高さ)に合わせ、頂点を X 軸 +90°回転
	 * (x,y,z)→(x,-z,y) で **高さを Z 軸**へ。90°は厳密・det=+1 で面の向き不変。
	 * これで prism(n,h,r) ≡ extrude(ngon(n,r),h)(断面 n 角形は XY 平面・z=0..h)。 */
	for ( Mesh::Vertex_index v : m.vertices() ) {
		Point_3 p = m.point(v);
		m.point(v) = Point_3(p.x(), -p.z(), p.y());
	}
	CGAL::Polygon_mesh_processing::triangulate_faces(m);
}

sPtr<stdString>
cgMesh3D::get_str()
{
	return thNEW(stdString,("<cgMesh3D>"));
}

/* ---- codec(cgChunkSink/Source 越しに cgaMeshCodec を駆動)---- */
void
cgMesh3D::encode(cgChunkSink& sink)
{
	cgaMeshCodec::encode(box().m, sink);   /* Sink=cgChunkSink。chunk() は virtual 呼び */
	/* ★★ #3525: **順序つき片リスト**の節 (色の節の後ろ)。
	 *   ⚠ **空なら 1 バイトも書かない** ⇒ 片を持たない値の blob は従来とバイト単位で同じ
	 *     (#3526 の枠の節と同じ作法)。4CC も "MESH" のまま = 他カーネルの reader の対象も変わらない。
	 *   ★ 他カーネル (manifold / geogram / nef) の "MESH" パーサは *必要バイトだけ pull して
	 *     残りは読み飛ばす* 規約 (src/h/common/exact_wire.h 冒頭)。⇒ 末尾に節が増えても食える。 */
	if ( ! partEnd_.empty() ) {
		cgaMeshCodec::put_u32(sink, (uint32_t)partEnd_.size());
		for ( std::size_t i = 0 ; i < partEnd_.size() ; ++i )
			cgaMeshCodec::put_u32(sink, partEnd_[i]);
	}
}
void
cgMesh3D::decode(cgChunkSource& src)
{
	if ( mfm3Input_ ) { decode_mfm3(src); return; }   /* ★ Manifold cache → 無損失昇格(#3404) */
	if ( nef3Input_ ) {                              /* ★ Nef cache → 表現できるなら降格(#3433) */
		if ( ! decode_nef3(src) ) {
			box().m.clear();
			/* ★ 2026-09-06: 「非 2-多様体だから」は **もう理由ではない** (nef が境界を併記する)。
			 *   ここまで来るのは境界表現がそもそも取れない値 = 非有界だけ。 */
			set_decode_err("the Nef value is stored as a bare SNC with no boundary section "
			               "(it has no boundary representation at all — e.g. it is unbounded, "
			               "like the result of complement), which cg-mesh3d cannot represent");
		}
		return;
	}
	cgaMeshCodec::decode(src, box().m);    /* Source=cgChunkSource。pull() は virtual 呼び */
	/* ★ #3525: 片リストの節 (無ければ more()==0 で素通り = 旧 blob との後方互換)。 */
	partEnd_.clear();
	if ( src.more() ) {
		uint32_t n = cgaMeshCodec::get_u32(src);
		partEnd_.reserve(n);
		for ( uint32_t i = 0 ; i < n ; ++i )
			partEnd_.push_back(cgaMeshCodec::get_u32(src));
	}
}

/* ★ MFM3(Manifold)キャッシュを EPECK Surface_mesh へ **無損失昇格** で取り込む(#3404)。
 *   framing(mfMesh::encode と一致・全 little-endian): [u32 nv][u32 nt] + nv×(3×f64 頂点 x,y,z)
 *   + nt×(3×u32 三角形頂点 index)。色/その他 section は無い(mf は最小)。
 *   double → EPECK(K::FT)は厳密(double は 2 進有理数)=損失ゼロ。頂点/面は cgaMeshCodec::decode
 *   と同じ add_vertex/add_face 作法。mf は全 tri 前提なので面頂点数は常に 3。
 *   mfMesh.h には依存しない(framing は安定契約としてここに inline 再現)。 */
void
cgMesh3D::decode_mfm3(cgChunkSource& src)
{
	box().m.clear();
	uint8_t b4[4];
	src.pull(b4, 4);
	uint32_t nv = (uint32_t)b4[0] | ((uint32_t)b4[1]<<8) | ((uint32_t)b4[2]<<16) | ((uint32_t)b4[3]<<24);
	src.pull(b4, 4);
	uint32_t nt = (uint32_t)b4[0] | ((uint32_t)b4[1]<<8) | ((uint32_t)b4[2]<<16) | ((uint32_t)b4[3]<<24);
	std::vector<Mesh::Vertex_index> vmap;
	vmap.reserve(nv);
	for ( uint32_t i = 0 ; i < nv ; ++i ) {
		double xyz[3];
		for ( int k = 0 ; k < 3 ; ++k ) {
			uint8_t b8[8]; src.pull(b8, 8); ::memcpy(&xyz[k], b8, 8);
		}
		vmap.push_back(box().m.add_vertex(K::Point_3(K::FT(xyz[0]), K::FT(xyz[1]), K::FT(xyz[2]))));   /* double→FT 厳密 */
	}
	for ( uint32_t t = 0 ; t < nt ; ++t ) {
		uint32_t idx[3];
		for ( int k = 0 ; k < 3 ; ++k ) {
			src.pull(b4, 4);
			idx[k] = (uint32_t)b4[0] | ((uint32_t)b4[1]<<8) | ((uint32_t)b4[2]<<16) | ((uint32_t)b4[3]<<24);
		}
		/* Manifold は一貫した外向き巻きで watertight manifold を保証 → add_face は成功する。 */
		box().m.add_face(vmap[idx[0]], vmap[idx[1]], vmap[idx[2]]);
	}
}

/* ---- ブーリアン(corefinement)。b が cgMesh3D でなければ null=エラー ----
 * corefinement の前提 = 両入力が「閉じた・自己交差しない多様体」。前段が接触/同一平面で破綻した
 * 不正メッシュ(非閉 or 自己交差)を渡すと CGAL が **segfault** する。これを防ぐ二段の安全策:
 *   ① is_closed(安い)で非閉を弾く。
 *   ② throw_on_self_intersection(true) で自己交差を**例外化**し try/catch で受ける
 *      (これが無いと自己交差入力で落ちる)。失敗は全て null=エラーにして上位で案内する。 */
static bool both_closed(const Mesh& a, const Mesh& b) {
	return CGAL::is_closed(a) && CGAL::is_closed(b);
}
sPtr<cgMesh>
cgMesh3D::op_union(sPtr<cgMesh> b)
{
	sPtr<cgMesh3D> mb = sPtr<cgMesh3D>::d_cast(b);
	if ( ! mb.is_notNull() ) return sPtr<cgMesh>();   /* 異次元/null */
	if ( ! both_closed(box().m, mb->box().m) ) return sPtr<cgMesh>();   /* 非閉=前段で破綻 → クラッシュ回避 */
	sPtr<cgMesh3D> out = thNEW(cgMesh3D,());
	bool ok = false;
	try {
		ok = CGAL::Polygon_mesh_processing::corefine_and_compute_union(
		         box().m, mb->box().m, out->box().m,
		         CGAL::parameters::throw_on_self_intersection(true));
	} catch ( const std::exception& ) { return sPtr<cgMesh>(); }   /* 自己交差等 → 失敗 */
	if ( ! ok ) return sPtr<cgMesh>();   /* 非多様体結果(同一平面重なり等)→ 失敗 */
	return out;
}
sPtr<cgMesh>
cgMesh3D::op_intersection(sPtr<cgMesh> b)
{
	sPtr<cgMesh3D> mb = sPtr<cgMesh3D>::d_cast(b);
	if ( ! mb.is_notNull() ) return sPtr<cgMesh>();
	if ( ! both_closed(box().m, mb->box().m) ) return sPtr<cgMesh>();
	sPtr<cgMesh3D> out = thNEW(cgMesh3D,());
	bool ok = false;
	try {
		ok = CGAL::Polygon_mesh_processing::corefine_and_compute_intersection(
		         box().m, mb->box().m, out->box().m,
		         CGAL::parameters::throw_on_self_intersection(true));
	} catch ( const std::exception& ) { return sPtr<cgMesh>(); }
	if ( ! ok ) return sPtr<cgMesh>();
	return out;
}
sPtr<cgMesh>
cgMesh3D::op_difference(sPtr<cgMesh> b)
{
	sPtr<cgMesh3D> mb = sPtr<cgMesh3D>::d_cast(b);
	if ( ! mb.is_notNull() ) return sPtr<cgMesh>();
	if ( ! both_closed(box().m, mb->box().m) ) return sPtr<cgMesh>();
	sPtr<cgMesh3D> out = thNEW(cgMesh3D,());
	bool ok = false;
	try {
		ok = CGAL::Polygon_mesh_processing::corefine_and_compute_difference(
		         box().m, mb->box().m, out->box().m,
		         CGAL::parameters::throw_on_self_intersection(true));
	} catch ( const std::exception& ) { return sPtr<cgMesh>(); }
	if ( ! ok ) return sPtr<cgMesh>();
	return out;
}
/* ---- combine: 両 Surface_mesh を 1 つに連結(corefinement しない・交差は解かない)。
 * ブール演算前に重なり具合を viewer で確認する用途(`a +++ b`)。閉立体性は保証しない。 ---- */
sPtr<cgMesh>
cgMesh3D::op_combine(sPtr<cgMesh> b)
{
	sPtr<cgMesh3D> mb = sPtr<cgMesh3D>::d_cast(b);
	if ( ! mb.is_notNull() ) return sPtr<cgMesh>();   /* 異次元/null */
	sPtr<cgMesh3D> out = thNEW(cgMesh3D,());
	out->box().m = box().m;                              /* A をコピー(A の f:color も複製される) */
	/* B の面 → out の新面 の対応を取りつつ追記(別連結成分として)。 */
	std::map<Mesh::Face_index, Mesh::Face_index> f2f;
	boost::associative_property_map<std::map<Mesh::Face_index, Mesh::Face_index> > f2f_pm(f2f);
	CGAL::copy_face_graph(mb->box().m, out->box().m, CGAL::parameters::face_to_face_map(f2f_pm));
	/* ★ 色の保持: A か B のどちらかが面色を持つなら out に f:color を用意し各面に正しい色を入れる
	 *   (未着色面は灰 180)。A 面は out->box().m コピーで保持済(A 着色時)/未着色なら default 灰。
	 *   B 面は copy 先の default が A 由来になりうるので f2f で明示上書きする。 */
	std::optional<Mesh::Property_map<Mesh::Face_index, CGAL::IO::Color> > acol
	    = box().m.property_map<Mesh::Face_index, CGAL::IO::Color>("f:color");
	std::optional<Mesh::Property_map<Mesh::Face_index, CGAL::IO::Color> > bcol
	    = mb->box().m.property_map<Mesh::Face_index, CGAL::IO::Color>("f:color");
	if ( acol.has_value() || bcol.has_value() ) {
		CGAL::IO::Color gray((unsigned char)180, (unsigned char)180, (unsigned char)180);
		Mesh::Property_map<Mesh::Face_index, CGAL::IO::Color> oc
		    = out->box().m.add_property_map<Mesh::Face_index, CGAL::IO::Color>("f:color", gray).first;
		for ( std::map<Mesh::Face_index, Mesh::Face_index>::iterator it = f2f.begin() ; it != f2f.end() ; ++it )
			oc[it->second] = bcol.has_value() ? (*bcol)[it->first] : gray;   /* B 面を明示着色 */
	}
	return out;
}

/* ---- 着色: 全面に f:color(r,g,b: 0-255)を付けた新 mesh。combine で各成分の色が残る。---- */
sPtr<cgMesh>
cgMesh3D::op_color(int r, int g, int b)
{
	if ( r < 0 ) r = 0;  if ( r > 255 ) r = 255;
	if ( g < 0 ) g = 0;  if ( g > 255 ) g = 255;
	if ( b < 0 ) b = 0;  if ( b > 255 ) b = 255;
	sPtr<cgMesh3D> out = thNEW(cgMesh3D,());
	out->box().m = box().m;   /* deep copy */
	CGAL::IO::Color c((unsigned char)r, (unsigned char)g, (unsigned char)b);
	Mesh::Property_map<Mesh::Face_index, CGAL::IO::Color> fc
	    = out->box().m.add_property_map<Mesh::Face_index, CGAL::IO::Color>("f:color", c).first;
	for ( Mesh::Face_index f : out->box().m.faces() )
		fc[f] = c;   /* 既存プロパティ再利用時も確実に上書き */
	return out;
}

/* ---- アフィン変換(行優先 double[12] = 3x4)。反射(det<0)は面の向きを反転 ---- */
sPtr<cgMesh>
cgMesh3D::apply_affine(const double e[12])
{
	/* double[12] → Aff_transformation_3<EPECK>。NB: K::FT(e[i]) を ctor に直接並べると most vexing
	 * parse(関数宣言化)→ FT 配列の添字式で渡す。 */
	K::FT f[12];
	for ( int i = 0 ; i < 12 ; ++i )
		f[i] = K::FT(e[i]);
	CGAL::Aff_transformation_3<K> aff(
	    f[0], f[1], f[2],  f[3],
	    f[4], f[5], f[6],  f[7],
	    f[8], f[9], f[10], f[11] );

	sPtr<cgMesh3D> out = thNEW(cgMesh3D,());
	Mesh& m = out->box().m;
	m = box().m;   /* deep copy */
	for ( Mesh::Vertex_index v : m.vertices() )
		m.point(v) = aff.transform(m.point(v));

	/* 3x3 線形部の行列式 < 0(反射)なら面の向きを反転して outward 法線を保つ。 */
	K::FT det =
	    aff.m(0,0) * ( aff.m(1,1)*aff.m(2,2) - aff.m(1,2)*aff.m(2,1) )
	  - aff.m(0,1) * ( aff.m(1,0)*aff.m(2,2) - aff.m(1,2)*aff.m(2,0) )
	  + aff.m(0,2) * ( aff.m(1,0)*aff.m(2,1) - aff.m(1,1)*aff.m(2,0) );
	if ( det < K::FT(0) )
		CGAL::Polygon_mesh_processing::reverse_face_orientations(m);
	/* ★ #3525: **アフィン変換だけは片リストを引き継ぐ**。面も面の順序も動かないため
	 *   (形を変える op — ブール・repair 等 — は引き継がない。片はもうセルではない)。 */
	out->partEnd_ = partEnd_;
	return out;
}

/* ★ offset 専用だった近似球生成器 cga_make_icosphere は nef へ移設した (#3440 の 2)。
 *   sphere/icosphere op が使う測地球は下の cga_make_geodesic (common/geodesic.h) で別物。 */

/* ★ #3433/#3440: nef の cache (4CC "NEFB") を cg へ**降格**して読む。
 *   framing: [u8 形式][…] (nfMesh.h の NF_FORM_*・安定契約としてここに inline 再現)。
 *   ★**cgal.so は SNC をパースしない**。よって読めるのは
 *     形式 1 = 厳密境界 (cg の "MESH" と同一フレーミング) **だけ**で、
 *     形式 0 = SNC は **読まずに false** を返す (呼び側が明示エラーにする)。
 *   ⚠⚠ #3559 で前提が **変わった**: 理由は長らく「cgal.so は CGAL Nef に依存しない (#3440)」
 *     = *依存できないから読めない* だったが、#3440 の線は畳まれ、libsrava_cg は Nef を
 *     含むようになった (上流が corefinement と Nef を分けられないため・ひさ裁定)。
 *     ⇒ 今は **依存はできるが reader を足していない** = *設計の選択*。SNC を読ませるなら
 *       ここに足せる (橋 nef_cg.so を通さずに)。#3559 ではやっていない — cache 形式と
 *       routing を 1 つも動かさないため。動かすなら別チケットで。
 *   nef_hybrid が SNC で書くのは「cg では**表現できない**値 (非有界・非多様体)」だけなので、
 *   降格が要る値は全て境界形式で届く (空洞つき立体も境界形式で運ばれる = 降格できる)。
 *   nef_snc は常に SNC なので cg へ降格できない (設計どおりの代償)。 */
bool
cgMesh3D::decode_nef3(cgChunkSource& src)
{
	uint8_t form = 0;
	src.pull(&form, 1);
	/* ★ #3478: NF_FORM_SNC_BND (=2) は [SNC のブロック列][厳密境界]。SNC は読まない
	 *   (上の ⚠⚠ を見よ — #3559 以降は「依存できない」ではなく「reader を持たない」)
	 *   が、終端まで読み捨てれば後半は form 1 と同一フレーミング。 */
	if ( form == 2 ) {
		/* ★ #3507: SNC は [u32 blocklen][block]…[u32 0] のブロック列になった。
		 *   前置された全長で読み飛ばすのではなく、**終端まで読み捨てる**。 */
		blockframe::ibuf<cgChunkSource> ib(src);
		if ( ! ib.drain() )
			return false;
		cgaMeshCodec::decode(src, box().m);
		return true;
	}
	if ( form != 1 )        /* NF_FORM_BOUNDARY 以外 (= 境界の付かない SNC) は読めない */
		return false;
	cgaMeshCodec::decode(src, box().m);
	return true;
}

/* ---- 測地球 (manifold と共通アルゴリズム = src/h/common/geodesic.h)。sphere/icosphere の実体。
 *      種 (八面体/二十面体) を n 分割して球面投影する。offset 専用の Loop 細分の球 (旧 cga_make_icosphere)
 *      とは別物: こちらは線形分割なので manifold と頂点・面が一致し体積が数値誤差で揃う。 ---- */
namespace {
struct CgGeoSink {
	Mesh&                           m;
	std::vector<Mesh::Vertex_index> vs;
	CgGeoSink(Mesh& mm) : m(mm) {}
	int  add_vertex(double x, double y, double z) {
		vs.push_back(m.add_vertex(K::Point_3(x, y, z)));
		return (int)vs.size() - 1;
	}
	void add_triangle(int a, int b, int c) { m.add_face(vs[a], vs[b], vs[c]); }
};
}  /* namespace */

/* seed: srava_geo::SEED_OCTAHEDRON(sphere) / SEED_ICOSAHEDRON(icosphere)。n=種 1 辺の分割数。 */
void cga_make_geodesic(Mesh& ball, int seed, int n, double r)
{
	ball.clear();
	CgGeoSink sink(ball);
	srava_geo::make_geodesic(seed, n, r, sink);
}

/* ---- 3D オフセット: **nef モジュールへ移設した** (#3440 の 2) ----
 * 中身は Minkowski 和 (Nef + 凸分解) で、他の幾何カーネル (Nef) の機能を借りて cgal の顔で
 * 出していた = モジュール境界の約束①違反だった (docs/srava_module_reference.md「モジュールの境界」)。
 * ★sig からも 3D は外してあるので、通常 routing でここへ来ることは無い (来たら明示エラー)。
 *   2D offset (straight skeleton) は cgMesh2D 側に健在。 */
sPtr<cgMesh>
cgMesh3D::op_offset(double, int)
{
	return thNULL;   /* 呼び側 (cgaOffset) がエラーにする */
}

/* ---- 計測: 頂点数 / 面数 (#3443) ----
 * ★ planner が cache の先頭バイトを "MESH" 前提で読んで表示していたのを op へ移した。
 *   形式ごとの詰め方を知っているのは **そのモジュール** なので、ここが正しい置き場所。 */
int cgMesh3D::op_nverts() { return (int)box().m.number_of_vertices(); }
int cgMesh3D::op_nfaces() { return (int)box().m.number_of_faces(); }

/* ---- ★ #3527: i 番目の頂点の座標 ----
 * ★ op_nverts と **同じ列を同じ順**で見る (どちらも m.vertices() の列挙)。
 * ⚠ 番号は Vertex_index の生の値ではなく **列挙の順番**。削除された頂点があると
 *   生の番号には穴が空くが、op_nverts は有効な頂点しか数えないので、**列挙で揃える**。
 * ⚠⚠ @to_double@ は Lazy_exact_nt の *区間近似* を返し正しく丸められない ⇒ @exact()@ を通す。
 *   ★ ここは「入力座標をビット単位で引き戻す」ための op なので、**op_bbox より厳しく取る**
 *     (op_bbox は範囲を出すだけなので素の to_double でよい)。 */
int
cgMesh3D::op_vert(int i, double out[3])
{
	if ( i < 0 ) return 0;
	int k = 0;
	for ( Mesh::Vertex_index v : box().m.vertices() ) {
		if ( k++ != i ) continue;
		out[0] = CGAL::to_double(CGAL::exact(box().m.point(v).x()));
		out[1] = CGAL::to_double(CGAL::exact(box().m.point(v).y()));
		out[2] = CGAL::to_double(CGAL::exact(box().m.point(v).z()));
		return 3;
	}
	return 0;
}

/* ---- ★ #3527: 全頂点を平坦な配列へ ----
 * ⚠⚠ **op_vert と同じ列を同じ順**で積むこと。片方だけ順序を変えると
 *   `verts(m)[i] == vert(m,i)` が黙って崩れる。⇒ 検査がその等式を見ている。
 * ★ 平坦な double 配列は ptCloud の内部表現そのものなので、そのまま渡せる。 */
int
cgMesh3D::op_verts(std::vector<double> &out)
{
	out.clear();
	out.reserve((size_t)box().m.number_of_vertices() * 3);
	for ( Mesh::Vertex_index v : box().m.vertices() ) {
		out.push_back(CGAL::to_double(CGAL::exact(box().m.point(v).x())));
		out.push_back(CGAL::to_double(CGAL::exact(box().m.point(v).y())));
		out.push_back(CGAL::to_double(CGAL::exact(box().m.point(v).z())));
	}
	return 3;
}

/* ---- 計測: 表面積(全三角形面積の和。√を含むので double で返す)---- */
double
cgMesh3D::op_area()
{
	return CGAL::to_double(CGAL::Polygon_mesh_processing::area(box().m));
}

/* ---- 計測: 体積(閉メッシュ。発散定理。√を含まないが有理→double)---- */
double
cgMesh3D::op_volume()
{
	/* ⚠ @to_double@ は Lazy_exact_nt の *区間近似* を返し正しく丸められない ⇒ exact() を通す
	 *   (#3525・2D の op_area と同じ)。
	 * ⚠⚠ @PMP::volume@ は **三角形メッシュ前提**。四角面のまま渡すと *黙って半分*を返す
	 *   (2026-09-15 実測: 形も bbox も valid も正しいのに体積だけ 0.5 だった)。srava の 3D は
	 *   三角形で揃えてあるので通常は当たらないが、新しく面を作る側が三角形化を忘れると踏む。 */
	return CGAL::to_double(CGAL::exact(CGAL::Polygon_mesh_processing::volume(box().m)));
}

/* ---- 3D に「周長」は未定義(呼び元 cgaPerimeter が dim==3 をエラーにする)---- */
double
cgMesh3D::op_perimeter()
{
	return 0.0;
}

/* ---- 計測: 体積重心(各三角形と原点で四面体に分解し、符号付き体積で加重平均)---- */
int
cgMesh3D::op_centroid(double out[3])
{
	/* ★★ #3525: **厳密なまま積んで、落とすのは最後に 1 回だけ**。体積重心は有理数なので
	 *   厳密に出せる (表面積と違って √ が要らない)。⚠ 以前は各項を double にして /6 /4 して
	 *   から足しており、**同じ立体でも面の並び順で答えが変わって**いた
	 *   (2026-09-15 実測: 単位立方体の重心が [0.50000000000000011, …, 0.5] と軸ごとに割れた)。
	 *   2D の op_centroid / op_area とまったく同じ直し方 — わけは cgMesh2D.cpp の ring_moment。
	 *   ⚠ @CGAL::to_double@ だけでは足りない (Lazy_exact_nt の区間近似なので正しく丸められない)
	 *     ⇒ @CGAL::exact()@ を通す。
	 *   v6 = 6·(符号つき体積) ・ c24 = 24·(体積)·(重心) で持つ (割らずに溜める)。 */
	K::FT v6 = 0, cx24 = 0, cy24 = 0, cz24 = 0;
	for ( Mesh::Face_index f : box().m.faces() ) {
		Mesh::Halfedge_index h = box().m.halfedge(f);
		const Point_3& A = box().m.point(box().m.source(h));
		const Point_3& B = box().m.point(box().m.target(h));
		const Point_3& C = box().m.point(box().m.target(box().m.next(h)));
		/* 四面体(原点,A,B,C)の符号付き体積の 6 倍 = A·(B×C) */
		const K::FT v = A.x()*(B.y()*C.z() - B.z()*C.y())
		              - A.y()*(B.x()*C.z() - B.z()*C.x())
		              + A.z()*(B.x()*C.y() - B.y()*C.x());
		v6   += v;
		cx24 += v * (A.x() + B.x() + C.x());   /* 四面体重心 = (0+A+B+C)/4 */
		cy24 += v * (A.y() + B.y() + C.y());
		cz24 += v * (A.z() + B.z() + C.z());
	}
	/* c = (c24/24) / (v6/6) = c24 / (4·v6) */
	if ( v6 != 0 ) {
		out[0] = CGAL::to_double(CGAL::exact(cx24 / (4 * v6)));
		out[1] = CGAL::to_double(CGAL::exact(cy24 / (4 * v6)));
		out[2] = CGAL::to_double(CGAL::exact(cz24 / (4 * v6)));
	} else {
		out[0] = out[1] = out[2] = 0.0;
	}
	return 3;
}

/* ---- 計測: 軸平行バウンディングボックス(全頂点走査で min/max)。空メッシュは全 0。---- */
int
cgMesh3D::op_bbox(double mn[3], double mx[3])
{
	bool first = true;
	for ( Mesh::Vertex_index v : box().m.vertices() ) {
		double x = CGAL::to_double(box().m.point(v).x());
		double y = CGAL::to_double(box().m.point(v).y());
		double z = CGAL::to_double(box().m.point(v).z());
		if ( first ) {
			mn[0] = mx[0] = x; mn[1] = mx[1] = y; mn[2] = mx[2] = z;
			first = false;
		} else {
			if ( x < mn[0] ) mn[0] = x;  if ( x > mx[0] ) mx[0] = x;
			if ( y < mn[1] ) mn[1] = y;  if ( y > mx[1] ) mx[1] = y;
			if ( z < mn[2] ) mn[2] = z;  if ( z > mx[2] ) mx[2] = z;
		}
	}
	if ( first ) { mn[0]=mn[1]=mn[2]=mx[0]=mx[1]=mx[2] = 0.0; }
	return 3;
}

/* ---- 近接(3D-3D)---- */
namespace {
	typedef CGAL::AABB_face_graph_triangle_primitive<Mesh>	CgPrim;
	typedef CGAL::AABB_traits_3<K, CgPrim>		CgAabbTraits;
	typedef CGAL::AABB_tree<CgAabbTraits>				CgTree;

	inline void pt_to(const Point_3& p, double out[3]) {
		out[0] = CGAL::to_double(p.x());
		out[1] = CGAL::to_double(p.y());
		out[2] = CGAL::to_double(p.z());
	}
	inline double dist2(const Point_3& a, const Point_3& b) {
		double dx = CGAL::to_double(a.x()-b.x());
		double dy = CGAL::to_double(a.y()-b.y());
		double dz = CGAL::to_double(a.z()-b.z());
		return dx*dx + dy*dy + dz*dz;
	}
}

/* 最遠=頂点ペア総当り(極値は頂点で達成され厳密。O(|VA|·|VB|))。
 * 最近接=AABB で「A 頂点→B 面」「B 頂点→A 面」両方向の最小(頂点-面の近似。辺-辺の谷は取りこぼす)。 */
double
cgMesh3D::op_proximity(sPtr<cgMesh3D> b, bool farthest, double pa[3], double pb[3])
{
	if ( farthest ) {
		double best = -1.0;
		for ( Mesh::Vertex_index va : box().m.vertices() )
		for ( Mesh::Vertex_index vb : b->box().m.vertices() ) {
			double d2 = dist2(box().m.point(va), b->box().m.point(vb));
			if ( d2 > best ) { best = d2; pt_to(box().m.point(va), pa); pt_to(b->box().m.point(vb), pb); }
		}
		return ( best < 0 ) ? 0.0 : std::sqrt(best);
	}
	double best = -1.0;
	{   /* A の各頂点 → B 表面 */
		CgTree tree(faces(b->box().m).first, faces(b->box().m).second, b->box().m);
		for ( Mesh::Vertex_index va : box().m.vertices() ) {
			Point_3 q = box().m.point(va);
			Point_3 cp = tree.closest_point(q);
			double d2 = dist2(q, cp);
			if ( best < 0 || d2 < best ) { best = d2; pt_to(q, pa); pt_to(cp, pb); }
		}
	}
	{   /* B の各頂点 → A 表面(辺-辺谷の近似改善・対称化) */
		CgTree tree(faces(box().m).first, faces(box().m).second, box().m);
		for ( Mesh::Vertex_index vb : b->box().m.vertices() ) {
			Point_3 q = b->box().m.point(vb);
			Point_3 cp = tree.closest_point(q);
			double d2 = dist2(q, cp);
			if ( best < 0 || d2 < best ) { best = d2; pt_to(cp, pa); pt_to(q, pb); }
		}
	}
	return ( best < 0 ) ? 0.0 : std::sqrt(best);
}

/* ---- 点との距離 (#3514) ----
 * ★ 面を三角形として AABB に載せ、closest_point までの距離。**三角形の内部も含めて**測るので
 *   頂点総当りの近似ではなく、多面体の境界に対する真の最短距離。
 * ⚠ 符号は付けない (内側でも正)。球 (半径 r) の中心から距離 d の点なら |d - r| になる。
 *   ⇒ 「内か外か」が要るなら別の問い (openvdb の距離場は符号を持つが、op の約束は揃えてある)。
 * ⚠ EPECK のまま closest_point を取ると有理数が膨れるので、距離の値は double で返す
 *   (√ を含む時点でどのみち厳密ではない)。 */
int
cgMesh3D::op_distance_at(const double p[3], double *out)
{
	if ( box().m.number_of_faces() == 0 ) return 0;
	CgTree tree(faces(box().m).first, faces(box().m).second, box().m);
	Point_3 q(p[0], p[1], p[2]);
	Point_3 cp = tree.closest_point(q);
	if ( out ) *out = std::sqrt(dist2(q, cp));
	return 1;
}

/* ---- 肉厚解析(SDF=Shape Diameter Function・自前並列実装)----
 * 各面で内向きに錐状(2/3π)のレイを rays 本飛ばし、反対側の壁までの距離(ロバスト加重平均)を
 * その面の「肉厚」とする。薄い壁ほど小さい → 3Dプリント時の割れやすい箇所が拾える。
 * レイ投射は EPECK だと非現実的に重いので、頂点を double 化した EPICK メッシュ上で行う(測定値=近似で十分)。
 *
 * 【並列化】面ごとの計算は完全独立で、共有する AABB ツリーは構築後 const クエリのみ → ロック不要。
 *   AABB ツリーを 1 本だけ先に build() し、面リストを区間分割して std::thread に配る。各スレッドは
 *   自分の担当面のスロット thk[i] にだけ書く(排他)→ 同期不要。CGAL::sdf_values は内部シングルスレッド
 *   かつ 1 面ぶきの入口を出さないので、1 面ぶんの SDF(sdf_one_face)を自前で持つ。
 *
 * 【sdf_one_face】① 面の重心 + 内向き法線。② 法線まわり半角 cone/2 の円錐内へ rays 方向を黄金角スパイラル
 *   で決定的にサンプル(乱数なし=再現可)。③ 各レイを共有ツリーへ撃ち first_intersection。自己面を eps だけ
 *   内側にずらした始点で除外し、ヒット面の法線とレイが同方向(反対側の壁=貫通)のものだけ採用。
 *   ④ 中央値の 1.5 倍超(grazing で遠くへ抜けた外れ値)を捨て、cos(法線との角)で加重平均。 */
namespace {
	typedef CGAL::Exact_predicates_inexact_constructions_kernel	SdfK;
	typedef CGAL::Surface_mesh<SdfK::Point_3>			SdfMesh;
	typedef boost::graph_traits<SdfMesh>::face_descriptor		SdfFace;
	typedef CGAL::AABB_face_graph_triangle_primitive<SdfMesh>	SdfPrim;
	typedef CGAL::AABB_traits_3<SdfK, SdfPrim>			SdfAabbTraits;
	typedef CGAL::AABB_tree<SdfAabbTraits>				SdfTree;
	typedef SdfK::Point_3	SdfPt;
	typedef SdfK::Vector_3	SdfVec;
	typedef SdfK::Ray_3	SdfRay;

	/* 三角形面の外向き法線(頂点周回の向き=outward)を返す。退化面は length 0。 */
	inline SdfVec face_normal(const SdfMesh& im, SdfFace f) {
		SdfMesh::Halfedge_index h = im.halfedge(f);
		const SdfPt& A = im.point(im.source(h));
		const SdfPt& B = im.point(im.target(h));
		const SdfPt& C = im.point(im.target(im.next(h)));
		return CGAL::cross_product(B - A, C - A);
	}
	inline SdfPt face_centroid(const SdfMesh& im, SdfFace f) {
		SdfMesh::Halfedge_index h = im.halfedge(f);
		const SdfPt& A = im.point(im.source(h));
		const SdfPt& B = im.point(im.target(h));
		const SdfPt& C = im.point(im.target(im.next(h)));
		return SdfPt((A.x()+B.x()+C.x())/3.0, (A.y()+B.y()+C.y())/3.0, (A.z()+B.z()+C.z())/3.0);
	}

	/* 1 面ぶんの SDF(肉厚)。ヒット皆無は -1。 */
	double sdf_one_face(const SdfMesh& im, SdfFace f, const SdfTree& tree,
	                    double cone, int rays, double eps)
	{
		SdfVec nout = face_normal(im, f);
		double nl = std::sqrt(nout.squared_length());
		if ( nl < 1e-20 ) return -1.0;                 /* 退化三角形 */
		nout = nout / nl;
		SdfVec nin = -nout;                            /* 内向き */
		/* 法線に直交する基底 (u, v) */
		SdfVec u = CGAL::cross_product(nin, SdfVec(1, 0, 0));
		if ( u.squared_length() < 1e-12 ) u = CGAL::cross_product(nin, SdfVec(0, 1, 0));
		u = u / std::sqrt(u.squared_length());
		SdfVec v = CGAL::cross_product(nin, u);        /* nin,u が単位直交 → v も単位 */

		SdfPt   cen = face_centroid(im, f);
		SdfPt   org = cen + nin * eps;                 /* 自己面を踏まないよう少し内側へ */
		double  rmax = std::tan(cone * 0.5);
		const double GA = 2.39996322972865332;         /* 黄金角(ラジアン) */

		std::vector<double> ds; ds.reserve(rays);
		std::vector<double> ws; ws.reserve(rays);
		for ( int k = 0 ; k < rays ; ++k ) {
			double t   = (k + 0.5) / rays;
			double rr  = rmax * std::sqrt(t);          /* 面積均等な半径 */
			double phi = k * GA;
			SdfVec dir = nin + u * (rr * std::cos(phi)) + v * (rr * std::sin(phi));
			dir = dir / std::sqrt(dir.squared_length());
			auto hit = tree.first_intersection(SdfRay(org, dir));
			if ( ! hit ) continue;
			const SdfPt* p = std::get_if<SdfPt>(&(hit->first));
			if ( ! p ) continue;                       /* 線分交差(同一平面)は捨てる */
			SdfFace hf = hit->second;
			if ( hf == f ) continue;
			SdfVec hn = face_normal(im, hf);
			if ( CGAL::scalar_product(dir, hn) <= 0 ) continue;  /* 反対側の壁(貫通方向)だけ採用 */
			double d = std::sqrt(CGAL::squared_distance(cen, *p));
			double w = CGAL::scalar_product(dir, nin); /* 法線に近いレイほど重い */
			if ( w < 0 ) w = 0;
			ds.push_back(d); ws.push_back(w);
		}
		if ( ds.empty() ) return -1.0;
		std::vector<double> srt = ds;
		std::sort(srt.begin(), srt.end());
		double med = srt[srt.size() / 2];
		double sw = 0, swd = 0;
		for ( size_t i = 0 ; i < ds.size() ; ++i ) {
			if ( ds[i] > 1.5 * med ) continue;         /* grazing の外れ値を捨てる */
			double w = ws[i] + 1e-6;
			sw += w; swd += w * ds[i];
		}
		return ( sw > 0 ) ? (swd / sw) : med;
	}
}
double
cgMesh3D::op_thin_spots(double t_min, int rays, double cone_deg, std::vector<double>& out)
{
	if ( rays < 1 ) rays = 1;
	if ( cone_deg < 1.0 ) cone_deg = 1.0;  if ( cone_deg > 179.0 ) cone_deg = 179.0;
	/* EPECK(box().m) → EPICK(im) へ頂点/面をコピー。Surface_mesh は三角形化済み前提。 */
	SdfMesh im;
	std::map<Mesh::Vertex_index, SdfMesh::Vertex_index> vmap;
	for ( Mesh::Vertex_index v : box().m.vertices() ) {
		const Point_3& p = box().m.point(v);
		vmap[v] = im.add_vertex(SdfPt(
		    CGAL::to_double(p.x()), CGAL::to_double(p.y()), CGAL::to_double(p.z())));
	}
	for ( Mesh::Face_index f : box().m.faces() ) {   /* 三角形化済み前提(全プリミティブが triangulate される) */
		Mesh::Halfedge_index h = box().m.halfedge(f);
		im.add_face(vmap[box().m.source(h)], vmap[box().m.target(h)], vmap[box().m.target(box().m.next(h))]);
	}
	if ( im.number_of_faces() == 0 ) return 0.0;

	/* AABB ツリーを 1 本だけ先に構築(以後 const クエリのみ → スレッド間共有安全)。 */
	SdfTree tree(faces(im).first, faces(im).second, im);
	tree.build();
	CGAL::Bbox_3 bb = tree.bbox();
	double diag = std::sqrt( (bb.xmax()-bb.xmin())*(bb.xmax()-bb.xmin())
	                       + (bb.ymax()-bb.ymin())*(bb.ymax()-bb.ymin())
	                       + (bb.zmax()-bb.zmin())*(bb.zmax()-bb.zmin()) );
	double eps  = ( diag > 0 ) ? diag * 1e-6 : 1e-9;
	double cone = cone_deg * (3.14159265358979323846 / 180.0);   /* 度→ラジアン。既定 45°=ほぼ垂直方向の肉厚 */

	std::vector<SdfFace> F(faces(im).begin(), faces(im).end());
	std::vector<double>  thk(F.size(), -1.0);          /* 面ごとの出力スロット(排他=ロック不要) */

	/* [lo,hi) の面を担当して自分のスロットへ書くワーカ。 */
	struct Job {
		const SdfMesh* im; const SdfTree* tree; const std::vector<SdfFace>* F;
		std::vector<double>* thk; double cone; int rays; double eps;
		void run(size_t lo, size_t hi) const {
			/* 退化三角形等で CGAL が例外を投げてもスレッドを巻き込まない(未捕捉=terminate 回避)。
			 * その面は -1(計測不能)として扱う。 */
			for ( size_t i = lo ; i < hi ; ++i ) {
				try { (*thk)[i] = sdf_one_face(*im, (*F)[i], *tree, cone, rays, eps); }
				catch ( ... ) { (*thk)[i] = -1.0; }
			}
		}
	};
	Job job; job.im = &im; job.tree = &tree; job.F = &F; job.thk = &thk;
	job.cone = cone; job.rays = rays; job.eps = eps;

	unsigned G = std::thread::hardware_concurrency();
	if ( G < 1 ) G = 4;
	if ( (size_t)G > F.size() ) G = (unsigned)(F.size() ? F.size() : 1);
	size_t chunk = (F.size() + G - 1) / G;
	std::vector<std::thread> ths;
	for ( unsigned g = 0 ; g < G ; ++g ) {
		size_t lo = (size_t)g * chunk;
		size_t hi = std::min(F.size(), lo + chunk);
		if ( lo < hi ) ths.push_back(std::thread(&Job::run, &job, lo, hi));
	}
	for ( size_t i = 0 ; i < ths.size() ; ++i ) ths[i].join();

	/* しきい値以下を収集(面の重心 + 厚み)。 */
	double gmin = -1.0;
	for ( size_t i = 0 ; i < F.size() ; ++i ) {
		double thkv = thk[i];
		if ( ! (thkv > 1e-9) ) continue;               /* レイ未命中・退化面は除外 */
		if ( gmin < 0.0 || thkv < gmin ) gmin = thkv;
		if ( thkv < t_min ) {
			SdfPt c = face_centroid(im, F[i]);
			out.push_back(c.x()); out.push_back(c.y()); out.push_back(c.z());
			out.push_back(thkv);
		}
	}
	return ( gmin < 0.0 ) ? 0.0 : gmin;
}

/* ---- 検査: 閉じている ∧ 自己交差していない(Surface_mesh は構造上多様体)---- */
int
cgMesh3D::op_valid()
{
	namespace PMP = CGAL::Polygon_mesh_processing;
	/* ★ #3487: 共通定義は ① 空でない ∧ ② 閉じている ∧ ③ 自己交差が無い。
	 *   ① は 2026-09-05 まで抜けており、**空メッシュに 1 を返していた** (manifold は 0 で、
	 *   同じ op が 2 本で違う答えを出していた)。① を入れる根拠は valid の使い道
	 *   (「空集合はエラー → valid でガード」= bbox / centroid の前段) — 空では
	 *   その 2 つが定義できないので、ガードとして 1 を返しては役に立たない。
	 *   定義の全文と経緯は src/h/common/meshprops.h の冒頭。 */
	if ( box().m.number_of_faces() == 0 ) return 0;
	bool ok = CGAL::is_closed(box().m) && ! PMP::does_self_intersect(box().m);
	return ok ? 1 : 0;
}

/* ---- 位相: シェル数 / 塊数 / 総種数 (#3514) ----
 * ★ 数えるものの定義は common/meshprops.h の topology() に書いてある (シェル = 面の連結成分 /
 *   塊 = 立体の連結成分 / 種数 = 取っ手の総数)。**定義は 1 つ・答え方はカーネルごと**
 *   (valid と同じ方針) なので、cgal はここで CGAL の語彙で答える。
 *
 * ★ 塊 = **符号つき体積が正のシェル**。向きの揃った閉じた 2-多様体では外殻の積分が正・
 *   空洞の境界は負になる。⇒ 中空の箱はシェル 2 枚で塊 1 個・xor の N 球はシェル N 枚で塊 N/2 個。
 *   ⚠ 積分は double で行う (@CGAL::to_double@)。**要るのは符号だけ**で、シェルは必ず有限の
 *     体積を囲むから桁落ちで符号が反転することはない。厳密有理数で積むと xor の掃引規模
 *     (10^6 面) では分母が膨れて実用にならない。
 * ★ Euler 標数は **面が三角形でなくても**そのまま V-E+F でよい (多角形複体の不変量)。
 *   体積の方だけ扇状に三角形へ割って積む。 */
int
cgMesh3D::op_topology(int *nshells, int *nparts, int *genus)
{
	namespace PMP = CGAL::Polygon_mesh_processing;
	if ( nshells ) *nshells = 0;
	if ( nparts  ) *nparts  = 0;
	if ( genus   ) *genus   = 0;
	if ( box().m.number_of_faces() == 0 ) return 0;

	typedef Mesh::Face_index   FI;
	typedef Mesh::Vertex_index VI;
	Mesh::Property_map<FI,std::size_t> fcc =
	    box().m.add_property_map<FI,std::size_t>("f:cgtopo", 0).first;
	const std::size_t nc = PMP::connected_components(box().m, fcc);
	if ( nc == 0 ) { box().m.remove_property_map(fcc); return 0; }

	std::vector<int>    nv((size_t)nc, 0), ne((size_t)nc, 0), nf((size_t)nc, 0);
	std::vector<double> vol((size_t)nc, 0.0);

	for ( FI f : box().m.faces() ) {
		const std::size_t c = fcc[f];
		++nf[c];
		/* 扇状に割って発散定理で積む (面が三角形ならそのまま 1 枚)。 */
		Mesh::Halfedge_index h0 = box().m.halfedge(f), h = box().m.next(h0);
		const Point_3& a = box().m.point(box().m.source(h0));
		while ( box().m.next(h) != h0 ) {
			const Point_3& b = box().m.point(box().m.source(h));
			const Point_3& c3 = box().m.point(box().m.target(h));
			const double ax = CGAL::to_double(a.x()), ay = CGAL::to_double(a.y()), az = CGAL::to_double(a.z());
			const double bx = CGAL::to_double(b.x()) - ax, by = CGAL::to_double(b.y()) - ay, bz = CGAL::to_double(b.z()) - az;
			const double cx = CGAL::to_double(c3.x()) - ax, cy = CGAL::to_double(c3.y()) - ay, cz = CGAL::to_double(c3.z()) - az;
			vol[c] += ( ax*(by*cz - bz*cy) + ay*(bz*cx - bx*cz) + az*(bx*cy - by*cx) ) / 6.0;
			h = box().m.next(h);
		}
	}
	for ( Mesh::Edge_index e : box().m.edges() ) {
		Mesh::Halfedge_index h = box().m.halfedge(e);
		FI f = box().m.face(h);
		if ( f == box().m.null_face() ) f = box().m.face(box().m.opposite(h));
		if ( f != box().m.null_face() ) ++ne[fcc[f]];
	}
	for ( VI v : box().m.vertices() ) {
		Mesh::Halfedge_index h = box().m.halfedge(v);
		if ( h == box().m.null_halfedge() ) continue;   /* 孤立点は数えない */
		FI f = box().m.face(h);
		if ( f == box().m.null_face() ) f = box().m.face(box().m.opposite(h));
		if ( f != box().m.null_face() ) ++nv[fcc[f]];
	}
	box().m.remove_property_map(fcc);

	int sh = 0, pa = 0, ge = 0;
	for ( std::size_t c = 0 ; c < nc ; ++c ) {
		if ( nf[c] == 0 ) continue;
		++sh;
		if ( vol[c] > 0.0 ) ++pa;
		ge += 1 - ( nv[c] - ne[c] + nf[c] ) / 2;
	}
	const int closed = CGAL::is_closed(box().m) ? 1 : 0;
	if ( nshells ) *nshells = sh;
	/* ★★ #3525: **順序つき片リストを持っている値は、その数が塊の数**。
	 *   約束は「その値が構造として持っている片の数」なので、値が明示的に片を持っているなら
	 *   そちらが答え (数え直さない)。⇒ セルが互いに接していても N のまま。
	 *   ⚠ 持っていない値は従来どおり **符号つき体積が正のシェル**の数 (1 ビットも動かない)。 */
	if ( nparts  ) *nparts  = has_parts() ? (int)partEnd_.size() : pa;
	if ( genus   ) *genus   = closed ? ge : 0;   /* 閉じていなければ意味を持たない */
	return closed;
}

/* 面の集合から新しい mesh を起こす (op_part / op_shell の共通部・定義は下) */
static sPtr<cgMesh3D> cg_extract_faces(Mesh &src, const std::vector<Mesh::Face_index> &want);

/* ---- i 番目の片 (#3525) ----
 * ★★ 2 つの道がある。**どちらを通ったかで索引の意味が違う**ので、混ぜずに書き分けてある:
 *
 *   ① 順序つき片リストを持つ値 (voronoi3d 等) … 面 [partEnd_[i-1], partEnd_[i]) を取り出す。
 *      索引は **値が実体として持っている順序** ⇒ 「セル i = サイト i」が構造的に保たれる。
 *   ② 持たない値 … **面の連結成分**に落ちる。⚠ 索引は走査順 = *実装依存* (#3527)。
 *      ⚠⚠ 空洞 (符号つき体積が負のシェル) が在ると「どの空洞がどの塊のものか」= 入れ子が
 *        要るので **明示エラー**にする。op_topology が「数えるだけ」にしてあるのと同じ理由で、
 *        ここで適当に外殻だけ返すと *体積が黙って増える*。⇒ nef の part を名指してもらう。
 */
sPtr<cgMesh3D>
cgMesh3D::op_part(int i, const char **why)
{
	namespace PMP = CGAL::Polygon_mesh_processing;
	typedef Mesh::Face_index   FaceI;
	typedef Mesh::Vertex_index VertI;
	*why = 0;
	if ( i < 0 ) { *why = "the index must be >= 0"; return sPtr<cgMesh3D>(); }

	std::vector<FaceI> want;   /* 取り出す面 (順序は元のまま) */
	if ( has_parts() ) {
		if ( (std::size_t)i >= partEnd_.size() ) { *why = "index out of range"; return sPtr<cgMesh3D>(); }
		const uint32_t b = ( i == 0 ) ? 0u : partEnd_[(std::size_t)i-1];
		const uint32_t e = partEnd_[(std::size_t)i];
		uint32_t k = 0;
		for ( FaceI f : box().m.faces() ) {
			if ( k >= b && k < e ) want.push_back(f);
			++k;
		}
	} else {
		/* ★★★ #3527 (2026-09-17): **塊は入れ子込みで取り出す**。
		 * ⚠⚠ 以前はここで @PMP::connected_components@ (= **面**の連結成分 = 殻) を使い、
		 *   空洞が在ると「入れ子が要る」と言って断っていた。**それは実装の都合**で、
		 *   CGAL 本体は @PMP::volume_connected_components@ で **塊 (volume)** を直接くれる。
		 *   ⇒ 断る理由が無くなったので解けるようにした。
		 * ★ なぜ殻では駄目か: 同心の中空球 (r=2 の中に r=1 の空洞) は
		 *     塊 1 個 ・ 殻 2 枚。材料は r=1 と r=2 の **間**に在るので、
		 *     その塊の境界は **2 枚の殻の両方**。
		 *     ⇒ 殻 1 枚を返すと *中身の詰まった半径 2 の球* = **別の立体**になり、
		 *       体積が 28.64 → 32.73 と **黙って増える** (実測)。
		 * ★ 検定できる形: **Σ volume(part(m,i)) == volume(m)** (符号なしで成立)。
		 *   ⚠ 殻のほうは **符号つきでしか**成立しない (shell は「値を分割する片」ではない)。
		 *   この 2 つが別の式であることが、part と shell が別物である理由そのもの。
		 * ⚠ 前提は「閉じた三角形メッシュ」(CGAL の precondition)。満たさないものは
		 *   そもそも塊が定義できないので、**理由を言って断る** (黙って殻を返さない)。 */
		if ( ! CGAL::is_closed(box().m) ) {
			*why = "this mesh is not closed, so it has no solids to take apart "
			       "(an open surface bounds nothing); use shell(m,i) to take a boundary component instead";
			return sPtr<cgMesh3D>();
		}
		if ( ! CGAL::is_triangle_mesh(box().m) ) {
			*why = "this mesh has non-triangular faces, which the volume decomposition does not accept; "
			       "triangulate it first";
			return sPtr<cgMesh3D>();
		}
		Mesh::Property_map<FaceI,std::size_t> fvol =
		    box().m.add_property_map<FaceI,std::size_t>("f:cgvol", 0).first;
		const std::size_t nv = PMP::volume_connected_components(box().m, fvol);
		if ( nv == 0 || (std::size_t)i >= nv ) {
			box().m.remove_property_map(fvol);
			*why = "index out of range";
			return sPtr<cgMesh3D>();
		}
		/* ★ 同じ塊に属する **全部の殻**の面を集める (外殻 + その空洞)。
		 *   ⇒ cg_extract_faces に渡す面の集合が変わるだけで、抽出そのものは共通部のまま。 */
		for ( FaceI f : box().m.faces() )
			if ( fvol[f] == (std::size_t)i ) want.push_back(f);
		box().m.remove_property_map(fvol);
	}
	if ( want.empty() ) { *why = "the part is empty"; return sPtr<cgMesh3D>(); }
	return cg_extract_faces(box().m, want);
}

/* ---- ★ #3527: i 番目の **殻** (面の連結成分) ----
 * ★ op_part との違いは **空洞を断らないこと** だけ。殻は面の連結成分そのものなので、
 *   入れ子 (どの空洞がどの塊のものか) を知らなくても取り出せる。
 * ★★ 向きは **そのまま**。⇒ 空洞の殻は volume() が **負**で返り、符号が判別子になる。
 * ⚠ 順序つき片リスト (voronoi 等) は **見ない**。あれは「セル」であって殻ではないので、
 *   殻は常に連結成分で数え直す (op_topology の nshells と同じ列)。
 */
sPtr<cgMesh3D>
cgMesh3D::op_shell(int i, const char **why)
{
	namespace PMP = CGAL::Polygon_mesh_processing;
	typedef Mesh::Face_index FaceI;
	*why = 0;
	if ( i < 0 ) { *why = "the index must be >= 0"; return sPtr<cgMesh3D>(); }
	Mesh::Property_map<FaceI,std::size_t> fcc =
	    box().m.add_property_map<FaceI,std::size_t>("f:cgshell", 0).first;
	const std::size_t nc = PMP::connected_components(box().m, fcc);
	if ( nc == 0 || (std::size_t)i >= nc ) {
		box().m.remove_property_map(fcc);
		*why = "index out of range";
		return sPtr<cgMesh3D>();
	}
	std::vector<FaceI> want;
	for ( FaceI f : box().m.faces() )
		if ( fcc[f] == (std::size_t)i ) want.push_back(f);
	box().m.remove_property_map(fcc);
	if ( want.empty() ) { *why = "the shell is empty"; return sPtr<cgMesh3D>(); }
	return cg_extract_faces(box().m, want);
}

/* ---- ★ #3527: 点 p に **いちばん近い殻** ----
 * ★ 距離は **厳密** (EPECK の squared_distance)。⇒ 同距離の判定も厳密に効く。
 * ⚠ 面は三角形とは限らないので **扇状に分けて**測る (四角面のまま渡されても正しい)。
 * ⚠ 同距離が 2 つ以上あれば **断る**。角の真上などで起きる。 */
sPtr<cgMesh3D>
cgMesh3D::op_shell_at(const double p[3], const char **why)
{
	namespace PMP = CGAL::Polygon_mesh_processing;
	typedef Mesh::Face_index FaceI;
	*why = 0;
	if ( box().m.number_of_faces() == 0 ) { *why = "the mesh has no faces"; return sPtr<cgMesh3D>(); }
	Mesh::Property_map<FaceI,std::size_t> fcc =
	    box().m.add_property_map<FaceI,std::size_t>("f:cgshellat", 0).first;
	const std::size_t nc = PMP::connected_components(box().m, fcc);
	if ( nc == 0 ) { box().m.remove_property_map(fcc); *why = "the mesh has no shells"; return sPtr<cgMesh3D>(); }

	const Point_3 q(p[0], p[1], p[2]);
	std::vector<K::FT> best((std::size_t)nc);
	std::vector<char>  seen((std::size_t)nc, 0);
	for ( FaceI f : box().m.faces() ) {
		std::vector<Point_3> pv;
		for ( Mesh::Vertex_index v : CGAL::vertices_around_face(box().m.halfedge(f), box().m) )
			pv.push_back(box().m.point(v));
		if ( pv.size() < 3 ) continue;
		const std::size_t c = fcc[f];
		for ( std::size_t t = 2 ; t < pv.size() ; ++t ) {
			const K::FT d = CGAL::squared_distance(q, K::Triangle_3(pv[0], pv[t-1], pv[t]));
			if ( ! seen[c] || d < best[c] ) { best[c] = d; seen[c] = 1; }
		}
	}
	std::size_t win = 0; int nwin = 0;
	for ( std::size_t c = 0 ; c < (std::size_t)nc ; ++c ) {
		if ( ! seen[c] ) continue;
		if ( nwin == 0 || best[c] < best[win] ) { win = c; nwin = 1; }
		else if ( best[c] == best[win] ) ++nwin;          /* ★ 厳密比較 */
	}
	if ( nwin == 0 ) { box().m.remove_property_map(fcc); *why = "the mesh has no usable faces"; return sPtr<cgMesh3D>(); }
	if ( nwin > 1 ) {
		box().m.remove_property_map(fcc);
		*why = "more than one shell is equally near that point, so which one is meant is ambiguous "
		       "(move the point off the symmetry, or name the shell by index with shell(m,i))";
		return sPtr<cgMesh3D>();
	}
	std::vector<FaceI> want;
	for ( FaceI f : box().m.faces() ) if ( fcc[f] == win ) want.push_back(f);
	box().m.remove_property_map(fcc);
	if ( want.empty() ) { *why = "the shell is empty"; return sPtr<cgMesh3D>(); }
	return cg_extract_faces(box().m, want);
}

/* ---- 面の集合から新しい mesh を起こす (op_part / op_shell の共通部) ----
 * ⚠ **共通化しておくこと**。以前は片側にしか無く、もう片方を書くときに写す形になって
 *   いた ⇒ 片方だけ直すと黙ってずれる (今日 op_vert / op_verts で同じ危険を検査で押さえた)。 */
static sPtr<cgMesh3D>
cg_extract_faces(Mesh &src, const std::vector<Mesh::Face_index> &want)
{
	typedef Mesh::Vertex_index VertI;
	sPtr<cgMesh3D> out = thNEW(cgMesh3D,());
	Mesh& o = out->box().m;
	std::map<VertI,VertI> vmap;
	for ( std::size_t k = 0 ; k < want.size() ; ++k ) {
		std::vector<VertI> fv;
		for ( VertI v : CGAL::vertices_around_face(src.halfedge(want[k]), src) ) {
			std::map<VertI,VertI>::iterator it = vmap.find(v);
			if ( it == vmap.end() ) it = vmap.insert(std::make_pair(v, o.add_vertex(src.point(v)))).first;
			fv.push_back(it->second);
		}
		o.add_face(fv);
	}
	return out;
}

/* ---- 修復: autorefine で **交差線を実エッジ化** する (EPECK で厳密・常に成功)。
 * ★**自己交差は「解消」されない**。valid() は does_self_intersect を見るので、
 *   自己交差した形状は repair の後も **valid()=0 のまま** である (2026-08-18 実測: 軽い/深い
 *   自己交差 tube・重なる 2 箱を combine したもの、いずれも repair 後 0)。細分は効いている
 *   (自己交差 tube で 86v/168f → 726v/792f) が、交わっていた 2 枚の面はエッジで接したまま残るため。
 *   ★以前ここに「とぐろ tube 等の自己交差を valid(=1) に持ち込む」と書いてあったが **誤り**だった
 *   (テストは健全な箱が素通りすることしか見ていなかったので、誰も気づいていなかった)。
 * 何に使えるか: 交点を実エッジにした mesh は「三角形が辺でしか交わらない」= arrangement 系の
 *   内外判定 (winding number) の **入力条件**を満たす。つまり repair は再構成の *前半* に当たる。
 *   後半 (どのセルが内側か) は cgal/manifold/nef のどれも持っていない → #3435 geogram /
 *   #3438 Cherchi の射程 (mesh arrangements)。---- */
sPtr<cgMesh>
cgMesh3D::op_repair()
{
	namespace PMP = CGAL::Polygon_mesh_processing;
	sPtr<cgMesh3D> out = thNEW(cgMesh3D,());
	out->box().m = box().m;                     /* コピーしてから in-place で精錬 */
	PMP::autorefine(out->box().m);
	return out;
}

/* ---- 断面: 点 P を通り法線 N の平面で切り、2D 断面(cgMesh2D)を返す。
 *
 * ★2026-08-15 改: Polygon_mesh_slicer をやめ、**厳密符号 + 記号的摂動(SoS)の自前カット**にした。
 * 旧実装は「平面が面と共面」のとき slicer が閉ループでなく辺の断片群を返すのに、糊コードが
 *   ① 開ポリラインを暗黙に弦で閉じる ② 3 点未満の断片を黙って捨てる
 * ため、キメラ断面(面積が非整数・ループ数が上下の極限のどちらとも違う)が valid=1 のまま出ていた。
 *
 * 新方式:
 *   - 各頂点の平面からの高さ h = N·(v-P) を **厳密有理数**で持ち、符号で分類する。
 *   - h=0(平面上)の頂点は mode で一貫して倒す = 記号的摂動:
 *       mode=+1 … 平面の直上 h+ε で切る → h=0 の頂点は「下」
 *       mode=-1 … 平面の直下 h-ε で切る → h=0 の頂点は「上」
 *       mode= 0 … +1 と同じ規則で倒す(共面が在れば coplanarOut で呼び側に知らせる)
 *     こうすると **共面の面は 3 頂点が同符号になり自動的に除外**され、旧実装の破綻要因が消える。
 *   - 交点は厳密に解き、**3D の厳密座標のままループに綴じる**(端点の一致は厳密比較)。閉多様体 ×
 *     一般位置なので各交点はちょうど 2 本の線分に現れ、**断面は必ず閉ループになる**(開いたら実装バグ)。
 *   - 2D 射影は最後に 1 回だけ。**軸平行法線なら座標を落とすだけで厳密**、一般法線のみ double 基底
 *     (任意角回転と同じ精度方針)。トポロジーは射影前に確定しているので、丸めでループが壊れない。
 *   - 穴の入れ子整理は従来どおり even-odd repair。 ---- */
namespace {

typedef K   SecK;
typedef SecK::FT    SecFT;

struct SecPtLess {   /* 厳密辞書式(交点の同一性判定に使う) */
	bool operator()(const SecK::Point_3& a, const SecK::Point_3& b) const {
		if ( a.x() != b.x() ) return a.x() < b.x();
		if ( a.y() != b.y() ) return a.y() < b.y();
		return a.z() < b.z();
	}
};

/* リングから連続重複点(と末尾=先頭の重複)を落とす。 */
static CGAL::Polygon_2<SecK> sec_dedup_ring(const CGAL::Polygon_2<SecK>& r)
{
	CGAL::Polygon_2<SecK> o;
	for ( auto v = r.vertices_begin() ; v != r.vertices_end() ; ++v ) {
		if ( o.size() > 0 && *v == o[o.size()-1] ) continue;
		o.push_back(*v);
	}
	while ( o.size() >= 2 && o[0] == o[o.size()-1] ) o.erase(o.vertices_end()-1);
	return o;
}

} // namespace

sPtr<cgMesh>
cgMesh3D::op_section(const double P[3], const double N[3], int mode, int *coplanarOut)
{
	if ( coplanarOut ) *coplanarOut = 0;
	SecFT nx(N[0]), ny(N[1]), nz(N[2]);
	if ( nx == 0 && ny == 0 && nz == 0 ) return sPtr<cgMesh>();   /* 退化法線 */
	SecFT px(P[0]), py(P[1]), pz(P[2]);

	/* ---- 頂点の高さと符号(SoS で 0 を倒す)---- */
	std::map<Mesh::Vertex_index, SecFT> hmap;
	std::map<Mesh::Vertex_index, int>   smap;
	const int tie = ( mode < 0 ) ? +1 : -1;   /* h=0 を上(-ε)/下(+ε・mode 0 も同じ)へ倒す */
	for ( Mesh::Vertex_index v : box().m.vertices() ) {
		const Mesh::Point& q = box().m.point(v);
		SecFT h = nx*(q.x()-px) + ny*(q.y()-py) + nz*(q.z()-pz);
		hmap[v] = h;
		int sg = (int)CGAL::sign(h);
		smap[v] = ( sg != 0 ) ? sg : tie;
	}

	/* ---- 面ごとに交差線分を作る(共面検出も同時に)---- */
	std::vector<std::pair<SecK::Point_3, SecK::Point_3> > segs;
	for ( Mesh::Face_index f : box().m.faces() ) {
		std::vector<Mesh::Vertex_index> vs;
		Mesh::Halfedge_index h0 = box().m.halfedge(f), hh = h0;
		do { vs.push_back(box().m.target(hh)); hh = box().m.next(hh); } while ( hh != h0 );
		int allzero = 1;
		for ( size_t i = 0 ; i < vs.size() ; ++i )
			if ( hmap[vs[i]] != 0 ) { allzero = 0; break; }
		if ( allzero ) {                       /* 共面の面: 摂動後はどちらか片側に寄るので断面に出ない */
			if ( coplanarOut ) *coplanarOut = 1;
			continue;
		}
		for ( size_t t = 1 ; t + 1 < vs.size() ; ++t ) {   /* 扇分割(三角形単位で処理) */
			Mesh::Vertex_index iv[3] = { vs[0], vs[t], vs[t+1] };
			SecK::Point_3 xp[2];
			int nx_ = 0;
			for ( int e = 0 ; e < 3 && nx_ < 2 ; ++e ) {
				Mesh::Vertex_index a = iv[e], b = iv[(e+1)%3];
				int sa = smap[a], sb = smap[b];
				if ( sa == sb ) continue;                   /* 同側 = 横断しない */
				const SecFT &ha = hmap[a], &hb = hmap[b];
				const Mesh::Point &pa = box().m.point(a), &pb = box().m.point(b);
				SecFT den = ha - hb;
				if ( den == 0 ) continue;                   /* 起きない(sa!=sb なら h も異なる) */
				SecFT tt = ha / den;                        /* 厳密な交点パラメータ */
				xp[nx_++] = SecK::Point_3(pa.x() + tt*(pb.x()-pa.x()),
				                          pa.y() + tt*(pb.y()-pa.y()),
				                          pa.z() + tt*(pb.z()-pa.z()));
			}
			if ( nx_ == 2 && xp[0] != xp[1] ) segs.push_back(std::make_pair(xp[0], xp[1]));
		}
	}
	sPtr<cgMesh2D> out = thNEW(cgMesh2D,());
	if ( segs.empty() ) return out;   /* 交差なし = 空断面(空の cgMesh2D) */

	/* ---- 線分を厳密な端点一致で閉ループに綴じる ---- */
	std::map<SecK::Point_3, std::vector<size_t>, SecPtLess> inc;
	for ( size_t i = 0 ; i < segs.size() ; ++i ) {
		inc[segs[i].first].push_back(i);
		inc[segs[i].second].push_back(i);
	}
	std::vector<char> used(segs.size(), 0);
	std::vector<std::vector<SecK::Point_3> > loops;
	for ( size_t i = 0 ; i < segs.size() ; ++i ) {
		if ( used[i] ) continue;
		std::vector<SecK::Point_3> loop;
		SecK::Point_3 start = segs[i].first, cur = segs[i].second;
		used[i] = 1;
		loop.push_back(start);
		for (;;) {
			loop.push_back(cur);
			if ( cur == start ) break;                  /* 閉じた */
			const std::vector<size_t>& cand = inc[cur];
			size_t nxt = (size_t)-1;
			for ( size_t k = 0 ; k < cand.size() ; ++k )
				if ( ! used[cand[k]] ) { nxt = cand[k]; break; }
			if ( nxt == (size_t)-1 ) break;             /* 行き止まり(非閉入力) */
			used[nxt] = 1;
			cur = ( segs[nxt].first == cur ) ? segs[nxt].second : segs[nxt].first;
		}
		if ( loop.size() > 1 && loop.front() == loop.back() ) loop.pop_back();
		if ( loop.size() >= 3 ) loops.push_back(loop);
	}
	if ( loops.empty() ) return out;

	/* ---- ★★ #3533: 枠は **@src/h/common/affine.h@ の表 1 つ**から取り、射影もそこから導く ----
	 *   ⚠ 以前はここに「どの法線にどの軸対を当てるか」の表が *2 回* (射影用と枠用) べた書きされ、
	 *     「軸の取り方は上の射影と対でなければならない」と **コメントで約束**していた。
	 *     さらに同じ表が occt_mf の polygonize (plane_frame_canonical) と manifold の
	 *     mfaSection にもあり、計 3 箇所。⇒ 片方だけ直すと同じ平面の 2D が経路で別の局所座標になる。
	 *   ★ @PLANE_FRAME_ORIENT_BY_N | ORIGIN_AT_PT@ が section の従来の規約そのもの
	 *     (切った向きに合わせる + 一般法線では利用者の P を原点に)。⇒ **値は 1 ビットも動かない**。 */
	double fo[3], fu[3], fv[3];
	{
		const double Nd[3] = { N[0], N[1], N[2] };
		const double Pd[3] = { P[0], P[1], P[2] };
		if ( ! srava_affine::plane_frame(Nd, Pd,
		         srava_affine::PLANE_FRAME_ORIENT_BY_N | srava_affine::PLANE_FRAME_ORIGIN_AT_PT,
		         fo, fu, fv) )
			return out;   /* 退化法線 (上で弾いているはず) */
	}
	/* ★ 枠が軸に平行なら、局所座標は **厳密なまま**取れる (内積の係数が 0/±1 ちょうど)。
	 *   ⇒ 「座標を落とすだけ」だった従来の厳密さがそのまま残る。 */
	const int exactProj = srava_affine::frame_is_axis_aligned(fu, fv);
	CGAL::Multipolygon_with_holes_2<SecK> mp;
	for ( size_t k = 0 ; k < loops.size() ; ++k ) {
		CGAL::Polygon_2<SecK> poly;
		/* 同一点が連続したら 1 個に潰す(ゼロ長辺を作らない)。平面が頂点をちょうど通ると、その頂点に
		 * 集まる複数の面が同じ交点を出すため連続重複が起きる。幾何は変わらないが、SVG/DXF に
		 * ゼロ長セグメントとして出ると CAM 側で自己交差扱いになるので、ここで落としておく。 */
		for ( size_t i = 0 ; i < loops[k].size() ; ++i ) {
			const SecK::Point_3& q = loops[k][i];
			if ( i > 0 && q == loops[k][i-1] ) continue;
			if ( i + 1 == loops[k].size() && q == loops[k][0] ) continue;
			/* 局所座標 = ((q - O)·U, (q - O)·V)。★ 枠が軸平行なら厳密カーネルのまま。 */
			if ( exactProj ) {
				const SecFT dx = q.x() - SecFT(fo[0]);
				const SecFT dy = q.y() - SecFT(fo[1]);
				const SecFT dz = q.z() - SecFT(fo[2]);
				poly.push_back(SecK::Point_2(
				    dx*SecFT(fu[0]) + dy*SecFT(fu[1]) + dz*SecFT(fu[2]),
				    dx*SecFT(fv[0]) + dy*SecFT(fv[1]) + dz*SecFT(fv[2])));
			} else {
				double dx = CGAL::to_double(q.x()) - fo[0];
				double dy = CGAL::to_double(q.y()) - fo[1];
				double dz = CGAL::to_double(q.z()) - fo[2];
				poly.push_back(SecK::Point_2(SecFT(dx*fu[0] + dy*fu[1] + dz*fu[2]),
				                             SecFT(dx*fv[0] + dy*fv[1] + dz*fv[2])));
			}
		}
		if ( poly.size() >= 3 )
			mp.add_polygon_with_holes(CGAL::Polygon_with_holes_2<SecK>(poly));
	}
	/* even-odd repair: 入れ子ループを外周/穴に整理(断面の内壁=穴)。 */
	auto repaired = CGAL::Polygon_repair::repair(mp);
	/* 連続重複点(ゼロ長辺)を落とす。平面が頂点をちょうど通ると、その頂点に集まる複数の面が
	 * 同じ交点を出すので同一点が並ぶ(repair も残す)。幾何は変わらないが、SVG/DXF にゼロ長
	 * セグメントとして出ると CAM 側で自己交差扱いになるため、ここで一度だけ掃除する。 */
	for ( const auto& pwh : repaired.polygons_with_holes() ) {
		CGAL::Polygon_2<SecK> ob = sec_dedup_ring(pwh.outer_boundary());
		if ( ob.size() < 3 ) continue;
		CGAL::Polygon_with_holes_2<SecK> cleaned(ob);
		for ( auto h = pwh.holes_begin() ; h != pwh.holes_end() ; ++h ) {
			CGAL::Polygon_2<SecK> hh = sec_dedup_ring(*h);
			if ( hh.size() >= 3 ) cleaned.add_hole(hh);
		}
		cg_regions(out).push_back(cleaned);
	}
	/* ★★ #3526: **切った場所に返す**。断面の枠 (平面) を切断平面にする。
	 *   ⚠ これが無いと断面が **黙って z=0 に戻る** — extrude し直したり export したりすると
	 *     *切った場所の情報が消えている*。
	 *   ★ 局所座標は上で枠から導いてあるので、ここは **同じ枠を載せるだけ**。
	 *     ⇒ #3533 以前は「軸の取り方は射影と対でなければならない」という約束を人間が
	 *       守っていたが、いまは 1 つの枠から両方を作るので **ずれようがない**。 */
	out->set_frame(fo, fu, fv);
	/* ★ #3533: @section@ の出力は **常に face3d** (切断平面に置かれている)。
	 *   ⚠ z=0 で切った場合も face3d — 型は切り方に依らない。 */
	out->set_placed(1);
	return out;
}

/* ---- 拡張子取り出し ---- */
static const char* ext_of(const char* path) {
	const char* dot = ::strrchr(path, '.');
	return dot ? dot + 1 : "";
}

/* ---- AMF / 3MF 書き出し ----
 * ★ライタ本体は共通ヘッダ src/h/common/mesh3mf.h (カーネル非依存・自前 XML + 自前 zip)。
 *   manifold.so も同じヘッダを使うので、両カーネルで同じ形式・同じ単位語彙・同じ決定的バイト列になる。
 *   ここは「Surface_mesh → srava_io::TriMesh の詰め替え」だけを持つ:
 *     頂点は m.vertices() の巡回順 (boolean 後の欠番を密 index へ詰め直す)、
 *     面は m.faces() の巡回順で 3 隅を取り (全プリミティブ三角化済み)、f:color があれば面色に。
 *   座標は EPECK (厳密有理) → double (to_double)。 */
static void
fill_trimesh(const Mesh& m, srava_io::TriMesh& out)
{
	std::map<Mesh::Vertex_index, int> id;
	int next = 0;
	out.verts.reserve(m.number_of_vertices() * 3);
	for ( Mesh::Vertex_index v : m.vertices() ) {
		const Point_3& p = m.point(v);
		out.verts.push_back(CGAL::to_double(p.x()));
		out.verts.push_back(CGAL::to_double(p.y()));
		out.verts.push_back(CGAL::to_double(p.z()));
		id[v] = next++;
	}
	std::optional<Mesh::Property_map<Mesh::Face_index, CGAL::IO::Color> > fcol
	    = m.property_map<Mesh::Face_index, CGAL::IO::Color>("f:color");
	out.tris.reserve(m.number_of_faces() * 3);
	if ( fcol.has_value() ) out.faceColor.reserve(m.number_of_faces());
	for ( Mesh::Face_index fc : m.faces() ) {
		Mesh::Halfedge_index h = m.halfedge(fc);
		out.tris.push_back((uint32_t)id[m.source(h)]);
		out.tris.push_back((uint32_t)id[m.target(h)]);
		out.tris.push_back((uint32_t)id[m.target(m.next(h))]);
		if ( fcol.has_value() ) {
			const CGAL::IO::Color& c = (*fcol)[fc];
			out.faceColor.push_back(((uint32_t)c.red() << 16) | ((uint32_t)c.green() << 8) | (uint32_t)c.blue());
		}
	}
}

/* ---- ファイル書き出し(拡張子で振り分け)----
 *   AMF = 自前 XML / 3MF = 自前 XML+zip(どちらも単位つき・全環境・依存なし)。
 *   その他 = OFF/STL/OBJ/PLY(CGAL・単位概念なしで unit 無視)。 */
bool
cgMesh3D::write_to(const char *path, const char *unit)
{
	const char *e = ext_of(path);
	if ( ::strcasecmp(e, "amf") == 0 || ::strcasecmp(e, "3mf") == 0 ) {
		srava_io::TriMesh tm;
		fill_trimesh(box().m, tm);
		return ( ::strcasecmp(e, "amf") == 0 ) ? srava_io::write_amf(path, tm, unit)
		                                       : srava_io::write_3mf(path, tm, unit);
	}
	/* OFF/PLY は面色(f:color)があれば COFF / 色つき PLY で出す(STL/OBJ は face_color_map を無視)。 */
	std::optional<Mesh::Property_map<Mesh::Face_index, CGAL::IO::Color> > fc
	    = box().m.property_map<Mesh::Face_index, CGAL::IO::Color>("f:color");
	if ( fc.has_value() )
		return CGAL::IO::write_polygon_mesh(std::string(path), box().m,
		           CGAL::parameters::face_color_map(*fc));
	return CGAL::IO::write_polygon_mesh(std::string(path), box().m);
}

/* ---- reader 用ファクトリ: D_META タグ → 具体型 ---- */
sPtr<cgMesh>
cgMesh::create_for_meta(const uint8_t *meta, int len)
{
	if ( len == 4 && meta[0]=='M' && meta[1]=='E' && meta[2]=='S' && meta[3]=='H' )
		return thNEW(cgMesh3D,());
	if ( len == 4 && meta[0]=='P' && meta[1]=='L' && meta[2]=='Y' && meta[3]=='2' )
		return thNEW(cgMesh2D,());
	/* ★ #3404: Manifold カーネルの出力キャッシュ "MFM3" も 3D として受理し、decode 時に EPECK へ
	 *   無損失昇格する(cg agent が Manifold 入力を透過的に読める)。昇格後は普通の cgMesh3D。
	 *   ★ #3435 / 2026-08-19: **geogram の値もこの形式** (自前 4CC "GGM3" は撤去し、同一レイアウト
	 *   なので MFM3 を共有)。geogram のブールは厳密述語/厳密構成だが結果の頂点は double に落ちるので、
	 *   EPECK へは manifold と同様に無損失昇格になる (double は 2 進有理数)。 */
	if ( len == 4 && meta[0]=='M' && meta[1]=='F' && meta[2]=='M' && meta[3]=='3' ) {
		sPtr<cgMesh3D> m = thNEW(cgMesh3D,());
		m->set_mfm3_input();
		return m;
	}
	/* ★ #3404: Manifold 2D "MFC2" も cgMesh2D として受理し decode 時に EPECK Pwh へ無損失昇格
	 *   (混成 2D combine・SVG/DXF 出力を CGAL 側で扱うため)。 */
	if ( len == 4 && meta[0]=='M' && meta[1]=='F' && meta[2]=='C' && meta[3]=='2' ) {
		sPtr<cgMesh2D> m = thNEW(cgMesh2D,());
		m->set_mfc2_input();
		return m;
	}
	/* ★ #3433: Nef カーネルの出力 "NEFB" (nef_hybrid) を 3D として受理。payload 先頭 1 バイトが
	 *   形式で、厳密境界 (=1) または SNC+境界 (=2) なら **CGAL Nef 無しで**後半を読める。
	 *   ★★ #3499: **"NEF3" (nef_snc) は受理しない**。nef_snc は「常に SNC だけ」を書くように
	 *     戻したので、ここで受けても中身を読めない (この TU に SNC の reader が無い。
	 *     ⚠ #3559 より前の理由は「cgal.so は CGAL Nef 非依存 = #3440」だったが、その線は
	 *     畳まれた — decode_nef3 の ⚠⚠ を見よ)。nf-mesh3d → cg-mesh3d の変換は **橋モジュール nef_cg.so** が
	 *     持つ。ここで名乗ったままにすると、橋があるのに cgal 側の reader が先に掴んで
	 *     「読めない」で落ちる (実際に踏んだ)。 */
	if ( len == 4 && ::memcmp(meta, "NEFB", 4) == 0 ) {
		sPtr<cgMesh3D> m = thNEW(cgMesh3D,());
		m->set_nef3_input();
		return m;
	}
	return sPtr<cgMesh>();
}

/* ---- ★ #3527: 点 p を **含む** 塊 (2026-09-18) ---------------------------------
 * ★★ @shell_at@ が「いちばん近い殻」なのにこちらは「**含む**塊」— 非対称に見えるが理由がある。
 *   **立体は内側を持ち、曲面は持たない**。殻は面の連結成分 (曲面) なので「含む」が定義できず
 *   最近傍しか言えない。塊は立体なので内外が言える。⇒ どちらも「その位置に在る片を指す」という
 *   1 つの規約の、次元による 2 つの姿 (geomutils の part_at とまったく同じ約束)。
 * ★ cgal は **厳密**に判定する — @Side_of_triangle_mesh@ (EPECK の述語)。
 *   gu は double の巻き数で近似するので、境界近傍の判定はこちらのほうが強い。
 * ★ 塊ごとに「その塊を作る面だけ」で判定する。@volume_connected_components@ が
 *   *空洞を含めて* 1 つの塊にまとめてくれるので、空洞の中は自然に外側と判定される。
 * ⚠ 空洞の中と立体の外はどちらも「そこに材料は無い」⇒ **明示エラー**。黙って近い塊を返さない。
 * ⚠ 前提は「閉じた三角形メッシュ」(op_part と同じ CGAL の precondition)。 */
sPtr<cgMesh3D>
cgMesh3D::op_part_at(const double p[3], const char **why)
{
	namespace PMP = CGAL::Polygon_mesh_processing;
	typedef Mesh::Face_index FaceI;
	*why = 0;
	if ( ! CGAL::is_closed(box().m) ) {
		*why = "this mesh is not closed, so it has no solids to pick from "
		       "(an open surface bounds nothing); use shell_at(m,p) to take the nearest boundary shell instead";
		return sPtr<cgMesh3D>();
	}
	if ( ! CGAL::is_triangle_mesh(box().m) ) {
		*why = "this mesh has non-triangular faces, which the volume decomposition does not accept; "
		       "triangulate it first";
		return sPtr<cgMesh3D>();
	}
	Mesh::Property_map<FaceI,std::size_t> fvol =
	    box().m.add_property_map<FaceI,std::size_t>("f:cgvolat", 0).first;
	const std::size_t nv = PMP::volume_connected_components(box().m, fvol);
	if ( nv == 0 ) {
		box().m.remove_property_map(fvol);
		*why = "this mesh has no solids";
		return sPtr<cgMesh3D>();
	}
	const Point_3 q(p[0], p[1], p[2]);
	int found = -1, nfound = 0;
	for ( std::size_t c = 0 ; c < nv ; ++c ) {
		/* その塊の面だけを抜いた mesh に対して内外を訊く。
		 * ⚠ 面の部分集合をそのまま Side_of_triangle_mesh に渡せないので、塊を起こしてから測る。 */
		std::vector<FaceI> want;
		for ( FaceI f : box().m.faces() ) if ( fvol[f] == c ) want.push_back(f);
		if ( want.empty() ) continue;
		sPtr<cgMesh3D> piece = cg_extract_faces(box().m, want);
		if ( ! piece.is_notNull() ) continue;
		CGAL::Side_of_triangle_mesh<Mesh, K> inside(piece->box().m);
		const CGAL::Bounded_side sd = inside(q);
		if ( sd == CGAL::ON_BOUNDED_SIDE || sd == CGAL::ON_BOUNDARY ) { found = (int)c; ++nfound; }
	}
	box().m.remove_property_map(fvol);
	if ( nfound == 0 ) {
		*why = "no solid of this mesh is at that point (the point is outside the mesh, or inside "
		       "a cavity, where there is no material); use shell_at(m,p) to take the nearest "
		       "boundary shell instead";
		return sPtr<cgMesh3D>();
	}
	if ( nfound > 1 ) {
		*why = "more than one solid of this mesh contains that point, so its parts overlap "
		       "(the mesh is not valid); check valid(m) first";
		return sPtr<cgMesh3D>();
	}
	return op_part(found, why);
}

/* ---- ★ #3527: 面 i の **頂点番号** [i0,i1,i2] (2026-09-18) ---------------------
 * ★★ @vert@ (座標) と **わざと分けてある**。連結関係が要る場面では番号が要り、座標から番号へ
 *   引き戻すのは丸めが絡んで危うい (#3527 本文の⚠)。
 * ⚠⚠ 番号は @op_vert@ / @op_verts@ と **同じ列**を指すこと。あちらは @m.vertices()@ の走査順で
 *   積んでいるので、ここも同じ順で番号を振る。片方だけ変えると黙ってずれる。
 *   ★ Surface_mesh の @Vertex_index@ は削除が無ければ 0..n-1 と一致するが、**それに頼らない** —
 *     頼ると「削除のあった mesh で黙ってずれる」形になる。⇒ 走査順から引く。
 * ⚠ 三角形以外の面は 3 頂点で名乗れないので断る (返り 0)。 */
int
cgMesh3D::op_face_verts(int i, int out[3])
{
	typedef Mesh::Face_index   FaceI;
	typedef Mesh::Vertex_index VertI;
	if ( i < 0 ) return 0;
	/* 頂点 → op_vert の索引 (走査順) */
	std::unordered_map<std::size_t,int> ix;
	{
		int k = 0;
		for ( VertI v : box().m.vertices() ) ix[(std::size_t)v] = k++;
	}
	int fk = 0;
	for ( FaceI f : box().m.faces() ) {
		if ( fk++ != i ) continue;
		int n = 0;
		for ( VertI v : CGAL::vertices_around_face(box().m.halfedge(f), box().m) ) {
			if ( n >= 3 ) return 0;                    /* 三角形でない */
			std::unordered_map<std::size_t,int>::const_iterator it = ix.find((std::size_t)v);
			if ( it == ix.end() ) return 0;
			out[n++] = it->second;
		}
		return ( n == 3 ) ? 3 : 0;
	}
	return 0;
}
