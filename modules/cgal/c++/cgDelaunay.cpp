/*
 * cgDelaunay — delaunay(p) の計算本体 (#3525)。cgal 版・2D。
 *
 * ★★ **voronoi とは別の計算**であって前段ではない。Voronoi セルは垂直二等分線の
 *   半平面の共通部分として直接作れる (cgVoronoi.cpp) ので、こちらは *三角形分割そのものが
 *   欲しい人のための独立した op*。⇒ 片方を直しても もう片方の値は動かない。
 *
 * ★ 出力は **1 つの 2D 値の中に三角形を片として並べた**もの。⇒ @nparts@ で数えて
 *   @part(d,i)@ で 1 枚ずつ取り出せる (voronoi と同じ形)。
 *
 * ⚠⚠ **索引の意味が voronoi とは違う** (#3527)。voronoi のセル番号は *定義で決まる*
 *   (サイト i のセル) が、三角形の番号は **実装依存** — CGAL の面の走査順で、上流の版や
 *   点の挿入順で変わりうる。★ しかも値はキャッシュに焼き付くので、版が変われば
 *   *同じ式が別の片を返しても値としては正常に見える*。
 *   ⇒ 「i 番目の三角形」を約束として使ってはいけない。数える / 全部回す のは安全。
 *
 * ⚠ 退化 (全部が 1 直線上) は **明示エラー**。CGAL は次元が 1 に落ちて有限面を 1 枚も
 *   返さないので、黙って空の値を返すと「面積 0 の 2D」が下流へ流れる。
 */
#include	"cg/c++/cgMeshCgal.h"
#include	"pt/c++/ptCloud.h"

#include	<CGAL/Delaunay_triangulation_2.h>
#include	<CGAL/Delaunay_triangulation_3.h>
#include	<vector>
#include	<stdio.h>
#include	<stdarg.h>

/* ⚠⚠ **理由をモジュール大域 (static) に置かない** (ひさ指示 2026-08-26・@010c39f@)。
 *   モジュールは in-proc (EXEC_THREAD) で走りうるので、1 プロセスに複数 op が同居する。
 *   可変な static はその op どうしで **混線する**。⇒ **呼び手のバッファへ書く**。
 * ★ 初版 (#3525) は @static char msgbuf[]@ に溜めていて @srava_no_mutable_static@ が
 *   赤くなった (2026-09-16・合流時に検出)。同じ規約を 2 度立てないよう理由をここに残す。
 * ⚠ err==0 なら理由は捨てる (呼び手が要らない場合)。 */
static void
cg_say(char *err, int errsz, const char *fmt, ...)
{
	if ( err == 0 || errsz <= 0 ) return;
	va_list ap;
	va_start(ap, fmt);
	::vsnprintf(err, (size_t)errsz, fmt, ap);
	va_end(ap);
}

sPtr<cgMesh2D>
cg_delaunay_2d(sPtr<ptCloud> cloud, char *err, int errsz)
{
	typedef CGAL::Delaunay_triangulation_2<K> DT;

	if ( ! cloud.is_notNull() || cloud->dim() != 2 ) {
		cg_say(err, errsz, "needs a 2D point cloud (pt-cloud2d)");
		return sPtr<cgMesh2D>();
	}
	const int np = cloud->np();
	if ( np < 3 ) {
		cg_say(err, errsz, "needs at least 3 points");
		return sPtr<cgMesh2D>();
	}
	const std::vector<double>& X = cloud->xyz();
	std::vector<K::Point_2> P;
	P.reserve((size_t)np);
	for ( int i = 0 ; i < np ; ++i )
		P.push_back(K::Point_2(K::FT(X[(size_t)i*2]), K::FT(X[(size_t)i*2+1])));

	DT dt;
	dt.insert(P.begin(), P.end());
	/* ⚠ 「面が 0 枚 = 失敗」と読む前に **次元**を見る。1 直線上なら dimension() は 1 で、
	 *   有限面の走査は空レンジを返す (正しい値を誤診しない・#3525 の下調べ)。 */
	if ( dt.dimension() < 2 ) {
		cg_say(err, errsz, "the points are degenerate (all on one line), so there is no triangulation");
		return sPtr<cgMesh2D>();
	}
	sPtr<cgMesh2D> out = thNEW(cgMesh2D,());
	for ( DT::Finite_faces_iterator f = dt.finite_faces_begin() ; f != dt.finite_faces_end() ; ++f ) {
		Polygon_2 t;
		t.push_back(f->vertex(0)->point());
		t.push_back(f->vertex(1)->point());
		t.push_back(f->vertex(2)->point());
		if ( t.is_clockwise_oriented() ) t.reverse_orientation();
		cg_regions(out).push_back(Pwh_2(t));
	}
	if ( cg_regions(out).empty() ) {
		cg_say(err, errsz, "no triangle came out of %d point(s)", np);
		return sPtr<cgMesh2D>();
	}
	return out;
}


/* ==================================================================================
 * 3D — 四面体分割。
 *
 * ★★ チケット (#3525 の 5 節) が 3D を見送る材料に挙げていた 4 つのうち、
 *   *「四面体は srava に無い新しい表現クラス」* は **消えた** — 順序つき片リスト
 *   (cgMesh3D::parts()) が入ったので、四面体は *1 つのメッシュの片として並べる*だけで済む。
 *   残るのは規模の話 (N サイトで約 6N 個)。⇒ 型・codec・計測・op 群を新設しなくてよい。
 *
 * ⚠⚠ **四面体どうしで頂点を共有しない**。共有すると隣り合う 2 つが同じ三角形を逆向きに
 *   足すことになり、Surface_mesh の半辺構造が非多様体になって @add_face@ が黙って失敗する。
 *   ⇒ 片ごとに独立の頂点を持つ (voronoi の 3D と同じ作法)。
 *
 * ★ **同一平面の入力はエラーにしない** (#3525 が明記している判断)。次元が 2 に落ちたときは
 *   *正しい三角形分割が存在する* ので、それを平たい片として返す。⚠ 片は閉じていないので
 *   @valid@ は 0・@volume@ は 0 — それが本当の答えであって、失敗ではない。
 *   ⚠ 「四面体が 0 個だから失敗」と読むと **正しく構築された値を誤診する** (Triangulation_3.h:1801
 *     が次元 3 未満で空レンジを返すため)。⇒ 数を見る前に **dimension() を見る**。
 * ================================================================================== */
sPtr<cgMesh3D>
cg_delaunay_3d(sPtr<ptCloud> cloud, char *err, int errsz)
{
	typedef Mesh::Vertex_index VI;
	typedef CGAL::Delaunay_triangulation_3<K> DT3;

	if ( ! cloud.is_notNull() || cloud->dim() != 3 ) {
		cg_say(err, errsz, "needs a 3D point cloud (pt-cloud3d)");
		return sPtr<cgMesh3D>();
	}
	const int np = cloud->np();
	if ( np < 3 ) { cg_say(err, errsz, "needs at least 3 points"); return sPtr<cgMesh3D>(); }
	const std::vector<double>& X = cloud->xyz();
	std::vector<K::Point_3> P;
	P.reserve((size_t)np);
	for ( int i = 0 ; i < np ; ++i )
		P.push_back(K::Point_3(K::FT(X[(size_t)i*3]), K::FT(X[(size_t)i*3+1]), K::FT(X[(size_t)i*3+2])));

	DT3 dt;
	dt.insert(P.begin(), P.end());
	const int dim = dt.dimension();
	if ( dim < 2 ) {
		cg_say(err, errsz, "the points are degenerate (all on one line or a single point), "
		          "so there is no triangulation");
		return sPtr<cgMesh3D>();
	}

	sPtr<cgMesh3D> out = thNEW(cgMesh3D,());
	Mesh& acc = cg_mesh(out);
	std::vector<uint32_t>& pend = out->parts();

	if ( dim == 2 ) {
		/* ★ 同一平面 — 正しい答えは **平たい三角形の分割**。片 1 つ = 三角形 1 枚。 */
		for ( DT3::Finite_facets_iterator f = dt.finite_facets_begin() ;
		      f != dt.finite_facets_end() ; ++f ) {
			const DT3::Cell_handle c = f->first;
			const int k = f->second;
			VI v[3];
			int n = 0;
			for ( int t = 0 ; t < 4 ; ++t ) {
				if ( t == k ) continue;
				v[n++] = acc.add_vertex(c->vertex(t)->point());
			}
			acc.add_face(v[0], v[1], v[2]);
			pend.push_back((uint32_t)acc.number_of_faces());
		}
	} else {
		for ( DT3::Finite_cells_iterator c = dt.finite_cells_begin() ;
		      c != dt.finite_cells_end() ; ++c ) {
			VI v[4];
			for ( int t = 0 ; t < 4 ; ++t )
				v[t] = acc.add_vertex(c->vertex(t)->point());
			/* ★ 外向きに揃える。CGAL の有限セルは @orientation(p0,p1,p2,p3)@ が POSITIVE
			 *   (正の向き) なので、面の巻きはこの並びから決まる。負なら 2 頂点を入れ替える。 */
			if ( CGAL::orientation(c->vertex(0)->point(), c->vertex(1)->point(),
			                       c->vertex(2)->point(), c->vertex(3)->point()) != CGAL::POSITIVE ) {
				VI t = v[0]; v[0] = v[1]; v[1] = t;
			}
			acc.add_face(v[0], v[2], v[1]);
			acc.add_face(v[0], v[1], v[3]);
			acc.add_face(v[1], v[2], v[3]);
			acc.add_face(v[2], v[0], v[3]);
			pend.push_back((uint32_t)acc.number_of_faces());
		}
	}
	if ( pend.empty() ) {
		cg_say(err, errsz, "no cell came out of %d point(s)", np);
		return sPtr<cgMesh3D>();
	}
	return out;
}
