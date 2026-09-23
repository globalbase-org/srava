/*
 * cgEstimateNormals — 点群の法線推定 (PCA + MST 向き付け) の **CGAL 本体**。
 *
 * ★★ #3545 段 2: cgaEstimateNormals.cpp (op) から引き取った。
 *   ⚠⚠ この op は cgal の中で **唯一 EPICK (Exact_predicates_inexact_constructions_kernel) を
 *     直に使う**ところで、点集合処理 (pca_estimate_normals / mst_orient_normals) は
 *     CGAL の述語・アサーションを通る = @c _error_handler / @c _error_behaviour の実体を
 *     引き込む。@c CGAL::to_double も @c relative_precision_of_to_double (可変な関数内 static)
 *     を読むので、op 側に置くと cgal.so にもコピーができる。
 *   ⇒ 入口を **素の double 配列**にして、CGAL はこの TU (libsrava_cg) に閉じ込める。
 *
 * ⚠ 向き付けの方針は op から動かしていない: 向き付かなかった点が 1 つでも残ったら
 *   **点は残したまま印 (oriented) だけ下ろす** (CGAL の作法である erase はしない —
 *   黙って点が減るため)。理由の全文は cgaEstimateNormals.cpp の冒頭。
 */
#include	"cg/c++/cgMesh.h"

#include	<CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include	<CGAL/property_map.h>
#include	<CGAL/pca_estimate_normals.h>
#include	<CGAL/mst_orient_normals.h>
#include	<CGAL/number_utils.h>

#include	<vector>
#include	<utility>
#include	<exception>
#include	<stdio.h>

int
cg_estimate_normals(const double *xyz, int np, int k,
                    std::vector<double>& outXyz, std::vector<double>& outNrm,
                    int *oriented, char *err, int errsz)
{
	typedef CGAL::Exact_predicates_inexact_constructions_kernel	Kd;
	typedef Kd::Point_3						Point;
	typedef Kd::Vector_3						Vector;
	typedef std::pair<Point,Vector>					PwN;

	std::vector<PwN> pts;
	pts.reserve((size_t)np);
	for ( int i = 0 ; i < np ; ++i )
		pts.push_back(std::make_pair(
		    Point(xyz[(size_t)i*3], xyz[(size_t)i*3+1], xyz[(size_t)i*3+2]),
		    Vector(0, 0, 0)));

	int ori = 0;
	try {
		CGAL::pca_estimate_normals<CGAL::Sequential_tag>(
		    pts, (unsigned int)k,
		    CGAL::parameters::point_map(CGAL::First_of_pair_property_map<PwN>())
		                     .normal_map(CGAL::Second_of_pair_property_map<PwN>()));
		std::vector<PwN>::iterator un = CGAL::mst_orient_normals(
		    pts, (unsigned int)k,
		    CGAL::parameters::point_map(CGAL::First_of_pair_property_map<PwN>())
		                     .normal_map(CGAL::Second_of_pair_property_map<PwN>()));
		ori = ( un == pts.end() ) ? 1 : 0;
	} catch ( const std::exception &e ) {
		if ( err != 0 && errsz > 0 ) ::snprintf(err, (size_t)errsz, "%s", e.what());
		return 1;
	} catch ( ... ) {
		if ( err != 0 && errsz > 0 ) ::snprintf(err, (size_t)errsz, "%s", "unknown error");
		return 1;
	}

	outXyz.reserve((size_t)np * 3);
	outNrm.reserve((size_t)np * 3);
	for ( int i = 0 ; i < np ; ++i ) {
		const Point  &p = pts[(size_t)i].first;
		const Vector &v = pts[(size_t)i].second;
		outXyz.push_back(CGAL::to_double(p.x()));
		outXyz.push_back(CGAL::to_double(p.y()));
		outXyz.push_back(CGAL::to_double(p.z()));
		outNrm.push_back(CGAL::to_double(v.x()));
		outNrm.push_back(CGAL::to_double(v.y()));
		outNrm.push_back(CGAL::to_double(v.z()));
	}
	if ( oriented != 0 ) *oriented = ori;
	return 0;
}
