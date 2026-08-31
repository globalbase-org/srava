/*
 * chaVolume — volume(m) の計算本体 (cherchi 版)。値返し。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"ch/c++/chMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/chaVolume_.h"

CLASS_TINYSTATE(ch/c++/chaVolume,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	chaVolume_(
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
class chMesh;
TS_END_INTERFACE

#endif


chaVolume_::chaVolume_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
chaVolume_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<chMesh> in = ( na > 0 ) ? sPtr<chMesh>::d_cast((*args)[0]) : sPtr<chMesh>();
	if ( ! in.is_notNull() ) {
		result = thNEW(pigDataError,(thNEW(stdString,("volume: needs a cherchi mesh"))));
		return;
	}
	result = thNEW(pigDataFloat,(in->volume()));
}
