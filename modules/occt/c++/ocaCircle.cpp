/*
 * ocaCircle — circle(r[, segs]) — 2D 円 (occt 版・#3474)。
 * ★ **厳密**な円 (Geom_Circle 1 本の Wire)。segs は近似しないので **無視する**
 *   (occt の sphere / cylinder / torus と同じ扱い)。
 * ⚠ したがってメッシュ系 (内接正多角形) とは面積が構造的に違う = カーネル一致の表には
 *   入れられない。検証は閉形式 (πr²) で行う。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"oc/c++/ocShape.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ocaCircle_.h"

#include	<BRepBuilderAPI_MakeEdge.hxx>
#include	<BRepBuilderAPI_MakeWire.hxx>
#include	<BRepBuilderAPI_MakeFace.hxx>
#include	<Geom_Circle.hxx>
#include	<TopoDS_Shape.hxx>
#include	<TopoDS_Face.hxx>
#include	<TopoDS_Wire.hxx>
#include	<TopoDS_Edge.hxx>
#include	<gp_Pnt.hxx>
#include	<gp_Dir.hxx>
#include	<gp_Ax2.hxx>
#include	<Standard_Failure.hxx>
#include	<cmath>
#include	<string>

CLASS_TINYSTATE(oc/c++/ocaCircle,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ocaCircle_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<ocFace2D>	out;
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
class ocFace2D;
TS_END_INTERFACE

#endif


ocaCircle_::ocaCircle_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
ocaCircle_::compute()
{
	ocShape::ensure_init();
	int na = ( args != 0 ) ? args->length() : 0;
	double r = ( na > 0 ) ? (*args)[0]->get_flt() : 1.0;
	if ( !(r > 0) ) { result = oca_err(thNEW(stdString,("circle: radius must be > 0"))); return; }
	try {
		/* XY 平面・原点中心。 */
		gp_Ax2 ax(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1));
		Handle(Geom_Circle) c = new Geom_Circle(ax, r);
		BRepBuilderAPI_MakeEdge me(c);
		if ( ! me.IsDone() ) { result = oca_err(thNEW(stdString,("circle: could not build the edge"))); return; }
		BRepBuilderAPI_MakeWire mw(me.Edge());
		if ( ! mw.IsDone() ) { result = oca_err(thNEW(stdString,("circle: could not build the wire"))); return; }
		BRepBuilderAPI_MakeFace mf(mw.Wire());
		if ( ! mf.IsDone() ) { result = oca_err(thNEW(stdString,("circle: could not build the face"))); return; }
		out = thNEW(ocFace2D,());
		out->set_shape(mf.Face());
	} catch ( const Standard_Failure& e ) {
		std::string m = std::string("circle: OCCT failed [") + e.DynamicType()->Name() + "] (" +
		    ( ( e.GetMessageString() && e.GetMessageString()[0] ) ? e.GetMessageString() : "no message" ) + ")";
		result = oca_err(thNEW(stdString,(m.c_str())));
		return;
	}
}

/* この演算の結果。エラー時は compute() が result にエラー値を残して本体未設定で return するので
 * result 優先。 */
sPtr<pigData>
ocaCircle_::get_result()
{
	return ( result != thNULL ) ? result : sPtr<pigData>::d_cast(out);
}
