/*
 * d2aSquare — d2_square(s) の計算本体 (rev4 次元分担デモ・d3aCube のミラー)。原点隅の s×s 正方形を
 * d2Shape に保持。leaf producer (2D shape 出力)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"d2/c++/d2Shape.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/d2aSquare_.h"

CLASS_TINYSTATE(d2/c++/d2aSquare,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	d2aSquare_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<d2Shape>	shape;
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
class d2Shape;
TS_END_INTERFACE

#endif


d2aSquare_::d2aSquare_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
d2aSquare_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	double s = ( na > 0 ) ? (*args)[0]->get_flt() : 1.0;
	shape = d2Shape::square(s);
}

sPtr<pigData>
d2aSquare_::get_result()
{
	return ( result != thNULL ) ? result : sPtr<pigData>::d_cast(shape);
}
