/*
 * vgaIsosurface — isosurface(vd-grid3d, iso) の geogram 版 (#3434・openvdb_gg.so)。
 * ★ **本物の ggMesh を返す**。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"vd/c++/vdGrid.h"
#include	"vd/c++/vdGridVdb.h"   /* ★ #3545 段 5: 橋は OpenVDB 型を扱うので読んでよい */
#include	"vd/c++/vdArena.h"   /* ★ #3441: op あたりの TBB 予算 */
#include	"gg/c++/ggMesh.h"
#include	"ts2/c++/stdString.h"
#include	<string>
#include	"_ts2/c++/vgaIsosurface_.h"
#include	<geogram/mesh/mesh.h>

#include	<openvdb/tools/VolumeToMesh.h>
#include	<vector>
#include	"pig/c++/pigModuleError.h"
/* ★ #3475: このモジュール専用のエラー生成子 (共有ヘッダを持たないので
 *   ここで定義する)。文言は "[TAG] openvdb_gg/op: message" になる。 */
PIG_DEFINE_MODULE_ERR(vga_err, "openvdb_gg")


CLASS_TINYSTATE(vg/c++/vgaIsosurface,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	vgaIsosurface_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<ggMesh>	out;
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
class ggMesh;
TS_END_INTERFACE

#endif


vgaIsosurface_::vgaIsosurface_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
vgaIsosurface_::compute()
{
	/* ★ #3441: op 内並列 (TBB) は **op あたり**の予算で走らせる。予算未指定なら素通し。
	 *   ⚠ 包み忘れるとその op だけ無制限になるので、compute() 単位で一律に包む。 */
	std::string vdwhy;
	/* ★ #3474 続き: 例外境界。openvdb が投げると受け手が無く、ワーカースレッド
	 *   由来なら agent ごと死ぬ (vdArena.h の vd_arena_guard 参照)。 */
	if ( ! vd_arena_guard("isosurface", [&]{

	vdGrid::ensure_init();
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<vdGrid> in = ( na > 0 ) ? sPtr<vdGrid>::d_cast((*args)[0]) : sPtr<vdGrid>();
	if ( ! in.is_notNull() || ! in->box().g ) {
		result = vga_err(thNEW(stdString,("isosurface: needs an openvdb grid")));
		return;
	}
	double iso = ( na > 1 ) ? (*args)[1]->get_flt() : 0.0;

	/* ★ 三角形が欲しいときは **四角形版を呼んで自分で分割する**のが上流の推奨
	 *   (VolumeToMesh.h: "Do not use this method just to get a triangle mesh - use the above
	 *    method and post process the quad index list")。adaptivity 版は適応的メッシュ用。 */
	std::vector<openvdb::Vec3s> points;
	std::vector<openvdb::Vec4I> quads;
	openvdb::tools::volumeToMesh(*in->box().g, points, quads, iso);
	if ( points.empty() || quads.empty() ) {
		result = vga_err(thNEW(stdString,
		    ("isosurface: empty surface (isovalue outside the narrow band?)")));
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

	/* ★ 頂点/三角形 → GEO::Mesh。 */
	out = thNEW(ggMesh,());
	GEO::Mesh &m = out->mesh();
	m.vertices.create_vertices((GEO::index_t)(v.size() / 3));
	for ( size_t i = 0, k = 0 ; i + 2 < v.size() ; i += 3, ++k )
		m.vertices.point((GEO::index_t)k) = GEO::vec3(v[i], v[i+1], v[i+2]);
	for ( size_t i = 0 ; i + 2 < t.size() ; i += 3 )
		m.facets.create_triangle((GEO::index_t)t[i], (GEO::index_t)t[i+1], (GEO::index_t)t[i+2]);
	m.facets.connect();
	}, vdwhy) )
		result = vga_err(thNEW(stdString,(vdwhy.c_str())));
}

sPtr<pigData>
vgaIsosurface_::get_result()
{
	return ( result != thNULL ) ? result : out;
}
