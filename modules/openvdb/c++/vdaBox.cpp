/*
 * vdaBox — box(w,h,d) / boxa([w,h,d]) の計算本体 (#3462)。
 * ★ OpenVDB は一般の直方体の生成器を持たない (createLevelSetCube は立方体のみ) ので、
 *   8 頂点 12 三角形を組んで meshToLevelSet へ渡す (vdGrid::make_box)。頂点をこの場で作る
 *   だけなので **メッシュカーネルには依存しない**。
 * ★ 精度は **末尾の引数 dx**。box(w,h,d,dx) / boxa([w,h,d],dx)。
 *   voxelize(mesh, dx) の書き方を踏襲する (ひさ指示 2026-08-31)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"vd/c++/vdGrid.h"
#include	"vd/c++/vdArena.h"   /* ★ #3441: op あたりの TBB 予算 */
#include	"ts2/c++/stdString.h"
#include	<string>
#include	"_ts2/c++/vdaBox_.h"

#include	<stdio.h>

CLASS_TINYSTATE(vd/c++/vdaBox,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	vdaBox_(
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


vdaBox_::vdaBox_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
vdaBox_::compute()
{
	std::string vdwhy;
	/* ★ #3474 続き: 例外境界。openvdb が投げると受け手が無く、ワーカースレッド
	 *   由来なら agent ごと死ぬ (vdArena.h の vd_arena_guard 参照)。 */
	if ( ! vd_arena_guard("box", [&]{

	vdGrid::ensure_init();
	int na = ( args != 0 ) ? args->length() : 0;
	double w = 0.0, h = 0.0, d = 0.0, dx = 0.0;
	/* 第 1 引数が配列なら boxa([w,h,d], dx)。そうでなければ box(w,h,d,dx)。
	 * どちらも **dx は末尾**。 */
	sPtr<pigDataArray> dims = ( na > 0 ) ? (*args)[0]->obt_array() : sPtr<pigDataArray>();
	if ( dims.is_notNull() ) {
		int nd = dims->length();
		if ( nd > 0 ) w = dims->get_ix(thNEW(pigDataInteger,((INTEGER64)0)))->get_flt();
		if ( nd > 1 ) h = dims->get_ix(thNEW(pigDataInteger,((INTEGER64)1)))->get_flt();
		if ( nd > 2 ) d = dims->get_ix(thNEW(pigDataInteger,((INTEGER64)2)))->get_flt();
		dx = ( na > 1 ) ? (*args)[1]->get_flt() : 0.0;
	} else {
		if ( na > 0 ) w  = (*args)[0]->get_flt();
		if ( na > 1 ) h  = (*args)[1]->get_flt();
		if ( na > 2 ) d  = (*args)[2]->get_flt();
		dx = ( na > 3 ) ? (*args)[3]->get_flt() : 0.0;
	}
	if ( !(w > 0) || !(h > 0) || !(d > 0) ) {
		result = vda_err(thNEW(stdString,("box: sizes must be > 0")));
		return;
	}
	if ( !(dx > 0) ) {
		result = vda_err(thNEW(stdString,(
		    "box: last argument is dx (voxel size in world units) and must be > 0")));
		return;
	}
	out = vdGrid::make_box(w, h, d, dx);
	if ( ! out.is_notNull() )
		result = vda_err(thNEW(stdString,("box: openvdb meshToLevelSet failed")));
	}, vdwhy) )
		result = vda_err(thNEW(stdString,(vdwhy.c_str())));
}

sPtr<pigData>
vdaBox_::get_result()
{
	return ( result != thNULL ) ? result : out;
}
