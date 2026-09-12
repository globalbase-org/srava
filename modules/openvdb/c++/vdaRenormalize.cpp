/*
 * vdaRenormalize — renormalize(v) の計算本体 (#3434 P2)。
 *
 * ★ なぜ要るか:
 *   ブールの結果 (max/min の合成) は **真の符号付き距離場ではなくなる** (|∇φ| = 1 が崩れる)。
 *   形そのものは正しいのだが、|∇φ| = 1 を仮定する tools::levelSetVolume が**偏る**。
 *   等値面を取り出して測れば正しい値になる。つまり**場は合っていて測り方が偏る**。
 *   ★ intersection (max(a,b)) は偏らない — 直接 voxelize した箱と bit 一致する。
 *     difference (max(a,-b)) の反転だけが場を崩す、という非対称。
 *
 * 等値面を抜き出して距離場を**作り直す**ので、|∇φ| = 1 が回復する。
 * ★ #3491 (2026-09-06): tools::levelSetRebuild そのものは使わない。中身の
 *   「メッシュ → 距離場」が **内部空洞を埋める**ので、そこだけ vd_mesh_to_levelset へ差し替える。
 * offset (距離場に定数を足す) でも帯が足りなくなるので、そこでも同じ関数が要る。
 *
 * ★ **明示 op にしてある** (ブールの内部で自動的に呼ばない)。作り直しは等値面抽出 + 再構築で
 *   ブール本体より重く、連鎖の途中で毎回払うのは無駄だから。必要なところ (測る直前・帯を
 *   広げたい時) で呼ぶ。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"vd/c++/vdGrid.h"
#include	"vd/c++/vdMeshVoxelize.h"   /* ★ #3491: 空洞を保つ共通の入口 */
#include	<stdio.h>
#include	"vd/c++/vdArena.h"   /* ★ #3441: op あたりの TBB 予算 */
#include	"ts2/c++/stdString.h"
#include	<string>
#include	"_ts2/c++/vdaRenormalize_.h"

#include	<vector>   /* ★ #3491: volumeToMesh の受け皿 */

CLASS_TINYSTATE(vd/c++/vdaRenormalize,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	vdaRenormalize_(
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


vdaRenormalize_::vdaRenormalize_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
vdaRenormalize_::compute()
{
	/* ★ #3441: op 内並列 (TBB) は **op あたり**の予算で走らせる。予算未指定なら素通し。
	 *   ⚠ 包み忘れるとその op だけ無制限になるので、compute() 単位で一律に包む。 */
	std::string vdwhy;
	/* ★ #3474 続き: 例外境界。openvdb が投げると受け手が無く、ワーカースレッド
	 *   由来なら agent ごと死ぬ (vdArena.h の vd_arena_guard 参照)。 */
	if ( ! vd_arena_guard("renormalize", [&]{

	vdGrid::ensure_init();
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<vdGrid> in = ( na > 0 ) ? sPtr<vdGrid>::d_cast((*args)[0]) : sPtr<vdGrid>();
	if ( ! in.is_notNull() || ! in->grid() ) {
		result = vda_err(thNEW(stdString,("renormalize: needs an openvdb grid")));
		return;
	}
	/* 第 2 引数 = 帯の半幅 (voxel 単位・省略時は OpenVDB の既定 3)。offset を大きく取るときに
	 * 広げる。0 以下は既定扱い。 */
	double hw = ( na > 1 ) ? (*args)[1]->get_flt() : 0.0;
	float  halfWidth = ( hw > 0 ) ? (float)hw : (float)openvdb::LEVEL_SET_HALF_WIDTH;

	/* ★★ #3491 (2026-09-06): tools::levelSetRebuild を直接呼ばない。
	 *   中身は volumeToMesh → meshToVolume の往復で、**メッシュ → 距離場の側が内部空洞を
	 *   埋める** (これが #3489 の真犯人でもあった)。同じ往復を自前で回し、
	 *   メッシュ → 距離場だけを空洞を保つ共通の入口 (vd_mesh_to_levelset) に差し替える。
	 *   ⚠ volumeToMesh は四角形も出すので、三角形へ割ってから渡す。 */
	std::vector<openvdb::Vec3s> pts;
	std::vector<openvdb::Vec3I> tris;
	std::vector<openvdb::Vec4I> quads;
	/* ⚠ #3498: **volumeToMesh には中断点が 1 つも無い** (VolumeToMesh.h)。
	 *   つまり renormalize は *後半 (メッシュ → 距離場) だけ*が止まる。前半で
	 *   Ctrl+C を受けたら、そこは走り切ってから後半の入口で気づくことになる。 */
	openvdb::tools::volumeToMesh(*in->grid(), pts, tris, quads, /*isovalue=*/0.0);
	tris.reserve(tris.size() + 2*quads.size());
	for ( size_t i = 0 ; i < quads.size() ; ++i ) {
		const openvdb::Vec4I &q = quads[i];
		tris.push_back(openvdb::Vec3I(q[0], q[1], q[2]));
		tris.push_back(openvdb::Vec3I(q[0], q[2], q[3]));
	}
	long fellBack = 0;
	openvdb::FloatGrid::Ptr g =
	    vd_mesh_to_levelset(pts, tris, in->grid()->transform(), halfWidth, &fellBack, &brk_);
	if ( ! g ) {
		if ( (result = vd_abort_err(brk_, "renormalize")) != thNULL ) return;   /* ★ #3498 */
		result = vda_err(thNEW(stdString,("renormalize: levelSetRebuild failed")));
		return;
	}
	/* ★ #3498: meshToVolume は中断されると **途中までの格子**を返すことがある (null とは限らない)。
	 *   それを set_body すると「作り直した」と称する壊れた距離場がキャッシュに焼き付くので、
	 *   null でなくても旗を見る。 */
	if ( (result = vd_abort_err(brk_, "renormalize")) != thNULL ) return;
	if ( fellBack > 0 )
		::fprintf(stderr, "[renormalize] WARN: %ld column(s) with non-zero winding sum "
		                  "(isosurface is not a closed, consistently oriented surface); "
		                  "internal cavities will be filled\n", fellBack);
	out = thNEW(vdGrid,());
	out->set_grid(g);
	out->set_normalized(true);   /* 作り直した = |grad| = 1 が回復した */
	}, vdwhy) )
		result = vda_err(thNEW(stdString,(vdwhy.c_str())));
}

sPtr<pigData>
vdaRenormalize_::get_result()
{
	return ( result != thNULL ) ? result : out;
}
