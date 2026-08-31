/*
 * ocaCylinder — cylinder(r,h) の計算本体 (#3437 P5)。
 * ★ 側面は**厳密な円筒面 1 枚**、上下は平面 2 枚 = Face 数 3。三角形近似は入らないので
 *   volume は π r² h とちょうど一致する (分割数という概念が無い)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"oc/c++/ocShape.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ocaCylinder_.h"
#include	<BRepPrimAPI_MakeCylinder.hxx>
#include	<gp_Ax2.hxx>
#include	<gp_Pnt.hxx>
#include	<gp_Dir.hxx>
#include	<TopoDS_Shape.hxx>

CLASS_TINYSTATE(oc/c++/ocaCylinder,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ocaCylinder_(
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


ocaCylinder_::ocaCylinder_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
ocaCylinder_::compute()
{
	ocShape::ensure_init();   /* ★ OCCT の診断出力を stdout から外す (ocShape.h 参照) */
	int na = ( args != 0 ) ? args->length() : 0;
	double r = ( na > 0 ) ? (*args)[0]->get_flt() : 1.0;
	double h = ( na > 1 ) ? (*args)[1]->get_flt() : 1.0;
	if ( !(r > 0) || !(h > 0) ) {
		result = thNEW(pigDataError,(thNEW(stdString,("cylinder: r and h must be > 0"))));
		return;
	}
	/* 他カーネルの box / sphere と同じく **原点中心**・軸は +Z。
	 * BRepPrimAPI_MakeCylinder は基準点が底面中心なので h/2 下げる。 */
	gp_Ax2 ax(gp_Pnt(0, 0, -h/2), gp_Dir(0, 0, 1));
	BRepPrimAPI_MakeCylinder mk(ax, r, h);
	/* ★ プリミティブは遅延構築。IsDone() は立たないので IsNull() で見る (ocaBox と同じ罠)。 */
	TopoDS_Shape sh = mk.Shape();
	if ( sh.IsNull() ) {
		result = thNEW(pigDataError,(thNEW(stdString,("cylinder: OCCT produced a null shape"))));
		return;
	}
	out = thNEW(ocShape,());
	out->set_shape(sh);
}

sPtr<pigData>
ocaCylinder_::get_result()
{
	return ( result != thNULL ) ? result : out;
}
