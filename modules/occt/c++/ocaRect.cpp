/*
 * ocaRect — rect(w, h) — 2D 矩形 (occt 版・#3474)。
 * ★ **角が原点**・CCW で (0,0)→(w,0)→(w,h)→(0,h) (cgal / manifold の rect と同じ)。
 * ★ 平面の 4 辺なのでメッシュ系と厳密に一致する。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"oc/c++/ocShape.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ocaRect_.h"

#include	<BRepBuilderAPI_MakePolygon.hxx>
#include	<BRepBuilderAPI_MakeFace.hxx>
#include	<TopoDS_Shape.hxx>
#include	<TopoDS_Face.hxx>
#include	<TopoDS_Wire.hxx>
#include	<gp_Pnt.hxx>
#include	<Standard_Failure.hxx>
#include	<vector>
#include	<cmath>
#include	<string>

CLASS_TINYSTATE(oc/c++/ocaRect,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ocaRect_(
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


ocaRect_::ocaRect_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
ocaRect_::compute()
{
	ocShape::ensure_init();
	int na = ( args != 0 ) ? args->length() : 0;
	double w = ( na > 0 ) ? (*args)[0]->get_flt() : 1.0;
	double h = ( na > 1 ) ? (*args)[1]->get_flt() : 1.0;
	if ( !(w > 0) || !(h > 0) ) { result = oca_err(thNEW(stdString,("rect: width and height must be > 0"))); return; }
	try {
		BRepBuilderAPI_MakePolygon poly;
		poly.Add(gp_Pnt(0, 0, 0));
		poly.Add(gp_Pnt(w, 0, 0));
		poly.Add(gp_Pnt(w, h, 0));
		poly.Add(gp_Pnt(0, h, 0));
		poly.Close();
		if ( ! poly.IsDone() ) { result = oca_err(thNEW(stdString,("rect: could not build the outline"))); return; }
		BRepBuilderAPI_MakeFace mf(poly.Wire());
		if ( ! mf.IsDone() ) { result = oca_err(thNEW(stdString,("rect: could not build the face"))); return; }
		out = thNEW(ocFace2D,());
		out->set_shape(mf.Face());
	} catch ( const Standard_Failure& e ) {
		std::string m = std::string("rect: OCCT failed [") + e.DynamicType()->Name() + "] (" +
		    ( ( e.GetMessageString() && e.GetMessageString()[0] ) ? e.GetMessageString() : "no message" ) + ")";
		result = oca_err(thNEW(stdString,(m.c_str())));
		return;
	}
}

/* この演算の結果。エラー時は compute() が result にエラー値を残して本体未設定で return するので
 * result 優先。 */
sPtr<pigData>
ocaRect_::get_result()
{
	return ( result != thNULL ) ? result : sPtr<pigData>::d_cast(out);
}
