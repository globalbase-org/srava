/*
 * ocaBox — box(w,h,d) の計算本体。★6 枚の**平面 Face** で作る (三角形ではない) (#3437 P5)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"oc/c++/ocShape.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ocaBox_.h"
#include	<BRepPrimAPI_MakeBox.hxx>
#include	<gp_Pnt.hxx>
#include	<TopoDS_Shape.hxx>

CLASS_TINYSTATE(oc/c++/ocaBox,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ocaBox_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<ocShape>	out;
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
class ocShape;
TS_END_INTERFACE

#endif


ocaBox_::ocaBox_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/


void
ocaBox_::compute()
{
	ocShape::ensure_init();   /* ★ OCCT の診断出力を stdout から外す (ocShape.h 参照) */
	int na = ( args != 0 ) ? args->length() : 0;
	double w = ( na > 0 ) ? (*args)[0]->get_flt() : 1.0;
	double h = ( na > 1 ) ? (*args)[1]->get_flt() : 1.0;
	double d = ( na > 2 ) ? (*args)[2]->get_flt() : 1.0;
	if ( !(w > 0) || !(h > 0) || !(d > 0) ) {
		result = thNEW(pigDataError,(thNEW(stdString,("box: sizes must be > 0"))));
		return;
	}
	/* 他カーネルと同じく **原点中心**。BRepPrimAPI_MakeBox は角が原点なので寄せる。 */
	gp_Pnt corner(-w/2, -h/2, -d/2);
	BRepPrimAPI_MakeBox mk(corner, w, h, d);
	/* ★ プリミティブは **遅延構築**で、Shape() を呼んだ時点で Build() が走る。
	 *   IsDone() は BRepBuilderAPI_Command の done フラグで、プリミティブでは立たない
	 *   (これで最初 "box: OCCT failed" になった)。IsNull() で判定するのが正しい。 */
	TopoDS_Shape sh = mk.Shape();
	if ( sh.IsNull() ) {
		result = thNEW(pigDataError,(thNEW(stdString,("box: OCCT produced a null shape"))));
		return;
	}
	out = thNEW(ocShape,());
	out->set_shape(sh);
}

sPtr<pigData>
ocaBox_::get_result()
{
	return ( result != thNULL ) ? result : out;
}
