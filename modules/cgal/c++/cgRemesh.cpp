/*
 * cgRemesh — refine(m,len) / remesh(m,len[,iter[,sharp]]) / simplify(m,n) の計算本体 (#3512)。cgal 版。
 *
 * 3 つとも **三角形の張り方を作り直す** op で、ブールのように「同じ集合を別の表現で」
 * 返すものではない。狙いが違うので並べて置いてある:
 *
 *     refine    形は **厳密に** 不変のまま面密度だけ上げる (三角形の形も変えない)
 *     remesh    面数はむしろ増えるが **三角形の形を良くする** (辺長を揃える)
 *     simplify  形はできるだけ保ったまま **面数だけ落とす**
 *
 * ★ refine だけは **EPECK のまま**やる (下の 2 つと違って新しい頂点位置が有理数の重心座標で
 *   書けるため)。⇒ 体積が **有理数として厳密に一致**する (実測で box の体積が == 1)。
 *
 * ★★ **remesh と simplify は EPICK (double) のコピー上で解く**。理由は 2 つあって、片方は原理的:
 *
 *   ① 原理 — この 2 つは **どこにも無かった頂点位置を決める** op で、最適な位置は一般に代数的数
 *     (LindstromTurk は体積保存の線形系を解き、remesh の緩和は重心へ寄せて面へ射影する)。
 *     EPECK の有理数で「正しい位置」を表せるわけではないので、厳密性は最初から無い。
 *   ② 実務 — CGAL の実装がそもそも浮動小数前提。`LindstromTurk_cost` は `sqrt` を要求し、
 *     EPECK の FT (Lazy_exact_nt<Gmpq>) は `Algebraic_structure_traits::Sqrt = Null_functor`
 *     なので **コンパイルすら通らない**。`isotropic_remeshing` は EPECK でも通るが、
 *     単位箱 (len=0.25・3 反復) で **9020 ms 対 1.7 ms** = 5000 倍遅く、しかも結果が違った
 *     (体積 0.955 対 0.999)。⇒ 実用にならない (2026-09-12 実測)。
 *
 *   ⚠ 戻しは **無損失**。double は 2 進有理数なので EPECK へは厳密に入る。落ちるのは
 *     「入力が厳密だったこと」だけで、出力は普通の cg-mesh3d として以後も厳密に扱われる。
 *     (同じ作法は op_thin_spots が既にレイ投射で採っている。)
 *
 * ★ remesh は **鋭角エッジを既定で保護する**。無保護だと単位箱が体積 0.999107 / 面積 5.98811 に
 *   なり (角が削れる)、**黙って形が変わる**。二面角が sharp° を超えるエッジを拘束して
 *   から回すと箱は体積 1 / 面積 6 ちょうどのまま残る (実測)。
 *   ⚠ CGAL は「拘束エッジが 4/3·len より長い」と protect_constraints を拒否する
 *     (precondition violation で **例外を投げて落ちる**) ので、拘束を先に len で割っておく。
 */
#include	"cg/c++/cgMeshCgal.h"
#include	"ts2/c++/stdString.h"

#include	<CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include	<CGAL/Surface_mesh.h>
#include	<CGAL/Polygon_mesh_processing/remesh.h>
#include	<CGAL/Polygon_mesh_processing/detect_features.h>
#include	<CGAL/Surface_mesh_simplification/edge_collapse.h>
#include	<CGAL/Surface_mesh_simplification/Policies/Edge_collapse/Face_count_stop_predicate.h>
#include	<CGAL/Surface_mesh_simplification/Policies/Edge_collapse/LindstromTurk_cost.h>
#include	<CGAL/Surface_mesh_simplification/Policies/Edge_collapse/LindstromTurk_placement.h>
#include	<CGAL/boost/graph/helpers.h>   /* is_triangle_mesh */
#include	<vector>
#include	<map>
#include	<math.h>

namespace PMP = CGAL::Polygon_mesh_processing;
namespace SMS = CGAL::Surface_mesh_simplification;

typedef CGAL::Exact_predicates_inexact_constructions_kernel	IK;
typedef CGAL::Surface_mesh<IK::Point_3>				IMesh;

/* ---- EPECK → EPICK (座標だけ double へ落とす。位相はそのまま) ---- */
static bool
cg_to_ik(const Mesh &src, IMesh &dst, const char **errmsg)
{
	if ( src.number_of_faces() == 0 ) { *errmsg = "the mesh has no faces"; return false; }
	if ( ! CGAL::is_triangle_mesh(src) ) {
		*errmsg = "needs a triangle mesh (this mesh has faces with more than 3 sides)";
		return false;
	}
	/* ⚠ 添字は **num_vertices()** (削除済みを含む容量) で取る。number_of_vertices() は
	 *   生きている数なので、削除跡のある mesh では添字が溢れる。 */
	std::vector<IMesh::Vertex_index> vmap(src.num_vertices());
	for ( Mesh::Vertex_index v : src.vertices() ) {
		const Point_3 &p = src.point(v);
		vmap[(std::size_t)v] = dst.add_vertex(IK::Point_3(CGAL::to_double(p.x()),
		                                                  CGAL::to_double(p.y()),
		                                                  CGAL::to_double(p.z())));
	}
	for ( Mesh::Face_index f : src.faces() ) {
		IMesh::Vertex_index t[3];
		int n = 0;
		for ( Mesh::Vertex_index v : CGAL::vertices_around_face(src.halfedge(f), src) ) {
			if ( n < 3 ) t[n] = vmap[(std::size_t)v];
			++n;
		}
		if ( n != 3 ) { *errmsg = "needs a triangle mesh"; return false; }
		dst.add_face(t[0], t[1], t[2]);
	}
	return true;
}

/* ---- EPICK → EPECK (double は 2 進有理数なので **厳密に**入る) ---- */
static void
cg_to_ek(const IMesh &src, Mesh &dst)
{
	std::vector<Mesh::Vertex_index> vmap(src.num_vertices());
	for ( IMesh::Vertex_index v : src.vertices() ) {
		const IK::Point_3 &p = src.point(v);
		vmap[(std::size_t)v] = dst.add_vertex(Point_3(p.x(), p.y(), p.z()));
	}
	for ( IMesh::Face_index f : src.faces() ) {
		Mesh::Vertex_index t[3];
		int n = 0;
		for ( IMesh::Vertex_index v : CGAL::vertices_around_face(src.halfedge(f), src) ) {
			if ( n < 3 ) t[n] = vmap[(std::size_t)v];
			++n;
		}
		if ( n == 3 ) dst.add_face(t[0], t[1], t[2]);
	}
}

/* ---- refine(m, len) ----
 *
 * 全三角形を **同じ n で相似分割**する (辺を n 等分して重心座標の格子を張る)。
 *
 * ★ 新しい頂点は A + (B-A)i/n + (C-A)j/n = **有理数の重心座標**なので EPECK のまま厳密。
 *   ⇒ 形は bit 単位で不変 (実測: 単位箱の体積が有理数として == 1 のまま)。
 * ★ 部分三角形は元の三角形と **相似**なので、最小角は **一切変わらない** (実測 45° → 45°)。
 *   remesh (質を上げる) とも simplify (質が落ちる) とも違い、*質に手を触れない*。
 * ⚠ n は **最長辺**から決めて全面に同じ値を使う。辺ごとに n を変えると隣の面と分割数が
 *   食い違って **T 字接合**ができるため。⇒ 元から細かい面も同じ倍率で割られる
 *   (面密度を揃える用途ではそれが望ましい)。
 */
static sPtr<cgMesh>
cg_refine_impl(const Mesh &src, int n, const char **errmsg)
{
	sPtr<cgMesh3D> out = thNEW(cgMesh3D,());
	Mesh &dst = cg_mesh(out);
	typedef Mesh::Vertex_index V;
	typedef K::FT FT;

	std::map<std::size_t, V> corner;
	for ( Mesh::Vertex_index v : src.vertices() )
		corner[(std::size_t)v] = dst.add_vertex(src.point(v));
	/* 辺上の点は両隣の面で **共有**する (T 字接合を作らない)。キーは端点の組 (小さい方を先)。 */
	std::map<std::pair<std::size_t,std::size_t>, std::vector<V> > edgePts;

	for ( Mesh::Face_index f : src.faces() ) {
		V vs[3];
		int nv = 0;
		for ( Mesh::Vertex_index v : CGAL::vertices_around_face(src.halfedge(f), src) ) {
			if ( nv < 3 ) vs[nv] = v;
			++nv;
		}
		if ( nv != 3 ) { *errmsg = "needs a triangle mesh"; return sPtr<cgMesh>(); }
		const Point_3 &A = src.point(vs[0]), &B = src.point(vs[1]), &C = src.point(vs[2]);

		std::vector<std::vector<V> > g((std::size_t)n + 1);
		for ( int j = 0 ; j <= n ; ++j ) {
			g[(std::size_t)j].resize((std::size_t)(n + 1 - j));
			for ( int i = 0 ; i + j <= n ; ++i ) {
				if ( j == 0 && i == 0 ) { g[j][i] = corner[(std::size_t)vs[0]]; continue; }
				if ( j == 0 && i == n ) { g[j][i] = corner[(std::size_t)vs[1]]; continue; }
				if ( j == n && i == 0 ) { g[j][i] = corner[(std::size_t)vs[2]]; continue; }
				Point_3 p = A + (B - A) * (FT(i)/FT(n)) + (C - A) * (FT(j)/FT(n));
				int on = -1;
				std::size_t a = 0, b = 0;
				int k = 0;
				if      ( j == 0 )     { on = 1; a = (std::size_t)vs[0]; b = (std::size_t)vs[1]; k = i; }
				else if ( i == 0 )     { on = 1; a = (std::size_t)vs[0]; b = (std::size_t)vs[2]; k = j; }
				else if ( i + j == n ) { on = 1; a = (std::size_t)vs[1]; b = (std::size_t)vs[2]; k = j; }
				if ( on > 0 ) {
					int flip = ( a > b );
					std::pair<std::size_t,std::size_t> key(flip ? b : a, flip ? a : b);
					std::vector<V> &vec = edgePts[key];
					if ( vec.empty() ) vec.assign((std::size_t)n + 1, V());
					int idx = flip ? (n - k) : k;
					if ( vec[(std::size_t)idx] == V() ) vec[(std::size_t)idx] = dst.add_vertex(p);
					g[j][i] = vec[(std::size_t)idx];
				} else {
					g[j][i] = dst.add_vertex(p);
				}
			}
		}
		for ( int j = 0 ; j < n ; ++j )
			for ( int i = 0 ; i + j < n ; ++i ) {
				dst.add_face(g[j][i], g[j][i+1], g[j+1][i]);
				if ( i + j + 1 < n ) dst.add_face(g[j][i+1], g[j+1][i+1], g[j+1][i]);
			}
	}
	return out;
}

sPtr<cgMesh>
cg_refine_3d(sPtr<cgMesh3D> in, double len, const char **errmsg)
{
	if ( ! ( len > 0 ) ) { *errmsg = "the target edge length must be > 0"; return sPtr<cgMesh>(); }
	const Mesh &src = cg_mesh(in);
	if ( src.number_of_faces() == 0 ) { *errmsg = "the mesh has no faces"; return sPtr<cgMesh>(); }
	if ( ! CGAL::is_triangle_mesh(src) ) {
		*errmsg = "needs a triangle mesh (this mesh has faces with more than 3 sides)";
		return sPtr<cgMesh>();
	}
	/* 分割数は最長辺から決める。長さは √ を要するので **ここだけ** double で見る
	 * (座標そのものは触らないので、出てくる形は厳密なまま)。 */
	double maxe = 0.0;
	for ( Mesh::Edge_index e : src.edges() ) {
		Mesh::Halfedge_index h = src.halfedge(e);
		double d = std::sqrt(CGAL::to_double(
			CGAL::squared_distance(src.point(src.source(h)), src.point(src.target(h)))));
		if ( d > maxe ) maxe = d;
	}
	int n = (int)std::ceil(maxe / len);
	if ( n < 1 ) n = 1;
	/* ⚠ 面数は n² 倍になる。上限を置かないと len に 0 に近い値を渡しただけで
	 *   メモリを食い尽くす (n=1000 なら 100 万倍)。 */
	if ( (double)n * (double)n * (double)src.number_of_faces() > 2.0e7 ) {
		*errmsg = "the target edge length is too small for this mesh "
		          "(the result would have more than 20 million faces)";
		return sPtr<cgMesh>();
	}
	if ( n == 1 ) {   /* 既に十分細かい = そのまま返す (エラーにしない) */
		sPtr<cgMesh3D> out = thNEW(cgMesh3D,());
		cg_mesh(out) = src;
		return out;
	}
	return cg_refine_impl(src, n, errmsg);
}

/* ---- remesh(m, len[, iter[, sharp]]) ---- */
sPtr<cgMesh>
cg_remesh_3d(sPtr<cgMesh3D> in, double len, int iter, double sharp, const char **errmsg)
{
	if ( ! ( len > 0 ) )  { *errmsg = "the target edge length must be > 0"; return sPtr<cgMesh>(); }
	if ( iter < 1 )       { *errmsg = "the iteration count must be >= 1"; return sPtr<cgMesh>(); }

	IMesh m;
	if ( ! cg_to_ik(cg_mesh(in), m, errmsg) ) return sPtr<cgMesh>();

	try {
		/* ★ 鋭角エッジ (二面角が sharp° 超) を拘束する。sharp を 180 以上にすると
		 *   1 本も拘束されない = 素の等方リメッシュ (角が削れるかわり一様さは上がる)。 */
		IMesh::Property_map<IMesh::Edge_index,bool> ecm =
			m.add_property_map<IMesh::Edge_index,bool>("e:cg_cst", false).first;
		std::vector<IMesh::Edge_index> cst;
		if ( sharp < 180.0 ) {
			PMP::detect_sharp_edges(m, sharp, ecm);
			for ( IMesh::Edge_index e : m.edges() ) if ( get(ecm, e) ) cst.push_back(e);
			/* ⚠ 拘束は先に len まで割っておく (これが無いと CGAL が precondition で落ちる)。 */
			if ( ! cst.empty() )
				PMP::split_long_edges(cst, len, m, CGAL::parameters::edge_is_constrained_map(ecm));
		}
		PMP::isotropic_remeshing(faces(m), len, m,
			CGAL::parameters::number_of_iterations(iter)
			                 .edge_is_constrained_map(ecm)
			                 .protect_constraints(! cst.empty()));
	} catch ( const std::exception &e ) {
		/* CGAL の precondition 違反も std::exception で飛んでくる (CGAL_NO_ASSERTIONS でない限り)。 */
		*errmsg = "CGAL could not remesh this mesh";
		(void)e;
		return sPtr<cgMesh>();
	}
	if ( m.number_of_faces() == 0 ) { *errmsg = "remeshing produced an empty mesh"; return sPtr<cgMesh>(); }

	sPtr<cgMesh3D> out = thNEW(cgMesh3D,());
	cg_to_ek(m, cg_mesh(out));
	return out;
}

/* ---- simplify(m, nfaces) ---- */
sPtr<cgMesh>
cg_simplify_3d(sPtr<cgMesh3D> in, int nfaces, const char **errmsg)
{
	/* ★ 4 面未満の閉じた三角形メッシュは存在しない (最小は四面体)。 */
	if ( nfaces < 4 ) { *errmsg = "the target face count must be >= 4"; return sPtr<cgMesh>(); }

	IMesh m;
	if ( ! cg_to_ik(cg_mesh(in), m, errmsg) ) return sPtr<cgMesh>();
	/* 目標がいまの面数以上なら**何もしない** (エラーにしない — 掃引で面数を振るとき、
	 * 上限側で黙って通ってくれた方が書きやすい)。 */
	if ( (std::size_t)nfaces < m.number_of_faces() ) {
		try {
			SMS::Face_count_stop_predicate<IMesh> stop((std::size_t)nfaces);
			SMS::edge_collapse(m, stop,
				CGAL::parameters::get_cost(SMS::LindstromTurk_cost<IMesh>())
				                 .get_placement(SMS::LindstromTurk_placement<IMesh>()));
		} catch ( const std::exception & ) {
			*errmsg = "CGAL could not simplify this mesh";
			return sPtr<cgMesh>();
		}
		m.collect_garbage();   /* 潰したエッジの跡を畳む (以後の添字を詰める) */
	}
	if ( m.number_of_faces() == 0 ) { *errmsg = "simplification produced an empty mesh"; return sPtr<cgMesh>(); }

	sPtr<cgMesh3D> out = thNEW(cgMesh3D,());
	cg_to_ek(m, cg_mesh(out));
	return out;
}
