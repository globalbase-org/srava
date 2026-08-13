/*
 * d3aNfaces — d3_nfaces(mesh) の計算本体 (rev4 Phase D-3・mfaVolume のミラー)。値返し(三角形数)。
 * mesh cache を読んで面数を int で返す consumer→value op = 往復検証をシェルから読める値にする点。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"d3/c++/d3Mesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/d3aNfaces_.h"

CLASS_TINYSTATE(d3/c++/d3aNfaces,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	d3aNfaces_(
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


d3aNfaces_::d3aNfaces_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
d3aNfaces_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<d3Mesh> in = ( na > 0 ) ? sPtr<d3Mesh>::d_cast((*args)[0]) : sPtr<d3Mesh>();
	if ( ! in.is_notNull() ) {
		result = thNEW(pigDataError,(thNEW(stdString,("nfaces: needs a d3 mesh"))));
		return;
	}
	result = thNEW(pigDataInteger,((INTEGER64)in->nf()));
}
