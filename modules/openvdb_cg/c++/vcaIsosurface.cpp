/*
 * vcaIsosurface — isosurface(vd-grid3d, iso) の cgal 版 (#3434・openvdb_cg.so)。
 * ★ **本物の cgMesh3D を返す**。float → EPECK の有理数は **無損失** (double は有理数で厳密に表せる)。
 * ⚠⚠ CGAL をリンクする = **GPL**。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"vd/c++/vdGrid.h"
#include	"vd/c++/vdArena.h"   /* ★ #3441: op あたりの TBB 予算 */
#include	"cg/c++/cgMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/vcaIsosurface_.h"


#include	<openvdb/tools/VolumeToMesh.h>
#include	<vector>

CLASS_TINYSTATE(vc/c++/vcaIsosurface,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	vcaIsosurface_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<cgMesh3D>	out;
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


vcaIsosurface_::vcaIsosurface_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
vcaIsosurface_::compute()
{
	/* ★ #3441: op 内並列 (TBB) は **op あたり**の予算で走らせる。予算未指定なら素通し。
	 *   ⚠ 包み忘れるとその op だけ無制限になるので、compute() 単位で一律に包む。 */
	vd_in_arena([&]{
	vdGrid::ensure_init();
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<vdGrid> in = ( na > 0 ) ? sPtr<vdGrid>::d_cast((*args)[0]) : sPtr<vdGrid>();
	if ( ! in.is_notNull() || ! in->grid() ) {
		result = thNEW(pigDataError,(thNEW(stdString,("isosurface: needs an openvdb grid"))));
		return;
	}
	double iso = ( na > 1 ) ? (*args)[1]->get_flt() : 0.0;

	/* ★ 三角形が欲しいときは **四角形版を呼んで自分で分割する**のが上流の推奨
	 *   (VolumeToMesh.h: "Do not use this method just to get a triangle mesh - use the above
	 *    method and post process the quad index list")。adaptivity 版は適応的メッシュ用。 */
	std::vector<openvdb::Vec3s> points;
	std::vector<openvdb::Vec4I> quads;
	openvdb::tools::volumeToMesh(*in->grid(), points, quads, iso);
	if ( points.empty() || quads.empty() ) {
		result = thNEW(pigDataError,(thNEW(stdString,
		    ("isosurface: empty surface (isovalue outside the narrow band?)"))));
		return;
	}

	/* ★ 本物の Manifold を組み立てて返す (MeshGL64 → Manifold)。 */
	std::vector<double>   v;
	std::vector<uint32_t> t;
	v.reserve(points.size() * 3);
	for ( size_t i = 0 ; i < points.size() ; ++i ) {
		v.push_back((double)points[i][0]);
		v.push_back((double)points[i][1]);
		v.push_back((double)points[i][2]);
	}
	/* ★ 四角形 → 2 三角形。OpenVDB の quad は (0,1,2,3) の巡回で、**外向きが逆**なので
	 *   反転して積む (そのまま積むと体積が負になる = 内向き。実測で確認)。 */
	t.reserve(quads.size() * 6);
	for ( size_t i = 0 ; i < quads.size() ; ++i ) {
		const openvdb::Vec4I &q = quads[i];
		t.push_back(q[0]); t.push_back(q[2]); t.push_back(q[1]);
		t.push_back(q[0]); t.push_back(q[3]); t.push_back(q[2]);
	}

	/* ★ 頂点/三角形 → CGAL Surface_mesh。double → EPECK は無損失。 */
	typedef cgMesh3D::Mesh Mesh;
	out = thNEW(cgMesh3D,());
	Mesh &m = out->mesh();
	std::vector<Mesh::Vertex_index> vi;
	vi.reserve(v.size() / 3);
	for ( size_t i = 0 ; i + 2 < v.size() ; i += 3 )
		vi.push_back(m.add_vertex(Mesh::Point(v[i], v[i+1], v[i+2])));
	for ( size_t i = 0 ; i + 2 < t.size() ; i += 3 )
		m.add_face(vi[t[i]], vi[t[i+1]], vi[t[i+2]]);
	});
}

sPtr<pigData>
vcaIsosurface_::get_result()
{
	return ( result != thNULL ) ? result : out;
}
