/*
 * vmaIsosurface — isosurface(vd-grid3d, iso) の計算本体 (#3434・openvdb_mf.so)。
 * ★★ **本物の mfMesh を返す** (旧実装は vdMesh という新しいクラスを作っていた・vmaVoxelize.cpp 参照)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"vd/c++/vdGrid.h"
#include	"vd/c++/vdArena.h"   /* ★ #3441: op あたりの TBB 予算 */
#include	"mf/c++/mfMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/vmaIsosurface_.h"
#include	<manifold/manifold.h>

#include	<openvdb/tools/VolumeToMesh.h>
#include	<vector>

CLASS_TINYSTATE(vm/c++/vmaIsosurface,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	vmaIsosurface_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<mfMesh>	out;
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
class mfMesh;
TS_END_INTERFACE

#endif


vmaIsosurface_::vmaIsosurface_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
vmaIsosurface_::compute()
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

	/* ★ MeshGL64 → Manifold。mfMesh::decode と同じ組み立て方 (座標一致で頂点マージされる)。 */
	manifold::MeshGL64 g;
	g.numProp = 3;
	g.vertProperties = v;
	g.triVerts.reserve(t.size());
	for ( size_t i = 0 ; i < t.size() ; ++i ) g.triVerts.push_back((uint64_t)t[i]);
	out = thNEW(mfMesh,(manifold::Manifold(g)));
	});
}

sPtr<pigData>
vmaIsosurface_::get_result()
{
	return ( result != thNULL ) ? result : out;
}
