/*
 * cgVoronoi — voronoi(p, box) の計算本体 (#3525)。cgal 版・2D。
 *
 * ★★ **Delaunay を経由しない**。セルの定義は「垂直二等分線による半平面の共通部分」であって
 *   三角形分割とは無関係で、ここでは定義どおり **クリップ箱を半平面で削って**作る
 *   (Sutherland–Hodgman)。⇒ 外心を 1 つも計算しないので、双対で作ったときの問題
 *   (ほぼ同一直線で外心が遠方へ飛ぶ / 退化で組合せが一意でない / spatial_sort が
 *   サイト順を壊す) が **まとめて起きない**。経緯と表は Redmine #3525 の本文。
 *
 * ★★ **約束: regions()[i] は サイト i のセル**。サイトごとに独立のループで削るので
 *   *構造的に* 保たれる (並べ替えが 1 回も起きない)。⇒ part(v,i) がそのまま使える。
 *
 * ★ 計算は **EPECK (K::FT) で厳密**。半平面の係数も交点も有理数のまま出るので、
 *   「削る」道には丸めが 1 つも入らない (箱の座標だけが入力の double)。
 *
 * ⚠ クリップ箱は **省略できない** (#3525)。理由は 2 つあって、① セルは無限に伸びうるので
 *   有界な値として返すのに要る ② 条件数 — 本来の Voronoi 頂点は遠方にあるが、削る道は
 *   **箱の外の頂点をそもそも作らない**。暗黙の箱を使うと箱の大きさで面積が黙って変わる。
 *
 * ⚠⚠ **サイトは箱の中に無ければならない** (外は明示エラー)。箱の外のサイトでも箱の一部を
 *   持ちうるが、*空のセル* が出るとセルの列に「面積 0 の穴」が空き、
 *   「regions()[i] = サイト i のセル」を**空をどう表すか**という別の問題に化ける。
 *   ⇒ いまは前提として断る。★ 後から緩める側には進めるが、逆には進めない。
 */
#include	"cg/c++/cgMeshCgal.h"
#include	"pt/c++/ptCloud.h"   /* 点群は中立の libsrava_pt が持つ (#3528) */

#include	<CGAL/Polygon_mesh_processing/clip.h>
#include	<CGAL/Polygon_mesh_processing/triangulate_faces.h>
#include	<CGAL/Polygon_mesh_processing/measure.h>
#include	<CGAL/boost/graph/helpers.h>   /* is_closed */
#include	<algorithm>
#include	<map>
#include	<vector>
#include	<stdio.h>
#include	<stdarg.h>

/* ---- 凸多角形を半平面 {q : a*qx + b*qy <= c} で削る (Sutherland–Hodgman) ----
 * ★ 入力が凸なら出力も凸で、辺ごとに符号を見るだけで済む。符号 s(q) = c - a*qx - b*qy は
 *   K::FT (厳密) なので、「辺の上に載っている」場合も 0 として**正しく**扱われる。 */
static void
cg_clip_halfplane(std::vector<K::Point_2>& poly,
                  const K::FT& a, const K::FT& b, const K::FT& c)
{
	const size_t n = poly.size();
	if ( n == 0 ) return;
	std::vector<K::FT> s(n);
	for ( size_t i = 0 ; i < n ; ++i )
		s[i] = c - a * poly[i].x() - b * poly[i].y();
	std::vector<K::Point_2> out;
	out.reserve(n + 1);
	for ( size_t i = 0 ; i < n ; ++i ) {
		const size_t j = (i + 1) % n;
		const int ki = ( s[i] >= 0 ) ? 1 : 0;
		const int kj = ( s[j] >= 0 ) ? 1 : 0;
		if ( ki ) out.push_back(poly[i]);
		if ( ki != kj ) {
			/* 交点 = u + t (v-u), t = s_u / (s_u - s_v)。⚠ s_u != s_v は符号が違うことから従う。 */
			const K::FT t = s[i] / (s[i] - s[j]);
			out.push_back(K::Point_2(poly[i].x() + t * (poly[j].x() - poly[i].x()),
			                         poly[i].y() + t * (poly[j].y() - poly[i].y())));
		}
	}
	/* ⚠ 切り口がちょうど頂点を通ると、頂点そのものと交点が **同じ点として 2 つ**入る。
	 *   残すと零長辺ができて Polygon_2::is_simple() が偽になり、valid / area が壊れる
	 *   (polygon() が継ぎ目の重複を間引いているのと同じ理由)。 */
	while ( out.size() >= 2 && out.back() == out.front() ) out.pop_back();
	for ( size_t i = 0 ; i + 1 < out.size() ; ) {
		if ( out[i] == out[i+1] ) out.erase(out.begin() + (long)(i+1));
		else ++i;
	}
	poly.swap(out);
}

/* ---- セルの外接半径の 2 乗 (サイト中心) ---- */
static K::FT
cg_cell_r2(const std::vector<K::Point_2>& poly, const K::Point_2& p)
{
	K::FT r2 = 0;
	for ( size_t i = 0 ; i < poly.size() ; ++i ) {
		const K::FT dx = poly[i].x() - p.x(), dy = poly[i].y() - p.y();
		const K::FT d2 = dx * dx + dy * dy;
		if ( d2 > r2 ) r2 = d2;
	}
	return r2;
}

/* ---- voronoi(p, box) 本体 ----
 * bmin / bmax = クリップ箱の対角 2 点。返り null = errmsg にわけが入る。 */
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
cg_voronoi_2d(sPtr<ptCloud> cloud, const double bmin[2], const double bmax[2], char *err, int errsz)
{

	if ( ! cloud.is_notNull() || cloud->dim() != 2 ) {
		cg_say(err, errsz, "needs a 2D point cloud (pt-cloud2d)");
		return sPtr<cgMesh2D>();
	}
	const int np = cloud->np();
	if ( np < 1 ) { cg_say(err, errsz, "the point cloud is empty"); return sPtr<cgMesh2D>(); }
	if ( ! ( bmin[0] < bmax[0] && bmin[1] < bmax[1] ) ) {
		cg_say(err, errsz, "the clip box is degenerate (it must have a positive width and height)");
		return sPtr<cgMesh2D>();
	}
	const std::vector<double>& X = cloud->xyz();

	/* ---- サイトは箱の中に居ること (上の ⚠⚠) ---- */
	for ( int i = 0 ; i < np ; ++i ) {
		const double x = X[(size_t)i*2], y = X[(size_t)i*2+1];
		if ( x < bmin[0] || x > bmax[0] || y < bmin[1] || y > bmax[1] ) {
			cg_say(err, errsz,
			    "site %d (%g, %g) is outside the clip box; every site must lie inside it", i, x, y);
			return sPtr<cgMesh2D>();
		}
	}
	/* ---- 重複サイトは明示エラー (黙って潰すと以降の索引がずれる・#3525) ----
	 * ★ 座標は double なので重複判定は **厳密**。並べ替えるのは検査用の索引だけで、
	 *   セルを作るループは元の順序のまま (約束を壊さない)。
	 * ⚠ -0.0 は 0.0 と == で等しいので、書き方が違うだけの重複もここで捕まる。 */
	{
		std::vector<int> ord((size_t)np);
		for ( int i = 0 ; i < np ; ++i ) ord[(size_t)i] = i;
		const double *Xp = X.data();
		std::sort(ord.begin(), ord.end(), [Xp](int a, int b) {
			const double ax = Xp[(size_t)a*2], bx = Xp[(size_t)b*2];
			if ( ax != bx ) return ax < bx;
			return Xp[(size_t)a*2+1] < Xp[(size_t)b*2+1];
		});
		for ( size_t k = 1 ; k < ord.size() ; ++k ) {
			const int a = ord[k-1], b = ord[k];
			if ( X[(size_t)a*2] == X[(size_t)b*2] && X[(size_t)a*2+1] == X[(size_t)b*2+1] ) {
				const int lo = ( a < b ) ? a : b, hi = ( a < b ) ? b : a;
				cg_say(err, errsz,
				    "sites %d and %d are the same point (%g, %g); duplicate sites are not allowed "
				    "because the cells are indexed by site", lo, hi, X[(size_t)a*2], X[(size_t)a*2+1]);
				return sPtr<cgMesh2D>();
			}
		}
	}

	/* 厳密な座標を 1 度だけ作る (double は 2 進有理数なので変換は無損失)。 */
	std::vector<K::Point_2> P;
	P.reserve((size_t)np);
	for ( int i = 0 ; i < np ; ++i )
		P.push_back(K::Point_2(K::FT(X[(size_t)i*2]), K::FT(X[(size_t)i*2+1])));

	const K::FT x0(bmin[0]), y0(bmin[1]), x1(bmax[0]), y1(bmax[1]);

	sPtr<cgMesh2D> out = thNEW(cgMesh2D,());
	cg_regions(out).reserve((size_t)np);

	for ( int i = 0 ; i < np ; ++i ) {
		/* 箱から始める (CCW)。 */
		std::vector<K::Point_2> cell;
		cell.push_back(K::Point_2(x0, y0));
		cell.push_back(K::Point_2(x1, y0));
		cell.push_back(K::Point_2(x1, y1));
		cell.push_back(K::Point_2(x0, y1));
		K::FT r2 = cg_cell_r2(cell, P[(size_t)i]);

		for ( int j = 0 ; j < np ; ++j ) {
			if ( j == i ) continue;
			/* ★ 安全半径: いまのセルが サイト i 中心・半径 R に収まっているなら、
			 *   距離 2R より遠いサイトの二等分線はセルに交わらない = もう削れない。
			 *   ⚠ 判定も **厳密** (K::FT) にしてある。double で近似すると、削れるものを
			 *     取り逃がしても *セルが少し大きいだけ* で値としては正常に見えてしまう。 */
			const K::FT dx = P[(size_t)j].x() - P[(size_t)i].x();
			const K::FT dy = P[(size_t)j].y() - P[(size_t)i].y();
			const K::FT d2 = dx * dx + dy * dy;
			if ( d2 > 4 * r2 ) continue;
			/* |q-pi|^2 <= |q-pj|^2  ⇔  2(pj-pi)·q <= |pj|^2 - |pi|^2 */
			const K::FT a = 2 * dx, b = 2 * dy;
			const K::FT c = ( P[(size_t)j].x() * P[(size_t)j].x() + P[(size_t)j].y() * P[(size_t)j].y() )
			              - ( P[(size_t)i].x() * P[(size_t)i].x() + P[(size_t)i].y() * P[(size_t)i].y() );
			cg_clip_halfplane(cell, a, b, c);
			if ( cell.size() < 3 ) break;   /* ここへは来ないはず (サイトは箱の中に居る) */
			/* ⚠ **点数が同じでも形は変わりうる** (1 つ落として 1 つ足す切り方)。
			 *   ⇒ 数を見て省かず、削ったら毎回取り直す。半径が減るほど次から先が早くなる。 */
			r2 = cg_cell_r2(cell, P[(size_t)i]);
		}
		if ( cell.size() < 3 ) {
			cg_say(err, errsz, "the cell of site %d came out empty", i);
			return sPtr<cgMesh2D>();
		}
		Polygon_2 poly(cell.begin(), cell.end());
		if ( poly.is_clockwise_oriented() ) poly.reverse_orientation();
		cg_regions(out).push_back(Pwh_2(poly));
	}
	return out;
}


/* ==================================================================================
 * 3D — ★★ **これを素直に入れられることが、voronoi を delaunay から切り離した目的**
 *       (#3525 の 5 節)。危険 (四面体という新しい表現クラス・最悪 O(N^2) のセル数) は
 *       delaunay 側に閉じ込められており、切り取りで作る voronoi はその向こう側に居ない。
 *
 * ★ 2D とまったく同じ骨: 箱から始めて、サイトごとに垂直二等分 **平面** の半空間で削る。
 *   違うのは削る道具だけ (2D = 自前の Sutherland–Hodgman / 3D = @PMP::clip@)。
 *   ⚠ @PMP::clip(mesh, plane)@ は **平面の負側を残す**。⇒ サイトが負側に来るよう係数を組む。
 *
 * ★★ 索引の約束は **順序つき片リスト** (cgMesh3D::partEnd_) が持つ。面の連結成分の
 *   発見順に頼ると *実装依存の索引* (#3527) になるので、値が順序を実体として持つ形にした
 *   (ひさ判断 2026-09-15)。⇒ 2D の box().regions と同じ立て付け。
 * ================================================================================== */

/* 箱 (CCW・外向き) を 1 つの Surface_mesh として作る。 */
static void
cg_box_mesh(Mesh& m, const K::FT& x0, const K::FT& y0, const K::FT& z0,
            const K::FT& x1, const K::FT& y1, const K::FT& z1)
{
	typedef Mesh::Vertex_index VI;
	m.clear();
	VI v[8];
	v[0]=m.add_vertex(K::Point_3(x0,y0,z0)); v[1]=m.add_vertex(K::Point_3(x1,y0,z0));
	v[2]=m.add_vertex(K::Point_3(x1,y1,z0)); v[3]=m.add_vertex(K::Point_3(x0,y1,z0));
	v[4]=m.add_vertex(K::Point_3(x0,y0,z1)); v[5]=m.add_vertex(K::Point_3(x1,y0,z1));
	v[6]=m.add_vertex(K::Point_3(x1,y1,z1)); v[7]=m.add_vertex(K::Point_3(x0,y1,z1));
	m.add_face(v[0],v[3],v[2],v[1]);   /* z0 (下・外向きは -z) */
	m.add_face(v[4],v[5],v[6],v[7]);   /* z1 */
	m.add_face(v[0],v[1],v[5],v[4]);   /* y0 */
	m.add_face(v[1],v[2],v[6],v[5]);   /* x1 */
	m.add_face(v[2],v[3],v[7],v[6]);   /* y1 */
	m.add_face(v[3],v[0],v[4],v[7]);   /* x0 */
	/* ⚠⚠ **渡す前に三角形化する** (macMINI 2026-09-16・#3525)。
	 *   @PMP::clip@ の宣言は "@param tm input **triangulated** surface mesh" (clip.h:1158)。
	 *   四角面 6 枚の箱をそのまま渡すと corefinement の印の付き方が壊れ、
	 *     compute_border_edge_map → next_marked_halfedge_around_target_vertex
	 *   が **印のある稜に当たらず戻らない** = 無限ループ (Linux / CGAL 6.0.1 で ctest が 80 分停止)。
	 *   ★ Epeck なので丸めではない。前提違反だった。
	 *   ⚠⚠ **「削った後で三角形化」では効かない** — 壊れるのは *削っている最中* なので、
	 *     渡す前でなければ意味がない。初版は後段にだけ置いていた。
	 *   ⚠ mac (CGAL 6.2) は前提違反を吸収してしまい **元から緑**だった
	 *     ⇒ 緑は「壊れていない」ではなく「この版では露出しない」しか言わない。 */
	CGAL::Polygon_mesh_processing::triangulate_faces(m);
}

/* セルの外接半径の 2 乗 (サイト中心)。 */
static K::FT
cg_cell_r2_3d(const Mesh& m, const K::Point_3& p)
{
	K::FT r2 = 0;
	for ( Mesh::Vertex_index v : m.vertices() ) {
		const K::Point_3& q = m.point(v);
		const K::FT dx = q.x()-p.x(), dy = q.y()-p.y(), dz = q.z()-p.z();
		const K::FT d2 = dx*dx + dy*dy + dz*dz;
		if ( d2 > r2 ) r2 = d2;
	}
	return r2;
}

sPtr<cgMesh3D>
cg_voronoi_3d(sPtr<ptCloud> cloud, const double bmin[3], const double bmax[3], char *err, int errsz)
{
	typedef Mesh::Vertex_index VI;
	namespace PMP = CGAL::Polygon_mesh_processing;

	if ( ! cloud.is_notNull() || cloud->dim() != 3 ) {
		cg_say(err, errsz, "needs a 3D point cloud (pt-cloud3d)");
		return sPtr<cgMesh3D>();
	}
	const int np = cloud->np();
	if ( np < 1 ) { cg_say(err, errsz, "the point cloud is empty"); return sPtr<cgMesh3D>(); }
	for ( int d = 0 ; d < 3 ; ++d )
		if ( ! ( bmin[d] < bmax[d] ) ) {
			cg_say(err, errsz, "the clip box is degenerate (it must have a positive size on every axis)");
			return sPtr<cgMesh3D>();
		}
	const std::vector<double>& X = cloud->xyz();

	/* ---- サイトは箱の中に居ること (2D と同じ約束・わけはこのファイルの冒頭) ---- */
	for ( int i = 0 ; i < np ; ++i ) {
		const double x = X[(size_t)i*3], y = X[(size_t)i*3+1], z = X[(size_t)i*3+2];
		if ( x < bmin[0] || x > bmax[0] || y < bmin[1] || y > bmax[1] || z < bmin[2] || z > bmax[2] ) {
			cg_say(err, errsz,
			    "site %d (%g, %g, %g) is outside the clip box; every site must lie inside it", i, x, y, z);
			return sPtr<cgMesh3D>();
		}
	}
	/* ---- 重複サイトは明示エラー (索引がずれる) ---- */
	{
		std::vector<int> ord((size_t)np);
		for ( int i = 0 ; i < np ; ++i ) ord[(size_t)i] = i;
		const double *Xp = X.data();
		std::sort(ord.begin(), ord.end(), [Xp](int a, int b) {
			for ( int d = 0 ; d < 3 ; ++d ) {
				const double ax = Xp[(size_t)a*3+d], bx = Xp[(size_t)b*3+d];
				if ( ax != bx ) return ax < bx;
			}
			return false;
		});
		for ( size_t k = 1 ; k < ord.size() ; ++k ) {
			const int a = ord[k-1], b = ord[k];
			if ( X[(size_t)a*3] == X[(size_t)b*3] && X[(size_t)a*3+1] == X[(size_t)b*3+1]
			  && X[(size_t)a*3+2] == X[(size_t)b*3+2] ) {
				const int lo = ( a < b ) ? a : b, hi = ( a < b ) ? b : a;
				cg_say(err, errsz,
				    "sites %d and %d are the same point (%g, %g, %g); duplicate sites are not allowed "
				    "because the cells are indexed by site", lo, hi,
				    X[(size_t)a*3], X[(size_t)a*3+1], X[(size_t)a*3+2]);
				return sPtr<cgMesh3D>();
			}
		}
	}

	std::vector<K::Point_3> P;
	P.reserve((size_t)np);
	for ( int i = 0 ; i < np ; ++i )
		P.push_back(K::Point_3(K::FT(X[(size_t)i*3]), K::FT(X[(size_t)i*3+1]), K::FT(X[(size_t)i*3+2])));

	const K::FT x0(bmin[0]), y0(bmin[1]), z0(bmin[2]), x1(bmax[0]), y1(bmax[1]), z1(bmax[2]);

	sPtr<cgMesh3D> out = thNEW(cgMesh3D,());
	Mesh& acc = cg_mesh(out);
	std::vector<uint32_t>& pend = out->parts();

	for ( int i = 0 ; i < np ; ++i ) {
		Mesh cell;
		cg_box_mesh(cell, x0, y0, z0, x1, y1, z1);
		K::FT r2 = cg_cell_r2_3d(cell, P[(size_t)i]);

		for ( int j = 0 ; j < np ; ++j ) {
			if ( j == i ) continue;
			const K::FT dx = P[(size_t)j].x()-P[(size_t)i].x();
			const K::FT dy = P[(size_t)j].y()-P[(size_t)i].y();
			const K::FT dz = P[(size_t)j].z()-P[(size_t)i].z();
			const K::FT d2 = dx*dx + dy*dy + dz*dz;
			if ( d2 > 4 * r2 ) continue;          /* ★ 安全半径 (2D と同じ・厳密に判定) */
			/* |q-pi|^2 <= |q-pj|^2 ⇔ 2(pj-pi)·q - (|pj|^2-|pi|^2) <= 0
			 * ⇒ Plane_3(a,b,c,d) = a x + b y + c z + d は **サイト i で負** になる。
			 *   ⚠ PMP::clip は **負側を残す** ので、これがそのまま「サイト i の側を残す」。 */
			const K::FT a = 2*dx, b = 2*dy, c = 2*dz;
			const K::FT d = - ( ( P[(size_t)j].x()*P[(size_t)j].x() + P[(size_t)j].y()*P[(size_t)j].y()
			                    + P[(size_t)j].z()*P[(size_t)j].z() )
			                  - ( P[(size_t)i].x()*P[(size_t)i].x() + P[(size_t)i].y()*P[(size_t)i].y()
			                    + P[(size_t)i].z()*P[(size_t)i].z() ) );
			PMP::clip(cell, K::Plane_3(a, b, c, d), CGAL::parameters::clip_volume(true));
			if ( cell.number_of_faces() == 0 ) break;
			r2 = cg_cell_r2_3d(cell, P[(size_t)i]);
		}
		/* ⚠⚠ **三角形化してから積む**。箱を削ると面は四角形以上になるが、srava の 3D は
		 *   三角形メッシュで揃っている (box(1,1,1) が 12 面)。しかも @PMP::volume@ は
		 *   *三角形前提* で、四角面のまま渡すと **黙って半分**を返す
		 *   (2026-09-15 に実測: 形も bbox も valid も正しいのに体積だけ 0.5 だった)。 */
		PMP::triangulate_faces(cell);
		if ( cell.number_of_faces() == 0 || ! CGAL::is_closed(cell) ) {
			cg_say(err, errsz, "the cell of site %d did not come out as a closed solid", i);
			return sPtr<cgMesh3D>();
		}
		/* ★ acc へ **面の順序を保ったまま**積み、終端を片リストに記録する。
		 *   ⇒ 片 i = サイト i が、面の連結成分の発見順に依らず *値の構造*として決まる。 */
		std::map<VI,VI> vmap;
		for ( Mesh::Face_index f : cell.faces() ) {
			std::vector<VI> fv;
			for ( VI v : CGAL::vertices_around_face(cell.halfedge(f), cell) ) {
				std::map<VI,VI>::iterator it = vmap.find(v);
				if ( it == vmap.end() ) it = vmap.insert(std::make_pair(v, acc.add_vertex(cell.point(v)))).first;
				fv.push_back(it->second);
			}
			acc.add_face(fv);
		}
		pend.push_back((uint32_t)acc.number_of_faces());
	}
	return out;
}
