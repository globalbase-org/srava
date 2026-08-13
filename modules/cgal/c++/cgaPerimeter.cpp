/*
 * cgaPerimeter — perimeter(mesh) の計算本体(ptsCalcBody 派生)= 境界長を返す**値返し op**。
 *   2D=外周+穴の周長の総和。3D は未定義 → エラー(表面積は area)。dim() でディスパッチ。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"cg/c++/cgMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/cgaPerimeter_.h"

CLASS_TINYSTATE(cg/c++/cgaPerimeter,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	cgaPerimeter_(
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


cgaPerimeter_::cgaPerimeter_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
cgaPerimeter_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<cgMesh> in = ( na > 0 ) ? sPtr<cgMesh>::d_cast((*args)[0]) : sPtr<cgMesh>();
	if ( ! in.is_notNull() ) {
		result = thNEW(pigDataError,(thNEW(stdString,("perimeter: needs a mesh"))));
		return;
	}
	if ( in->dim() != 2 ) {
		result = thNEW(pigDataError,(thNEW(stdString,("perimeter: 3D surface has no perimeter (use area)"))));
		return;
	}
	result = thNEW(pigDataFloat,(in->op_perimeter()));
}
