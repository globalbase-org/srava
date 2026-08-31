/*
 * vmaVoxelize — voxelize(mf-mesh3d, dx) の計算本体 (#3434・openvdb_mf.so)。
 *
 * ★★ **本物の mfMesh を受け取る**。旧実装は openvdb 側に vdMesh という**新しいクラス**を作って
 *   mf-mesh3d を名乗らせていたが、それは docs の「新しい型を作らず既存の型を名乗る」に違反していた
 *   (型名だけ借りて実体を新設していた)。process 実行では codec 経由なので露出しなかったが、
 *   **in-proc では生オブジェクトが渡るので d_cast が失敗する**。
 *   → cross モジュール (両側を知る) に置き、**相手の本物のクラス**を使う形に是正した。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"vd/c++/vdGrid.h"
#include	"vd/c++/vdArena.h"   /* ★ #3441: op あたりの TBB 予算 */
#include	"mf/c++/mfMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/vmaVoxelize_.h"
#include	<manifold/manifold.h>

#include	<openvdb/tools/MeshToVolume.h>
#include	<vector>

CLASS_TINYSTATE(vm/c++/vmaVoxelize,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	vmaVoxelize_(
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


vmaVoxelize_::vmaVoxelize_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
vmaVoxelize_::compute()
{
	/* ★ #3441: op 内並列 (TBB) は **op あたり**の予算で走らせる。予算未指定なら素通し。
	 *   ⚠ 包み忘れるとその op だけ無制限になるので、compute() 単位で一律に包む。 */
	vd_in_arena([&]{
	vdGrid::ensure_init();
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<mfMesh> in = ( na > 0 ) ? sPtr<mfMesh>::d_cast((*args)[0]) : sPtr<mfMesh>();
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
	/* ★ 本物の Manifold から抽出する。GetMeshGL64 は **lazy CSG をここで評価する**
	 *   (mfMesh::encode と同じコスト構造)。 */
	manifold::MeshGL64 m = in->manifold().GetMeshGL64();
	const int np = m.numProp ? m.numProp : 1;
	std::vector<openvdb::Vec3s> points;
	points.reserve(m.vertProperties.size() / np);
	for ( size_t i = 0 ; i + (size_t)np <= m.vertProperties.size() ; i += (size_t)np ) {
		const double *p = &m.vertProperties[i];
		points.push_back(openvdb::Vec3s((float)p[0], (float)p[1], (float)p[2]));
	}
	std::vector<openvdb::Vec3I> tris;
	tris.reserve(m.triVerts.size() / 3);
	for ( size_t i = 0 ; i + 2 < m.triVerts.size() ; i += 3 )
		tris.push_back(openvdb::Vec3I((uint32_t)m.triVerts[i], (uint32_t)m.triVerts[i+1],
		                              (uint32_t)m.triVerts[i+2]));
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
vmaVoxelize_::get_result()
{
	return ( result != thNULL ) ? result : out;
}
