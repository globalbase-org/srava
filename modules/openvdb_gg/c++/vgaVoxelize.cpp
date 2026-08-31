/*
 * vgaVoxelize — voxelize(gg-mesh3d, dx) の計算本体 (#3434・openvdb_gg.so)。
 * ★ **本物の ggMesh を受け取る** (vmaVoxelize の geogram 版)。ライセンスは BSD-3 + Apache-2.0。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"vd/c++/vdGrid.h"
#include	"vd/c++/vdArena.h"   /* ★ #3441: op あたりの TBB 予算 */
#include	"gg/c++/ggMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/vgaVoxelize_.h"
#include	<geogram/mesh/mesh.h>

#include	<openvdb/tools/MeshToVolume.h>
#include	<vector>

CLASS_TINYSTATE(vg/c++/vgaVoxelize,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	vgaVoxelize_(
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


vgaVoxelize_::vgaVoxelize_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
vgaVoxelize_::compute()
{
	/* ★ #3441: op 内並列 (TBB) は **op あたり**の予算で走らせる。予算未指定なら素通し。
	 *   ⚠ 包み忘れるとその op だけ無制限になるので、compute() 単位で一律に包む。 */
	vd_in_arena([&]{
	vdGrid::ensure_init();
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<ggMesh> in = ( na > 0 ) ? sPtr<ggMesh>::d_cast((*args)[0]) : sPtr<ggMesh>();
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
	/* ★ 本物の GEO::Mesh から抽出する。 */
	GEO::Mesh &m = in->mesh();
	std::vector<openvdb::Vec3s> points;
	points.reserve(m.vertices.nb());
	for ( GEO::index_t v = 0 ; v < m.vertices.nb() ; ++v ) {
		const GEO::vec3 &p = m.vertices.point(v);
		points.push_back(openvdb::Vec3s((float)p.x, (float)p.y, (float)p.z));
	}
	std::vector<openvdb::Vec3I> tris;
	tris.reserve(m.facets.nb());
	for ( GEO::index_t f = 0 ; f < m.facets.nb() ; ++f ) {
		if ( m.facets.nb_vertices(f) != 3 ) continue;   /* 三角形以外は落とす */
		tris.push_back(openvdb::Vec3I((uint32_t)m.facets.vertex(f,0),
		                              (uint32_t)m.facets.vertex(f,1),
		                              (uint32_t)m.facets.vertex(f,2)));
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
vgaVoxelize_::get_result()
{
	return ( result != thNULL ) ? result : out;
}
