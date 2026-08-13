/*
 * mfaVolume — volume(mesh) の計算本体(mf 版・cgaVolume のミラー)。値返し(体積)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"mf/c++/mfMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/mfaVolume_.h"

CLASS_TINYSTATE(mf/c++/mfaVolume,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	mfaVolume_(
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


mfaVolume_::mfaVolume_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
mfaVolume_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<mfMesh> in = ( na > 0 ) ? sPtr<mfMesh>::d_cast((*args)[0]) : sPtr<mfMesh>();
	if ( ! in.is_notNull() ) {
		result = thNEW(pigDataError,(thNEW(stdString,("volume: needs a 3D mesh"))));
		return;
	}
	result = thNEW(pigDataFloat,(in->op_volume()));
}
