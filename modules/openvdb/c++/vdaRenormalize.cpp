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
 * tools::levelSetRebuild は等値面を抜き出して距離場を**作り直す**ので、|∇φ| = 1 が回復する。
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
#include	"vd/c++/vdArena.h"   /* ★ #3441: op あたりの TBB 予算 */
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/vdaRenormalize_.h"

#include	<openvdb/tools/LevelSetRebuild.h>

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
	vd_in_arena([&]{
	vdGrid::ensure_init();
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<vdGrid> in = ( na > 0 ) ? sPtr<vdGrid>::d_cast((*args)[0]) : sPtr<vdGrid>();
	if ( ! in.is_notNull() || ! in->grid() ) {
		result = thNEW(pigDataError,(thNEW(stdString,("renormalize: needs an openvdb grid"))));
		return;
	}
	/* 第 2 引数 = 帯の半幅 (voxel 単位・省略時は OpenVDB の既定 3)。offset を大きく取るときに
	 * 広げる。0 以下は既定扱い。 */
	double hw = ( na > 1 ) ? (*args)[1]->get_flt() : 0.0;
	float  halfWidth = ( hw > 0 ) ? (float)hw : (float)openvdb::LEVEL_SET_HALF_WIDTH;

	openvdb::FloatGrid::Ptr g =
	    openvdb::tools::levelSetRebuild(*in->grid(), /*isovalue=*/0.0f, halfWidth);
	if ( ! g ) {
		result = thNEW(pigDataError,(thNEW(stdString,("renormalize: levelSetRebuild failed"))));
		return;
	}
	out = thNEW(vdGrid,());
	out->set_grid(g);
	out->set_normalized(true);   /* 作り直した = |grad| = 1 が回復した */
	});
}

sPtr<pigData>
vdaRenormalize_::get_result()
{
	return ( result != thNULL ) ? result : out;
}
