/*
 * cgMesh3D — 3D Surface_mesh(EPECK)の多態メソッド実装(Step B)。
 * ブーリアン(corefinement)・アフィン変換・codec・get_str/factory をここに集約。CGAL リンク側で compile。
 * cgaUnion 等の計算本体はこれらを virtual 越しに呼ぶだけ(次元非依存)。
 */
#include	"cg/c++/cgMesh.h"
#include	"cg/c++/cgaMeshCodec.h"
#include	"ts2/c++/stdString.h"

#include	<CGAL/Polygon_mesh_processing/corefinement.h>
#include	<CGAL/Aff_transformation_3.h>
#include	<CGAL/Polygon_mesh_processing/orientation.h>   /* reverse_face_orientations */
#include	<CGAL/boost/graph/IO/polygon_mesh_io.h>        /* 拡張子で OFF/STL/OBJ/PLY 書き出し */
#include	<CGAL/Nef_polyhedron_3.h>                       /* 3D offset = Minkowski(球) */
#include	<CGAL/minkowski_sum_3.h>
#include	<CGAL/convex_decomposition_3.h>
#include	<CGAL/boost/graph/convert_nef_polyhedron_to_polygon_mesh.h>
#include	<CGAL/boost/graph/generators.h>                /* make_icosahedron */
#include	<CGAL/boost/graph/copy_face_graph.h>           /* combine(単純連結・corefinement なし) */
#include	<boost/property_map/property_map.hpp>          /* combine の face_to_face_map(色保持) */
#include	<CGAL/Polygon_mesh_processing/triangulate_faces.h>
#include	<CGAL/Polygon_mesh_processing/measure.h>   /* area / volume */
#include	<CGAL/Polygon_mesh_processing/self_intersections.h>   /* does_self_intersect(valid 検査) */
#include	<CGAL/Polygon_mesh_processing/autorefinement.h>       /* autorefine(repair) */
#include	<CGAL/Polygon_mesh_slicer.h>                          /* section(平面で切る) */
#include	<CGAL/Polygon_2.h>
#include	<CGAL/Polygon_with_holes_2.h>
#include	<CGAL/Polygon_repair/repair.h>                        /* even-odd で断面を塗り領域化 */
#include	<CGAL/number_utils.h>                                 /* to_double */
#include	<CGAL/boost/graph/helpers.h>               /* is_closed */
#include	<CGAL/AABB_tree.h>                          /* 近接(closest_point) */
#include	<CGAL/AABB_traits_3.h>
#include	<CGAL/AABB_face_graph_triangle_primitive.h>
#include	<CGAL/Subdivision_method_3/subdivision_methods_3.h>   /* icosphere 細分化 */
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
#include	<string.h>     /* strrchr */
#include	<strings.h>    /* strcasecmp */
#include	<stdint.h>     /* uint32_t(zip ヘッダ) */

/* ---- get_str(RTTI/vtable anchor)。基底 cgMesh::get_str は cgMesh.cpp に ---- */
sPtr<stdString>
cgMesh3D::get_str()
{
	return thNEW(stdString,("<cgMesh3D>"));
}

/* ---- codec(cgChunkSink/Source 越しに cgaMeshCodec を駆動)---- */
void
cgMesh3D::encode(cgChunkSink& sink)
{
	cgaMeshCodec::encode(m_, sink);   /* Sink=cgChunkSink。chunk() は virtual 呼び */
}
void
cgMesh3D::decode(cgChunkSource& src)
{
	cgaMeshCodec::decode(src, m_);    /* Source=cgChunkSource。pull() は virtual 呼び */
}

/* ---- ブーリアン(corefinement)。b が cgMesh3D でなければ null=エラー ----
 * corefinement の前提 = 両入力が「閉じた・自己交差しない多様体」。前段が接触/同一平面で破綻した
 * 不正メッシュ(非閉 or 自己交差)を渡すと CGAL が **segfault** する。これを防ぐ二段の安全策:
 *   ① is_closed(安い)で非閉を弾く。
 *   ② throw_on_self_intersection(true) で自己交差を**例外化**し try/catch で受ける
 *      (これが無いと自己交差入力で落ちる)。失敗は全て null=エラーにして上位で案内する。 */
static bool both_closed(const cgMesh::Mesh& a, const cgMesh::Mesh& b) {
	return CGAL::is_closed(a) && CGAL::is_closed(b);
}
sPtr<cgMesh>
cgMesh3D::op_union(sPtr<cgMesh> b)
{
	sPtr<cgMesh3D> mb = sPtr<cgMesh3D>::d_cast(b);
	if ( ! mb.is_notNull() ) return sPtr<cgMesh>();   /* 異次元/null */
	if ( ! both_closed(m_, mb->m_) ) return sPtr<cgMesh>();   /* 非閉=前段で破綻 → クラッシュ回避 */
	sPtr<cgMesh3D> out = thNEW(cgMesh3D,());
	bool ok = false;
	try {
		ok = CGAL::Polygon_mesh_processing::corefine_and_compute_union(
		         m_, mb->m_, out->m_,
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
	if ( ! both_closed(m_, mb->m_) ) return sPtr<cgMesh>();
	sPtr<cgMesh3D> out = thNEW(cgMesh3D,());
	bool ok = false;
	try {
		ok = CGAL::Polygon_mesh_processing::corefine_and_compute_intersection(
		         m_, mb->m_, out->m_,
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
	if ( ! both_closed(m_, mb->m_) ) return sPtr<cgMesh>();
	sPtr<cgMesh3D> out = thNEW(cgMesh3D,());
	bool ok = false;
	try {
		ok = CGAL::Polygon_mesh_processing::corefine_and_compute_difference(
		         m_, mb->m_, out->m_,
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
	out->m_ = m_;                              /* A をコピー(A の f:color も複製される) */
	/* B の面 → out の新面 の対応を取りつつ追記(別連結成分として)。 */
	std::map<Mesh::Face_index, Mesh::Face_index> f2f;
	boost::associative_property_map<std::map<Mesh::Face_index, Mesh::Face_index> > f2f_pm(f2f);
	CGAL::copy_face_graph(mb->m_, out->m_, CGAL::parameters::face_to_face_map(f2f_pm));
	/* ★ 色の保持: A か B のどちらかが面色を持つなら out に f:color を用意し各面に正しい色を入れる
	 *   (未着色面は灰 180)。A 面は out->m_ コピーで保持済(A 着色時)/未着色なら default 灰。
	 *   B 面は copy 先の default が A 由来になりうるので f2f で明示上書きする。 */
	std::optional<Mesh::Property_map<Mesh::Face_index, CGAL::IO::Color> > acol
	    = m_.property_map<Mesh::Face_index, CGAL::IO::Color>("f:color");
	std::optional<Mesh::Property_map<Mesh::Face_index, CGAL::IO::Color> > bcol
	    = mb->m_.property_map<Mesh::Face_index, CGAL::IO::Color>("f:color");
	if ( acol.has_value() || bcol.has_value() ) {
		CGAL::IO::Color gray((unsigned char)180, (unsigned char)180, (unsigned char)180);
		Mesh::Property_map<Mesh::Face_index, CGAL::IO::Color> oc
		    = out->m_.add_property_map<Mesh::Face_index, CGAL::IO::Color>("f:color", gray).first;
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
	out->m_ = m_;   /* deep copy */
	CGAL::IO::Color c((unsigned char)r, (unsigned char)g, (unsigned char)b);
	Mesh::Property_map<Mesh::Face_index, CGAL::IO::Color> fc
	    = out->m_.add_property_map<Mesh::Face_index, CGAL::IO::Color>("f:color", c).first;
	for ( Mesh::Face_index f : out->m_.faces() )
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
	Mesh& m = out->m_;
	m = m_;   /* deep copy */
	for ( Mesh::Vertex_index v : m.vertices() )
		m.point(v) = aff.transform(m.point(v));

	/* 3x3 線形部の行列式 < 0(反射)なら面の向きを反転して outward 法線を保つ。 */
	K::FT det =
	    aff.m(0,0) * ( aff.m(1,1)*aff.m(2,2) - aff.m(1,2)*aff.m(2,1) )
	  - aff.m(0,1) * ( aff.m(1,0)*aff.m(2,2) - aff.m(1,2)*aff.m(2,0) )
	  + aff.m(0,2) * ( aff.m(1,0)*aff.m(2,1) - aff.m(1,1)*aff.m(2,0) );
	if ( det < K::FT(0) )
		CGAL::Polygon_mesh_processing::reverse_face_orientations(m);
	return out;
}

/* 半径 r・細分化 subdiv の icosphere(icosahedron を Loop 細分化し球面へ投影)を ball に。
 * subdiv=0=icosahedron(20 面・粗い)、1=80 面、2=320 面…(大=滑らか・Minkowski が重い)。
 * 投影は double 正規化(sqrt)→ K::FT 格納(近似形状・有理座標、回転と同じ方針)。
 * cgaSphere.cpp と共有(sphere(r, subdiv) も同じ球近似)→ 非 static。 */
void cga_make_icosphere(cgMesh::Mesh& ball, double r, int subdiv)
{
	typedef cgMesh::K K;
	CGAL::make_icosahedron<cgMesh::Mesh, K::Point_3>(ball, K::Point_3(0,0,0), K::FT(r));
	CGAL::Polygon_mesh_processing::triangulate_faces(ball);
	for ( int i = 0 ; i < subdiv ; ++i )
		CGAL::Subdivision_method_3::Loop_subdivision(ball, CGAL::parameters::number_of_iterations(1));
	if ( subdiv > 0 ) {
		for ( cgMesh::Mesh::Vertex_index v : ball.vertices() ) {
			double x = CGAL::to_double(ball.point(v).x());
			double y = CGAL::to_double(ball.point(v).y());
			double z = CGAL::to_double(ball.point(v).z());
			double L = std::sqrt(x*x + y*y + z*z);
			if ( L > 0 ) { double s = r / L;
				ball.point(v) = K::Point_3(K::FT(x*s), K::FT(y*s), K::FT(z*s)); }
		}
		CGAL::Polygon_mesh_processing::triangulate_faces(ball);
	}
}

/* ---- 3D オフセット = Minkowski 和(半径 |d| の icosphere 球)。d>0 膨張 / d<0 収縮(補集合トリック)。
 *      subdiv 大=丸めが滑らか・重い(Nef + 凸分解)が srava はそれ向け。 ---- */
sPtr<cgMesh>
cgMesh3D::op_offset(double d, int subdiv)
{
	typedef CGAL::Nef_polyhedron_3<K> Nef;
	if ( d == 0.0 ) {
		sPtr<cgMesh3D> out = thNEW(cgMesh3D,());
		out->m_ = m_;
		return out;
	}
	if ( subdiv < 0 ) subdiv = 0;
	if ( subdiv > 3 ) subdiv = 3;   /* 上限(暴走防止) */
	double r = ( d > 0.0 ) ? d : -d;

	/* 半径 r の icosphere を球の近似に。 */
	Mesh ball;
	cga_make_icosphere(ball, r, subdiv);
	Nef nball(ball);

	sPtr<cgMesh3D> out = thNEW(cgMesh3D,());

	if ( d > 0.0 ) {
		/* 膨張: A ⊕ ball。 */
		Nef nbody(m_);
		Nef nres = CGAL::minkowski_sum_3(nbody, nball);   /* 内部で凸分解(重い) */
		CGAL::convert_nef_polyhedron_to_polygon_mesh(nres, out->m_, true /* 三角化 */);
		return out;
	}

	/* 収縮(d<0): 補集合トリック erode(A,r) = A − dilate(boundingBox − A, ball_r)。
	 * 有界化のため A の bbox を margin(>r)拡大した箱 B を使う(B 壁付近の誤侵食を避ける)。 */
	double minx=1e300, miny=1e300, minz=1e300, maxx=-1e300, maxy=-1e300, maxz=-1e300;
	for ( Mesh::Vertex_index v : m_.vertices() ) {
		double x = CGAL::to_double(m_.point(v).x());
		double y = CGAL::to_double(m_.point(v).y());
		double z = CGAL::to_double(m_.point(v).z());
		if ( x<minx ) minx=x; if ( x>maxx ) maxx=x;
		if ( y<miny ) miny=y; if ( y>maxy ) maxy=y;
		if ( z<minz ) minz=z; if ( z>maxz ) maxz=z;
	}
	if ( minx > maxx ) return out;   /* 空 */
	double mg = 2.0 * r;             /* margin > r */
	minx-=mg; miny-=mg; minz-=mg; maxx+=mg; maxy+=mg; maxz+=mg;
	Mesh box;
	CGAL::make_hexahedron(
	    K::Point_3(K::FT(minx),K::FT(miny),K::FT(minz)), K::Point_3(K::FT(maxx),K::FT(miny),K::FT(minz)),
	    K::Point_3(K::FT(maxx),K::FT(maxy),K::FT(minz)), K::Point_3(K::FT(minx),K::FT(maxy),K::FT(minz)),
	    K::Point_3(K::FT(minx),K::FT(miny),K::FT(maxz)), K::Point_3(K::FT(maxx),K::FT(miny),K::FT(maxz)),
	    K::Point_3(K::FT(maxx),K::FT(maxy),K::FT(maxz)), K::Point_3(K::FT(minx),K::FT(maxy),K::FT(maxz)), box);
	CGAL::Polygon_mesh_processing::triangulate_faces(box);

	Nef nefA(m_), nefBox(box);
	Nef nefOut = nefBox - nefA;                          /* 箱内の外側(A の補集合を有界化) */
	Nef nefDil = CGAL::minkowski_sum_3(nefOut, nball);   /* 外側を r 膨張(A 内へ r シェル侵入) */
	Nef nefRes = nefA - nefDil;                          /* A から r シェルを除く = 収縮 */
	CGAL::convert_nef_polyhedron_to_polygon_mesh(nefRes, out->m_, true);
	return out;
}

/* ---- 計測: 表面積(全三角形面積の和。√を含むので double で返す)---- */
double
cgMesh3D::op_area()
{
	return CGAL::to_double(CGAL::Polygon_mesh_processing::area(m_));
}

/* ---- 計測: 体積(閉メッシュ。発散定理。√を含まないが有理→double)---- */
double
cgMesh3D::op_volume()
{
	return CGAL::to_double(CGAL::Polygon_mesh_processing::volume(m_));
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
	double cx = 0, cy = 0, cz = 0, vol = 0;
	for ( Mesh::Face_index f : m_.faces() ) {
		Mesh::Halfedge_index h = m_.halfedge(f);
		const Point_3& A = m_.point(m_.source(h));
		const Point_3& B = m_.point(m_.target(h));
		const Point_3& C = m_.point(m_.target(m_.next(h)));
		double ax = CGAL::to_double(A.x()), ay = CGAL::to_double(A.y()), az = CGAL::to_double(A.z());
		double bx = CGAL::to_double(B.x()), by = CGAL::to_double(B.y()), bz = CGAL::to_double(B.z());
		double dx = CGAL::to_double(C.x()), dy = CGAL::to_double(C.y()), dz = CGAL::to_double(C.z());
		/* 四面体(原点,A,B,C)の符号付き体積 = (A·(B×C))/6 */
		double v = ( ax*(by*dz - bz*dy) - ay*(bx*dz - bz*dx) + az*(bx*dy - by*dx) ) / 6.0;
		vol += v;
		/* 四面体重心 = (0+A+B+C)/4 */
		cx += v * (ax + bx + dx) / 4.0;
		cy += v * (ay + by + dy) / 4.0;
		cz += v * (az + bz + dz) / 4.0;
	}
	if ( vol != 0.0 ) { out[0] = cx/vol; out[1] = cy/vol; out[2] = cz/vol; }
	else              { out[0] = out[1] = out[2] = 0.0; }
	return 3;
}

/* ---- 計測: 軸平行バウンディングボックス(全頂点走査で min/max)。空メッシュは全 0。---- */
int
cgMesh3D::op_bbox(double mn[3], double mx[3])
{
	bool first = true;
	for ( Mesh::Vertex_index v : m_.vertices() ) {
		double x = CGAL::to_double(m_.point(v).x());
		double y = CGAL::to_double(m_.point(v).y());
		double z = CGAL::to_double(m_.point(v).z());
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
	typedef CGAL::AABB_face_graph_triangle_primitive<cgMesh3D::Mesh>	CgPrim;
	typedef CGAL::AABB_traits_3<cgMesh3D::K, CgPrim>		CgAabbTraits;
	typedef CGAL::AABB_tree<CgAabbTraits>				CgTree;

	inline void pt_to(const cgMesh3D::Point_3& p, double out[3]) {
		out[0] = CGAL::to_double(p.x());
		out[1] = CGAL::to_double(p.y());
		out[2] = CGAL::to_double(p.z());
	}
	inline double dist2(const cgMesh3D::Point_3& a, const cgMesh3D::Point_3& b) {
		double dx = CGAL::to_double(a.x()-b.x());
		double dy = CGAL::to_double(a.y()-b.y());
		double dz = CGAL::to_double(a.z()-b.z());
		return dx*dx + dy*dy + dz*dz;
	}
}

/* 最遠=頂点ペア総当り(極値は頂点で達成され厳密。O(|VA|·|VB|))。
 * 最近接=AABB で「A 頂点→B 面」「B 頂点→A 面」両方向の最小(頂点-面の近似。辺-辺の谷は取りこぼす)。 */
double
cgMesh3D::op_proximity(cgMesh3D& b, bool farthest, double pa[3], double pb[3])
{
	if ( farthest ) {
		double best = -1.0;
		for ( Mesh::Vertex_index va : m_.vertices() )
		for ( Mesh::Vertex_index vb : b.m_.vertices() ) {
			double d2 = dist2(m_.point(va), b.m_.point(vb));
			if ( d2 > best ) { best = d2; pt_to(m_.point(va), pa); pt_to(b.m_.point(vb), pb); }
		}
		return ( best < 0 ) ? 0.0 : std::sqrt(best);
	}
	double best = -1.0;
	{   /* A の各頂点 → B 表面 */
		CgTree tree(faces(b.m_).first, faces(b.m_).second, b.m_);
		for ( Mesh::Vertex_index va : m_.vertices() ) {
			Point_3 q = m_.point(va);
			Point_3 cp = tree.closest_point(q);
			double d2 = dist2(q, cp);
			if ( best < 0 || d2 < best ) { best = d2; pt_to(q, pa); pt_to(cp, pb); }
		}
	}
	{   /* B の各頂点 → A 表面(辺-辺谷の近似改善・対称化) */
		CgTree tree(faces(m_).first, faces(m_).second, m_);
		for ( Mesh::Vertex_index vb : b.m_.vertices() ) {
			Point_3 q = b.m_.point(vb);
			Point_3 cp = tree.closest_point(q);
			double d2 = dist2(q, cp);
			if ( best < 0 || d2 < best ) { best = d2; pt_to(cp, pa); pt_to(q, pb); }
		}
	}
	return ( best < 0 ) ? 0.0 : std::sqrt(best);
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
	/* EPECK(m_) → EPICK(im) へ頂点/面をコピー。Surface_mesh は三角形化済み前提。 */
	SdfMesh im;
	std::map<Mesh::Vertex_index, SdfMesh::Vertex_index> vmap;
	for ( Mesh::Vertex_index v : m_.vertices() ) {
		const Point_3& p = m_.point(v);
		vmap[v] = im.add_vertex(SdfPt(
		    CGAL::to_double(p.x()), CGAL::to_double(p.y()), CGAL::to_double(p.z())));
	}
	for ( Mesh::Face_index f : m_.faces() ) {   /* 三角形化済み前提(全プリミティブが triangulate される) */
		Mesh::Halfedge_index h = m_.halfedge(f);
		im.add_face(vmap[m_.source(h)], vmap[m_.target(h)], vmap[m_.target(m_.next(h))]);
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
	bool ok = CGAL::is_closed(m_) && ! PMP::does_self_intersect(m_);
	return ok ? 1 : 0;
}

/* ---- 修復: autorefine で自己交差を幾何的に解消(交差線を実エッジ化。EPECK で厳密・常に成功)。
 *      とぐろ tube 等の自己交差を valid(=1) に持ち込む。重なる立体からのソリッド再構成
 *      (内壁除去)は Nef が要るので未対応(将来)。---- */
sPtr<cgMesh>
cgMesh3D::op_repair()
{
	namespace PMP = CGAL::Polygon_mesh_processing;
	sPtr<cgMesh3D> out = thNEW(cgMesh3D,());
	out->m_ = m_;                     /* コピーしてから in-place で精錬 */
	PMP::autorefine(out->m_);
	return out;
}

/* ---- 断面: 点 P を通り法線 N の平面で切り、2D 断面(cgMesh2D)を返す。
 * Polygon_mesh_slicer で平面との交差ポリラインを得て、面内の正規直交基底(u,v)で 2D に射影し、
 * even-odd repair で塗り領域(穴つき)に組み立てる。基底は double(任意法線は正規化に sqrt が要る=
 * 任意角回転と同じ精度方針)。退化法線/空断面は null/空。 ---- */
sPtr<cgMesh>
cgMesh3D::op_section(const double P[3], const double N[3])
{
	double nx = N[0], ny = N[1], nz = N[2];
	double nl = std::sqrt(nx*nx + ny*ny + nz*nz);
	if ( nl < 1e-12 ) return sPtr<cgMesh>();   /* 退化法線 */
	nx /= nl; ny /= nl; nz /= nl;
	/* 面内の正規直交基底 u,v(u = n に直交な軸, v = n×u)。 */
	double ax = ( std::fabs(nx) < 0.9 ) ? 1.0 : 0.0;
	double ay = ( std::fabs(nx) < 0.9 ) ? 0.0 : 1.0;
	double an = ax*nx + ay*ny;                 /* a=(ax,ay,0) */
	double ux = ax - an*nx, uy = ay - an*ny, uz = -an*nz;
	double ul = std::sqrt(ux*ux + uy*uy + uz*uz);
	ux /= ul; uy /= ul; uz /= ul;
	double vx = ny*uz - nz*uy, vy = nz*ux - nx*uz, vz = nx*uy - ny*ux;

	/* 名前付き中間変数で most vexing parse を回避(handoff の affine と同じ罠)。 */
	K::FT fp[3] = { K::FT(P[0]), K::FT(P[1]), K::FT(P[2]) };
	K::FT fn[3] = { K::FT(N[0]), K::FT(N[1]), K::FT(N[2]) };
	K::Point_3  pOnPlane(fp[0], fp[1], fp[2]);
	K::Vector_3 planeN(fn[0], fn[1], fn[2]);
	K::Plane_3  plane(pOnPlane, planeN);
	CGAL::Polygon_mesh_slicer<Mesh, K> slicer(m_);
	std::vector<std::vector<K::Point_3> > polylines;
	slicer(plane, std::back_inserter(polylines));

	CGAL::Multipolygon_with_holes_2<K> mp;
	for ( std::size_t k = 0 ; k < polylines.size() ; ++k ) {
		std::vector<K::Point_3>& pl = polylines[k];
		std::size_t n = pl.size();
		if ( n > 1 && pl.front() == pl.back() ) --n;   /* 閉ポリラインの重複終点を落とす */
		if ( n < 3 ) continue;                          /* 開いた断片はスキップ(塗りにならない) */
		CGAL::Polygon_2<K> poly;
		for ( std::size_t i = 0 ; i < n ; ++i ) {
			double qx = CGAL::to_double(pl[i].x()), qy = CGAL::to_double(pl[i].y()), qz = CGAL::to_double(pl[i].z());
			double dx = qx - P[0], dy = qy - P[1], dz = qz - P[2];
			double uu = dx*ux + dy*uy + dz*uz;
			double vv = dx*vx + dy*vy + dz*vz;
			poly.push_back(K::Point_2(K::FT(uu), K::FT(vv)));
		}
		if ( poly.size() >= 3 )
			mp.add_polygon_with_holes(CGAL::Polygon_with_holes_2<K>(poly));
	}
	/* even-odd repair: 入れ子ループを外周/穴に整理(断面の内壁=穴)。 */
	auto repaired = CGAL::Polygon_repair::repair(mp);
	sPtr<cgMesh2D> out = thNEW(cgMesh2D,());
	for ( const auto& pwh : repaired.polygons_with_holes() )
		out->regions().push_back(pwh);
	return out;
}

/* ---- 拡張子取り出し ---- */
static const char* ext_of(const char* path) {
	const char* dot = ::strrchr(path, '.');
	return dot ? dot + 1 : "";
}

/* AMF の unit 属性語へ正規化(規格値: inch/millimeter/meter/feet/micron)。
 * 別名(mm/m/in/ft/um)も許容。未指定/不明は millimeter(3D プリント既定)。 */
static const char* amf_unit_of(const char* unit)
{
	if ( unit == 0 || unit[0] == '\0' ) return "millimeter";
	if ( ::strcasecmp(unit,"mm")==0 || ::strcasecmp(unit,"millimeter")==0 ) return "millimeter";
	if ( ::strcasecmp(unit,"m")==0  || ::strcasecmp(unit,"meter")==0 )      return "meter";
	if ( ::strcasecmp(unit,"in")==0 || ::strcasecmp(unit,"inch")==0 )       return "inch";
	if ( ::strcasecmp(unit,"ft")==0 || ::strcasecmp(unit,"feet")==0 || ::strcasecmp(unit,"foot")==0 ) return "feet";
	if ( ::strcasecmp(unit,"um")==0 || ::strcasecmp(unit,"micron")==0 || ::strcasecmp(unit,"micrometer")==0 ) return "micron";
	return "millimeter";   /* 不明語は既定 mm(無効な AMF を吐かない) */
}

/* ---- AMF 書き出し(自前 XML・依存ライブラリなし)----
 * STL/OFF が持てない「単位」を unit 属性に刻めるクロスプラットフォームな出力。
 * 座標は EPECK(厳密有理)→ double(to_double)。三角形メッシュ前提(全プリミティブは三角化済み)。
 * 頂点は boolean 後の欠番を密 index へ詰め直す。 */
static bool write_amf(const char *path, const cgMesh3D::Mesh& m, const char *unit)
{
	FILE *f = ::fopen(path, "wb");
	if ( f == 0 ) return false;
	::fprintf(f, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
	::fprintf(f, "<amf unit=\"%s\" version=\"1.1\">\n", amf_unit_of(unit));
	::fprintf(f, "  <object id=\"0\">\n    <mesh>\n      <vertices>\n");
	std::map<cgMesh3D::Mesh::Vertex_index, int> id;
	int next = 0;
	for ( cgMesh3D::Mesh::Vertex_index v : m.vertices() ) {
		const cgMesh3D::Point_3& p = m.point(v);
		::fprintf(f, "        <vertex><coordinates>"
		            "<x>%.17g</x><y>%.17g</y><z>%.17g</z></coordinates></vertex>\n",
		          CGAL::to_double(p.x()), CGAL::to_double(p.y()), CGAL::to_double(p.z()));
		id[v] = next++;
	}
	::fprintf(f, "      </vertices>\n      <volume>\n");
	std::optional<cgMesh3D::Mesh::Property_map<cgMesh3D::Mesh::Face_index, CGAL::IO::Color> > fcol
	    = m.property_map<cgMesh3D::Mesh::Face_index, CGAL::IO::Color>("f:color");
	for ( cgMesh3D::Mesh::Face_index fc : m.faces() ) {
		cgMesh3D::Mesh::Halfedge_index h = m.halfedge(fc);
		::fprintf(f, "        <triangle><v1>%d</v1><v2>%d</v2><v3>%d</v3>",
		          id[m.source(h)], id[m.target(h)], id[m.target(m.next(h))]);
		if ( fcol.has_value() ) {   /* AMF の色は 0..1 の r/g/b */
			const CGAL::IO::Color& c = (*fcol)[fc];
			::fprintf(f, "<color><r>%.4g</r><g>%.4g</g><b>%.4g</b></color>",
			          c.red()/255.0, c.green()/255.0, c.blue()/255.0);
		}
		::fprintf(f, "</triangle>\n");
	}
	::fprintf(f, "      </volume>\n    </mesh>\n  </object>\n</amf>\n");
	::fclose(f);
	return true;
}

/* 3MF の単位語へ正規化(規格値: micron/millimeter/centimeter/inch/foot/meter)。
 * AMF と語彙が少し違う(3MF は foot・centimeter あり)。未指定/不明は millimeter。 */
static const char* unit_3mf_of(const char *unit)
{
	if ( unit == 0 || unit[0] == '\0' ) return "millimeter";
	if ( ::strcasecmp(unit,"mm")==0 || ::strcasecmp(unit,"millimeter")==0 ) return "millimeter";
	if ( ::strcasecmp(unit,"cm")==0 || ::strcasecmp(unit,"centimeter")==0 ) return "centimeter";
	if ( ::strcasecmp(unit,"m")==0  || ::strcasecmp(unit,"meter")==0 )      return "meter";
	if ( ::strcasecmp(unit,"in")==0 || ::strcasecmp(unit,"inch")==0 )       return "inch";
	if ( ::strcasecmp(unit,"ft")==0 || ::strcasecmp(unit,"feet")==0 || ::strcasecmp(unit,"foot")==0 ) return "foot";
	if ( ::strcasecmp(unit,"um")==0 || ::strcasecmp(unit,"micron")==0 || ::strcasecmp(unit,"micrometer")==0 ) return "micron";
	return "millimeter";
}

/* ---- 最小 ZIP(STORE=無圧縮)ライタ。3MF は OPC=zip なので外部ライブラリ不要 ----
 * CRC32 自前・タイムスタンプ固定(1980-01-01)で**決定的出力**(同一メッシュ→同一バイト→
 * content_hash 安定でキャッシュが効く)。圧縮しないので zlib 等にも依存しない。 */
namespace {
	uint32_t crc32_of(const std::string& s) {
		static uint32_t T[256];
		static bool init = false;
		if ( ! init ) {
			for ( uint32_t i = 0 ; i < 256 ; ++i ) {
				uint32_t c = i;
				for ( int k = 0 ; k < 8 ; ++k ) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
				T[i] = c;
			}
			init = true;
		}
		uint32_t c = 0xFFFFFFFFu;
		for ( unsigned char ch : s ) c = T[(c ^ ch) & 0xFF] ^ (c >> 8);
		return c ^ 0xFFFFFFFFu;
	}
	struct ZipEntry { std::string name; std::string data; uint32_t crc; uint32_t off; };
	inline void put16(std::string& o, uint32_t v) { o.push_back(char(v & 0xFF)); o.push_back(char((v >> 8) & 0xFF)); }
	inline void put32(std::string& o, uint32_t v) {
		o.push_back(char(v & 0xFF));        o.push_back(char((v >> 8) & 0xFF));
		o.push_back(char((v >> 16) & 0xFF)); o.push_back(char((v >> 24) & 0xFF));
	}
	bool write_zip_store(const char *path, std::vector<ZipEntry>& es) {
		std::string out;
		for ( size_t i = 0 ; i < es.size() ; ++i ) {
			ZipEntry& e = es[i];
			e.crc = crc32_of(e.data);
			e.off = (uint32_t)out.size();
			put32(out, 0x04034b50u); put16(out, 20); put16(out, 0); put16(out, 0);   /* sig/ver/flag/method=store */
			put16(out, 0); put16(out, 0x0021);                                       /* time / date=1980-01-01 */
			put32(out, e.crc); put32(out, (uint32_t)e.data.size()); put32(out, (uint32_t)e.data.size());
			put16(out, (uint32_t)e.name.size()); put16(out, 0);
			out += e.name; out += e.data;
		}
		uint32_t cdStart = (uint32_t)out.size();
		for ( size_t i = 0 ; i < es.size() ; ++i ) {
			ZipEntry& e = es[i];
			put32(out, 0x02014b50u); put16(out, 20); put16(out, 20); put16(out, 0); put16(out, 0);
			put16(out, 0); put16(out, 0x0021);
			put32(out, e.crc); put32(out, (uint32_t)e.data.size()); put32(out, (uint32_t)e.data.size());
			put16(out, (uint32_t)e.name.size()); put16(out, 0); put16(out, 0);
			put16(out, 0); put16(out, 0); put32(out, 0); put32(out, e.off);
			out += e.name;
		}
		uint32_t cdSize = (uint32_t)out.size() - cdStart;
		put32(out, 0x06054b50u); put16(out, 0); put16(out, 0);
		put16(out, (uint32_t)es.size()); put16(out, (uint32_t)es.size());
		put32(out, cdSize); put32(out, cdStart); put16(out, 0);
		FILE *f = ::fopen(path, "wb");
		if ( f == 0 ) return false;
		size_t n = ::fwrite(out.data(), 1, out.size(), f);
		::fclose(f);
		return n == out.size();
	}
}

/* ---- 3MF 書き出し(自前 XML + zip・lib3mf 不要・全環境)----
 * 3MF は OPC コンテナ(zip)に 3 つの XML パートを詰めたもの。AMF 同様に自前で組む。
 * STL/OFF と違い <model unit="..."> で単位を持てる → unit 引数を反映(既定 mm)。
 * 座標は EPECK → double(to_double)。三角形メッシュ前提・頂点は密 index へ詰め直す。 */
static bool write_3mf_file(const char *path, const cgMesh3D::Mesh& m, const char *unit)
{
	char buf[192];
	/* 面色 → パレット(一意色)。3MF Materials 拡張の colorgroup で per-triangle 着色。 */
	std::optional<cgMesh3D::Mesh::Property_map<cgMesh3D::Mesh::Face_index, CGAL::IO::Color> > fcol
	    = m.property_map<cgMesh3D::Mesh::Face_index, CGAL::IO::Color>("f:color");
	bool hasColor = fcol.has_value();
	std::vector<uint32_t> palette;          /* packed 0xRRGGBB の一意リスト */
	std::map<uint32_t, int> palIdx;
	if ( hasColor )
		for ( cgMesh3D::Mesh::Face_index fc : m.faces() ) {
			const CGAL::IO::Color& c = (*fcol)[fc];
			uint32_t k = ((uint32_t)c.red() << 16) | ((uint32_t)c.green() << 8) | (uint32_t)c.blue();
			if ( palIdx.find(k) == palIdx.end() ) { palIdx[k] = (int)palette.size(); palette.push_back(k); }
		}

	std::string model;
	model += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
	model += "<model unit=\"";  model += unit_3mf_of(unit);
	model += "\" xml:lang=\"en-US\" xmlns=\"http://schemas.microsoft.com/3dmanufacturing/core/2015/02\"";
	if ( hasColor )
		model += " xmlns:m=\"http://schemas.microsoft.com/3dmanufacturing/material/2015/02\"";
	model += ">\n <resources>\n";
	if ( hasColor ) {
		model += "  <m:colorgroup id=\"2\">\n";
		for ( std::size_t i = 0 ; i < palette.size() ; ++i ) {
			::snprintf(buf, sizeof buf, "   <m:color color=\"#%02X%02X%02XFF\"/>\n",
			          (palette[i] >> 16) & 0xff, (palette[i] >> 8) & 0xff, palette[i] & 0xff);
			model += buf;
		}
		model += "  </m:colorgroup>\n";
	}
	model += "  <object id=\"1\" type=\"model\">\n   <mesh>\n    <vertices>\n";
	std::map<cgMesh3D::Mesh::Vertex_index, int> id;
	int next = 0;
	for ( cgMesh3D::Mesh::Vertex_index v : m.vertices() ) {
		const cgMesh3D::Point_3& p = m.point(v);
		::snprintf(buf, sizeof buf, "     <vertex x=\"%.17g\" y=\"%.17g\" z=\"%.17g\"/>\n",
		          CGAL::to_double(p.x()), CGAL::to_double(p.y()), CGAL::to_double(p.z()));
		model += buf;
		id[v] = next++;
	}
	model += "    </vertices>\n    <triangles>\n";
	for ( cgMesh3D::Mesh::Face_index fc : m.faces() ) {
		cgMesh3D::Mesh::Halfedge_index h = m.halfedge(fc);
		int v1 = id[m.source(h)], v2 = id[m.target(h)], v3 = id[m.target(m.next(h))];
		if ( hasColor ) {
			const CGAL::IO::Color& c = (*fcol)[fc];
			uint32_t k = ((uint32_t)c.red() << 16) | ((uint32_t)c.green() << 8) | (uint32_t)c.blue();
			::snprintf(buf, sizeof buf, "     <triangle v1=\"%d\" v2=\"%d\" v3=\"%d\" pid=\"2\" p1=\"%d\"/>\n",
			          v1, v2, v3, palIdx[k]);
		} else {
			::snprintf(buf, sizeof buf, "     <triangle v1=\"%d\" v2=\"%d\" v3=\"%d\"/>\n", v1, v2, v3);
		}
		model += buf;
	}
	model += "    </triangles>\n   </mesh>\n  </object>\n </resources>\n"
	         " <build>\n  <item objectid=\"1\"/>\n </build>\n</model>\n";

	static const char *CONTENT_TYPES =
	    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
	    "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
	    "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
	    "<Default Extension=\"model\" ContentType=\"application/vnd.ms-package.3dmanufacturing-3dmodel+xml\"/>"
	    "</Types>";
	static const char *RELS =
	    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
	    "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
	    "<Relationship Target=\"/3D/3dmodel.model\" Id=\"rel0\" "
	    "Type=\"http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel\"/>"
	    "</Relationships>";

	std::vector<ZipEntry> es;
	es.push_back((ZipEntry){ std::string("[Content_Types].xml"), std::string(CONTENT_TYPES), 0, 0 });
	es.push_back((ZipEntry){ std::string("_rels/.rels"),         std::string(RELS),          0, 0 });
	es.push_back((ZipEntry){ std::string("3D/3dmodel.model"),    model,                      0, 0 });
	return write_zip_store(path, es);
}

/* ---- ファイル書き出し(拡張子で振り分け)----
 *   AMF = 自前 XML / 3MF = 自前 XML+zip(どちらも単位つき・全環境・依存なし)。
 *   その他 = OFF/STL/OBJ/PLY(CGAL・単位概念なしで unit 無視)。 */
bool
cgMesh3D::write_to(const char *path, const char *unit)
{
	const char *e = ext_of(path);
	if ( ::strcasecmp(e, "amf") == 0 ) return write_amf(path, m_, unit);
	if ( ::strcasecmp(e, "3mf") == 0 ) return write_3mf_file(path, m_, unit);
	/* OFF/PLY は面色(f:color)があれば COFF / 色つき PLY で出す(STL/OBJ は face_color_map を無視)。 */
	std::optional<Mesh::Property_map<Mesh::Face_index, CGAL::IO::Color> > fc
	    = m_.property_map<Mesh::Face_index, CGAL::IO::Color>("f:color");
	if ( fc.has_value() )
		return CGAL::IO::write_polygon_mesh(std::string(path), m_,
		           CGAL::parameters::face_color_map(*fc));
	return CGAL::IO::write_polygon_mesh(std::string(path), m_);
}

/* ---- reader 用ファクトリ: D_META タグ → 具体型 ---- */
sPtr<cgMesh>
cgMesh::create_for_meta(const uint8_t *meta, int len)
{
	if ( len == 4 && meta[0]=='M' && meta[1]=='E' && meta[2]=='S' && meta[3]=='H' )
		return thNEW(cgMesh3D,());
	if ( len == 4 && meta[0]=='P' && meta[1]=='L' && meta[2]=='Y' && meta[3]=='2' )
		return thNEW(cgMesh2D,());
	return sPtr<cgMesh>();
}
