/*
 * ptaCentroid — centroid(p) の計算本体 (#3528)。配列返し ([x,y] / [x,y,z])。
 * ★ 点群の重心は **点の平均**。メッシュの面積/体積重心とは別物だが、「重心」という約束は同じ。
 * ⚠ 空の点群に重心は無い ⇒ 明示エラー。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"pt/c++/ptCloud.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ptaCentroid_.h"

CLASS_TINYSTATE(pt/c++/ptaCentroid,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ptaCentroid_(
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


ptaCentroid_::ptaCentroid_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
ptaCentroid_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<ptCloud> in = ( na > 0 ) ? sPtr<ptCloud>::d_cast((*args)[0]) : sPtr<ptCloud>();
	if ( ! in.is_notNull() ) {
		result = pta_err(thNEW(stdString,("centroid: needs a point cloud")));
		return;
	}
	double c[3];
	int d = in->op_centroid(c);
	if ( d == 0 ) {
		result = pta_err(thNEW(stdString,("centroid: the point cloud is empty")));
		return;
	}
	sPtr<pigDataArray> arr = thNEW(pigDataArray,());
	for ( int i = 0 ; i < d ; ++i )
		arr->push(thNEW(pigDataFloat,(c[i])));
	result = arr;
}
