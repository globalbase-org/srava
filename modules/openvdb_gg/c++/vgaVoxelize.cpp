/*
 * vgaVoxelize — voxelize(gg-mesh3d, dx) の計算本体 (#3434・openvdb_gg.so)。
 * ★ **本物の ggMesh を受け取る** (vmaVoxelize の geogram 版)。ライセンスは BSD-3 + Apache-2.0。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"vd/c++/vdGrid.h"
#include	"vd/c++/vdMeshVoxelize.h"   /* ★ #3491: 空洞を保つ共通の入口 */
#include	<stdio.h>   /* ★ #3491: 退避したときの WARN (PIG_SEP_LOG で採取可) */
#include	"vd/c++/vdArena.h"   /* ★ #3441: op あたりの TBB 予算 */
#include	"gg/c++/ggMesh.h"
#include	"ts2/c++/stdString.h"
#include	<string>
#include	"_ts2/c++/vgaVoxelize_.h"
#include	<geogram/mesh/mesh.h>

#include	<openvdb/tools/MeshToVolume.h>
#include	<vector>
#include	"pig/c++/pigModuleError.h"
/* ★ #3475: このモジュール専用のエラー生成子 (共有ヘッダを持たないので
 *   ここで定義する)。文言は "[TAG] openvdb_gg/op: message" になる。 */
PIG_DEFINE_MODULE_ERR(vga_err, "openvdb_gg")


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
	std::string vdwhy;
	/* ★ #3474 続き: 例外境界。openvdb が投げると受け手が無く、ワーカースレッド
	 *   由来なら agent ごと死ぬ (vdArena.h の vd_arena_guard 参照)。 */
	if ( ! vd_arena_guard("voxelize", [&]{

	vdGrid::ensure_init();
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<ggMesh> in = ( na > 0 ) ? sPtr<ggMesh>::d_cast((*args)[0]) : sPtr<ggMesh>();
	if ( ! in.is_notNull() ) {
		result = vga_err(thNEW(stdString,("voxelize: needs a 3D mesh")));
		return;
	}
	double dx = ( na > 1 ) ? (*args)[1]->get_flt() : 0.0;
	if ( !(dx > 0) ) {
		result = vga_err(thNEW(stdString,
		    ("voxelize: dx (voxel size in world units) must be > 0")));
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
		result = vga_err(thNEW(stdString,("voxelize: empty mesh")));
		return;
	}

	openvdb::math::Transform::Ptr xform = openvdb::math::Transform::createLinearTransform(dx);
	/* ★ 帯幅は既定 (LEVEL_SET_HALF_WIDTH = 3 voxel)。offset を大きく取るときは帯が足りなく
	 *   なるので、その時は再正規化 (tools::levelSetRebuild) が要る — #3434 の offset 実装で扱う。 */
	/* ★★ #3491 (2026-09-06): tools::meshToLevelSet を直接呼ばず vd_mesh_to_levelset を通す。
	 *   OpenVDB は符号を「グリッド外周から到達できるか」で決めるので、**閉じた内部空洞を
	 *   材料として埋めてしまう** (中空の殻が「詰まった球」になる)。共通の入口が巻き数の
	 *   interiorTest を渡して直す。理由と実測は vd/c++/vdMeshVoxelize.h の冒頭。 */
	long fellBack = 0;
	openvdb::FloatGrid::Ptr g =
	    vd_mesh_to_levelset(points, tris, *xform, (float)openvdb::LEVEL_SET_HALF_WIDTH, &fellBack);
	if ( ! g ) {
		result = vga_err(thNEW(stdString,("voxelize: meshToLevelSet failed")));
		return;
	}
	if ( fellBack > 0 )   /* 閉じた向きの揃った曲面ではない = 従来経路で作った (空洞は埋まる) */
		::fprintf(stderr, "[voxelize] WARN: %ld column(s) with non-zero winding sum "
		                  "(input is not a closed, consistently oriented surface); "
		                  "internal cavities will be filled\n", fellBack);
	out = thNEW(vdGrid,());
	out->set_grid(g);
	out->set_normalized(true);   /* meshToVolume は真の符号付き距離場を作る */
	}, vdwhy) )
		result = vga_err(thNEW(stdString,(vdwhy.c_str())));
}

sPtr<pigData>
vgaVoxelize_::get_result()
{
	return ( result != thNULL ) ? result : out;
}
