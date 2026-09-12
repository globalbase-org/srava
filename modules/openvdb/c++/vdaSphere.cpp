/*
 * vdaSphere — sphere(r, dx) の計算本体 (#3462)。
 * ★ OpenVDB 本体の createLevelSetSphere をそのまま使う。**メッシュを経由しない**。
 * ★ 第 2 引数は **dx (ボクセルサイズ)**。voxelize(mesh, dx) の書き方を踏襲する
 *   (ひさ指示 2026-08-31)。メッシュ系カーネルの sphere(r, seg) と位置は同じで、
 *   「その表現で精度を決める値」という役割も同じ。ボクセル表現に分割数は意味を持たない。
 * ⚠ dx が違うグリッド同士は合成できない。bool_from_args が transform 全体を比べて
 *   明示エラーにする。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"vd/c++/vdGrid.h"
#include	"vd/c++/vdArena.h"   /* ★ #3441: op あたりの TBB 予算 */
#include	"ts2/c++/stdString.h"
#include	<string>
#include	"_ts2/c++/vdaSphere_.h"

#include	<stdio.h>

CLASS_TINYSTATE(vd/c++/vdaSphere,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	vdaSphere_(
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


vdaSphere_::vdaSphere_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
vdaSphere_::compute()
{
	std::string vdwhy;
	/* ★ #3474 続き: 例外境界。openvdb が投げると受け手が無く、ワーカースレッド
	 *   由来なら agent ごと死ぬ (vdArena.h の vd_arena_guard 参照)。 */
	if ( ! vd_arena_guard("sphere", [&]{

	vdGrid::ensure_init();
	int na = ( args != 0 ) ? args->length() : 0;
	double r  = ( na > 0 ) ? (*args)[0]->get_flt() : 0.0;
	double dx = ( na > 1 ) ? (*args)[1]->get_flt() : 0.0;
	if ( !(r > 0) ) {
		result = vda_err(thNEW(stdString,("sphere: radius must be > 0")));
		return;
	}
	if ( !(dx > 0) ) {
		result = vda_err(thNEW(stdString,(
		    "sphere: 2nd argument is dx (voxel size in world units) and must be > 0")));
		return;
	}
	out = vdGrid::make_sphere(r, dx);
	if ( ! out.is_notNull() )
		result = vda_err(thNEW(stdString,(
		    "sphere: openvdb createLevelSetSphere failed")));
	}, vdwhy) )
		result = vda_err(thNEW(stdString,(vdwhy.c_str())));
}

sPtr<pigData>
vdaSphere_::get_result()
{
	return ( result != thNULL ) ? result : out;
}
