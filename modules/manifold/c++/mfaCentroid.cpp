/*
 * mfaCentroid — centroid(mesh) の計算本体(mf 版・cgaCentroid のミラー)。配列返し([x,y,z])。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"mf/c++/mfMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/mfaCentroid_.h"

CLASS_TINYSTATE(mf/c++/mfaCentroid,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	mfaCentroid_(
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


mfaCentroid_::mfaCentroid_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
mfaCentroid_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<mfMesh> in = ( na > 0 ) ? sPtr<mfMesh>::d_cast((*args)[0]) : sPtr<mfMesh>();
	if ( ! in.is_notNull() ) {
		result = thNEW(pigDataError,(thNEW(stdString,("centroid: needs a 3D mesh"))));
		return;
	}
	double c[3] = {0,0,0};
	int n = in->op_centroid(c);
	sPtr<pigDataArray> arr = thNEW(pigDataArray,());
	for ( int i = 0 ; i < n ; ++i )
		arr->push(thNEW(pigDataFloat,(c[i])));
	result = arr;
}
