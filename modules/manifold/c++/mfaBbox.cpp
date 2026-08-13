/*
 * mfaBbox — bbox(mesh) の計算本体(mf 版・cgaBbox のミラー)。値返し([min隅, max隅])。
 * 3D(mfMesh)/ 2D(mfCross)両対応。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"mf/c++/mfMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/mfaBbox_.h"

CLASS_TINYSTATE(mf/c++/mfaBbox,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	mfaBbox_(
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
TS_END_INTERFACE

#endif


mfaBbox_::mfaBbox_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
mfaBbox_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<pigData> a = ( na > 0 ) ? (*args)[0] : sPtr<pigData>();
	double mn[3] = {0,0,0}, mx[3] = {0,0,0};
	int n = 0;
	sPtr<mfMesh>  m3 = sPtr<mfMesh>::d_cast(a);
	sPtr<mfCross> c2 = sPtr<mfCross>::d_cast(a);
	if ( m3.is_notNull() )      n = m3->op_bbox(mn, mx);
	else if ( c2.is_notNull() ) n = c2->op_bbox(mn, mx);
	else {
		result = thNEW(pigDataError,(thNEW(stdString,("bbox: needs a mesh"))));
		return;
	}
	sPtr<pigDataArray> lo = thNEW(pigDataArray,());
	sPtr<pigDataArray> hi = thNEW(pigDataArray,());
	for ( int i = 0 ; i < n ; ++i ) {
		lo->push(thNEW(pigDataFloat,(mn[i])));
		hi->push(thNEW(pigDataFloat,(mx[i])));
	}
	sPtr<pigDataArray> arr = thNEW(pigDataArray,());
	arr->push(lo);
	arr->push(hi);
	result = arr;
}
