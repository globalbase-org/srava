/*
 * ptaBbox — bbox(p) の計算本体 (#3528)。入れ子配列返し ([min 隅, max 隅])。
 * ⚠ 空の点群に AABB は無い ⇒ 明示エラー (0 の箱を黙って返さない)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"pt/c++/ptCloud.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ptaBbox_.h"

CLASS_TINYSTATE(pt/c++/ptaBbox,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ptaBbox_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

protected:
	virtual void	compute();
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
class ptCloud;
TS_END_INTERFACE

#endif


ptaBbox_::ptaBbox_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
ptaBbox_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<ptCloud> in = ( na > 0 ) ? sPtr<ptCloud>::d_cast((*args)[0]) : sPtr<ptCloud>();
	if ( ! in.is_notNull() ) {
		result = pta_err(thNEW(stdString,("bbox: needs a point cloud")));
		return;
	}
	double mn[3], mx[3];
	int d = in->op_bbox(mn, mx);
	if ( d == 0 ) {
		result = pta_err(thNEW(stdString,("bbox: the point cloud is empty")));
		return;
	}
	sPtr<pigDataArray> lo = thNEW(pigDataArray,());
	sPtr<pigDataArray> hi = thNEW(pigDataArray,());
	for ( int i = 0 ; i < d ; ++i ) {
		lo->push(thNEW(pigDataFloat,(mn[i])));
		hi->push(thNEW(pigDataFloat,(mx[i])));
	}
	sPtr<pigDataArray> arr = thNEW(pigDataArray,());   /* [min 隅, max 隅] */
	arr->push(lo);
	arr->push(hi);
	result = arr;
}
