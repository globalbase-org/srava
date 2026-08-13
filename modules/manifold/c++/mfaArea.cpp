/*
 * mfaArea — area(mesh) の計算本体(mf 版・cgaArea のミラー)。値返し。
 * 3D(mfMesh)=表面積 / 2D(mfCross)=囲み面積。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"mf/c++/mfMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/mfaArea_.h"

CLASS_TINYSTATE(mf/c++/mfaArea,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	mfaArea_(
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


mfaArea_::mfaArea_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
mfaArea_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<pigData> a = ( na > 0 ) ? (*args)[0] : sPtr<pigData>();
	sPtr<mfMesh>  m3 = sPtr<mfMesh>::d_cast(a);
	sPtr<mfCross> c2 = sPtr<mfCross>::d_cast(a);
	if ( m3.is_notNull() )      result = thNEW(pigDataFloat,(m3->op_area()));
	else if ( c2.is_notNull() ) result = thNEW(pigDataFloat,(c2->op_area()));
	else result = thNEW(pigDataError,(thNEW(stdString,("area: needs a mesh"))));
}
