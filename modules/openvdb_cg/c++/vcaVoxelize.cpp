/*
 * vcaVoxelize — voxelize(cg-mesh3d, dx) の計算本体 (#3434・openvdb_cg.so)。
 * ★★ **本物の cgMesh3D を受け取る** (vmaVoxelize の cgal 版)。
 * ⚠⚠ このモジュールは **CGAL をリンクする = GPL**。openvdb.so / openvdb_mf.so は Apache-2.0 のみ。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"vd/c++/vdGrid.h"
#include	"vd/c++/vdArena.h"   /* ★ #3441: op あたりの TBB 予算 */
#include	"cg/c++/cgMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/vcaVoxelize_.h"


#include	<openvdb/tools/MeshToVolume.h>
#include	<vector>

CLASS_TINYSTATE(vc/c++/vcaVoxelize,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	vcaVoxelize_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<vdGrid>	out;
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
class vdGrid;
TS_END_INTERFACE

#endif


vcaVoxelize_::vcaVoxelize_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
vcaVoxelize_::compute()
{
	/* ★ #3441: op 内並列 (TBB) は **op あたり**の予算で走らせる。予算未指定なら素通し。
	 *   ⚠ 包み忘れるとその op だけ無制限になるので、compute() 単位で一律に包む。 */
	vd_in_arena([&]{
	vdGrid::ensure_init();
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<cgMesh3D> in = ( na > 0 ) ? sPtr<cgMesh3D>::d_cast((*args)[0]) : sPtr<cgMesh3D>();
	if ( ! in.is_notNull() ) {
		result = thNEW(pigDataError,(thNEW(stdString,("voxelize: needs a 3D mesh"))));
		return;
	}
	double dx = ( na > 1 ) ? (*args)[1]->get_flt() : 0.0;
	if ( !(dx > 0) ) {
		result = thNEW(pigDataError,(thNEW(stdString,
		    ("voxelize: dx (voxel size in world units) must be > 0"))));
		return;
	}

	/* vdMesh (double) → OpenVDB の Vec3s (float) + Vec3I。
	 * ★ float へ落ちるが、level set の値自体が float なので追加の損失にはならない。 */
	/* ★ 本物の CGAL Surface_mesh から抽出する。EPECK の有理数 → float は情報が落ちるが、
	 *   level set の値自体が float なので追加の損失にはならない (vd 側の性質)。 */
	typedef cgMesh3D::Mesh Mesh;
	Mesh &m = in->mesh();
	std::vector<openvdb::Vec3s> points;
	std::vector<uint32_t> vidx(m.number_of_vertices() + 1, 0);
	points.reserve(m.number_of_vertices());
	uint32_t next = 0;
	for ( Mesh::Vertex_index v : m.vertices() ) {
		if ( (size_t)v >= vidx.size() ) vidx.resize((size_t)v + 1, 0);
		vidx[(size_t)v] = next++;
		points.push_back(openvdb::Vec3s((float)CGAL::to_double(m.point(v).x()),
		                                (float)CGAL::to_double(m.point(v).y()),
		                                (float)CGAL::to_double(m.point(v).z())));
	}
	std::vector<openvdb::Vec3I> tris;
	for ( Mesh::Face_index f : m.faces() ) {
		uint32_t c[3]; int n = 0;
		for ( Mesh::Vertex_index v : CGAL::vertices_around_face(m.halfedge(f), m) ) {
			if ( n < 3 ) c[n] = vidx[(size_t)v];
			++n;
		}
		if ( n == 3 ) tris.push_back(openvdb::Vec3I(c[0], c[1], c[2]));
	}
	if ( points.empty() || tris.empty() ) {
		result = thNEW(pigDataError,(thNEW(stdString,("voxelize: empty mesh"))));
		return;
	}

	openvdb::math::Transform::Ptr xform = openvdb::math::Transform::createLinearTransform(dx);
	/* ★ 帯幅は既定 (LEVEL_SET_HALF_WIDTH = 3 voxel)。offset を大きく取るときは帯が足りなく
	 *   なるので、その時は再正規化 (tools::levelSetRebuild) が要る — #3434 の offset 実装で扱う。 */
	openvdb::FloatGrid::Ptr g =
	    openvdb::tools::meshToLevelSet<openvdb::FloatGrid>(*xform, points, tris);
	if ( ! g ) {
		result = thNEW(pigDataError,(thNEW(stdString,("voxelize: meshToLevelSet failed"))));
		return;
	}
	out = thNEW(vdGrid,());
	out->set_grid(g);
	out->set_normalized(true);   /* meshToLevelSet は真の符号付き距離場を作る */
	});
}

sPtr<pigData>
vcaVoxelize_::get_result()
{
	return ( result != thNULL ) ? result : out;
}
