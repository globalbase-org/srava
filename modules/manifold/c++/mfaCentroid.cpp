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
	sPtr<mfMesh>  m3 = ( na > 0 ) ? sPtr<mfMesh>::d_cast((*args)[0])  : sPtr<mfMesh>();
	sPtr<mfCross> c2 = ( na > 0 ) ? sPtr<mfCross>::d_cast((*args)[0]) : sPtr<mfCross>();
	if ( ! m3.is_notNull() && ! c2.is_notNull() ) {
		result = mfa_err(thNEW(stdString,("centroid: needs a mesh")));
		return;
	}
	double c[3] = {0,0,0};
	/* ★ #3533: 2D も受ける (それまで manifold だけ 2D の centroid が無かった)。
	 *   ⚠ 2D は **cross2d=局所 2 成分 / face3d=world 3 成分** で返り値の長さが変わる。 */
	int n = m3.is_notNull() ? m3->op_centroid(c) : c2->op_centroid(c);
	sPtr<pigDataArray> arr = thNEW(pigDataArray,());
	for ( int i = 0 ; i < n ; ++i )
		arr->push(thNEW(pigDataFloat,(c[i])));
	result = arr;
}
