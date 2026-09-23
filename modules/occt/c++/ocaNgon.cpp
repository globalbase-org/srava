/*
 * ocaNgon — ngon(n, r) — 2D 正 n 角形 (occt 版・#3474)。
 * ★ **原点中心**・外接円半径 r・角度 2πk/n・+X 始点・CCW (cgal の cga_regular_polygon と同じ)。
 * ★ 平面の n 辺なのでメッシュ系と厳密に一致する。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"oc/c++/ocShape.h"
#include	"ts2/c++/stdString.h"
#include	"common/segs.h"   /* ★ #3530: segs / n の共通検査 */
#include	"_ts2/c++/ocaNgon_.h"

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

CLASS_TINYSTATE(oc/c++/ocaNgon,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ocaNgon_(
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


ocaNgon_::ocaNgon_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
ocaNgon_::compute()
{
	ocShape::ensure_init();
	int na = ( args != 0 ) ? args->length() : 0;
	int    n = ( na > 0 ) ? (int)(*args)[0]->get_int() : 3;
	double r = ( na > 1 ) ? (*args)[1]->get_flt() : 1.0;
	if ( srava_geo::check_sides(n, &n) != srava_geo::SEGS_OK ) {   /* ★ #3530: n は形そのもの = 既定値なし */
		result = oca_err(thNEW(stdString,(srava_geo::sides_error("ngon").c_str()))); return; }
	if ( !(r > 0) ) { result = oca_err(thNEW(stdString,("ngon: radius must be > 0"))); return; }
	try {
		BRepBuilderAPI_MakePolygon poly;
		for ( int k = 0 ; k < n ; ++k ) {
			double a = 2.0 * 3.14159265358979323846 * (double)k / (double)n;
			poly.Add(gp_Pnt(r * std::cos(a), r * std::sin(a), 0.0));
		}
		poly.Close();
		if ( ! poly.IsDone() ) { result = oca_err(thNEW(stdString,("ngon: could not build the outline"))); return; }
		BRepBuilderAPI_MakeFace mf(poly.Wire());
		if ( ! mf.IsDone() ) { result = oca_err(thNEW(stdString,("ngon: could not build the face"))); return; }
		out = thNEW(ocFace2D,());
		out->set_shape(mf.Face());
	} catch ( const Standard_Failure& e ) {
		std::string m = std::string("ngon: OCCT failed [") + e.DynamicType()->Name() + "] (" +
		    ( ( e.GetMessageString() && e.GetMessageString()[0] ) ? e.GetMessageString() : "no message" ) + ")";
		result = oca_err(thNEW(stdString,(m.c_str())));
		return;
	}
}

/* この演算の結果。エラー時は compute() が result にエラー値を残して本体未設定で return するので
 * result 優先。 */
sPtr<pigData>
ocaNgon_::get_result()
{
	return ( result != thNULL ) ? result : sPtr<pigData>::d_cast(out);
}
