/*
 * vdaPtIntersection — intersection(点群, 距離場, ...) — 点群を距離場で切る (#3580)。
 *
  * intersection(点群, 距離場, mode) -> 点群 ・ mode: 0=境界の帯 / -1=内側 / 1=外側 *
 * ★★ 中身は **vd/c++/vdPtSplit.h に 1 本**。判定は距離場の符号 (vdGrid::op_classify_points)。
 *   ⚠ 濾しの規則は geomutils と **同一**にしてある — 変えると同じ op がモジュールごとに
 *     別のことを答える形になる。
 * ⚠ **可換ではない**。型が非対称なので可換フラグを立てると fold 分解が引数を組み替えて壊れる。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"vd/c++/vdGrid.h"
#include	"vd/c++/vdPtSplit.h"
#include	"pt/c++/ptCloud.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/vdaPtIntersection_.h"
#include	<stdio.h>

CLASS_TINYSTATE(vd/c++/vdaPtIntersection,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	vdaPtIntersection_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	/* ⚠ get_result は **public 側**に置く (protected だと codegen が転送を作らない)。 */
	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<ptCloud>	cloud;
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
class ptCloud;
TS_END_INTERFACE

#endif

vdaPtIntersection_::vdaPtIntersection_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
vdaPtIntersection_::compute()
{
	const int na = ( args != 0 ) ? args->length() : 0;
	sPtr<ptCloud> in   = ( na > 0 ) ? sPtr<ptCloud>::d_cast((*args)[0]) : sPtr<ptCloud>();
	sPtr<vdGrid>  grid = ( na > 1 ) ? sPtr<vdGrid>::d_cast((*args)[1])  : sPtr<vdGrid>();
	const int mode = ( na > 2 ) ? (int)(*args)[2]->get_int() : -1;   /* ★ 既定は内側 */

	const char *why = 0;
	sPtr<ptCloud> out = vd_pt_split(in, grid, mode, &why);
	if ( ! out.is_notNull() ) {
		char b[352];
		::snprintf(b, sizeof b, "intersection: %s", why ? why : "could not split the point cloud");
		result = vda_err(thNEW(stdString,(b)));
		return;
	}
	cloud = out;
}

/* エラー時は compute() が result にエラーを残すので result 優先。 */
sPtr<pigData>
vdaPtIntersection_::get_result()
{
	return ( result != thNULL ) ? result : sPtr<pigData>::d_cast(cloud);
}
