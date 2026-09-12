/*
 * ocaIcosphere — icosphere(r, subdiv) — 測地球 (正二十面体を細分) の計算本体 (occt 版・#3474)。
 * ★ **測地多面体**なので occt でも平面 Face を縫えば厳密に一致する。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"oc/c++/ocShape.h"
#include	<BRepBuilderAPI_MakePolygon.hxx>
#include	<BRepBuilderAPI_MakeFace.hxx>
#include	<BRepBuilderAPI_Sewing.hxx>
#include	<BRepBuilderAPI_MakeSolid.hxx>
#include	<TopoDS.hxx>
#include	<TopoDS_Shape.hxx>
#include	<TopoDS_Face.hxx>
#include	<TopoDS_Shell.hxx>
#include	<TopoDS_Solid.hxx>
#include	<TopoDS_Wire.hxx>
#include	<TopAbs_ShapeEnum.hxx>
#include	<gp_Pnt.hxx>
#include	<Standard_Failure.hxx>
#include	<vector>
#include	<cmath>
#include	<string>
#include	"common/geodesic.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ocaIcosphere_.h"

CLASS_TINYSTATE(oc/c++/ocaIcosphere,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ocaIcosphere_(
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


ocaIcosphere_::ocaIcosphere_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
ocaIcosphere_::compute()
{
	ocShape::ensure_init();
	int na = ( args != 0 ) ? args->length() : 0;
	double r      = ( na > 0 ) ? (*args)[0]->get_flt() : 1.0;
	int    subdiv = ( na > 1 ) ? (int)(*args)[1]->get_int() : 0;   /* 細分回数。0=20 面 */
	int    n      = srava_geo::subdiv_to_n(subdiv);
	if ( !(r > 0) ) { result = oca_err(thNEW(stdString,("icosphere: radius must be > 0"))); return; }
	try {
		/* ★ icosphere は **測地多面体** (平面の三角形の集まり) であって近似球ではない。
		 *   occt でも平面 Face を縫えば **厳密に**同じ立体になる (sphere と違って
		 *   一致検査の表に入れられる)。 */
		struct OcSink {
			std::vector<gp_Pnt> v;
			std::vector<int>    t;
			int  add_vertex(double x, double y, double z) { v.push_back(gp_Pnt(x,y,z)); return (int)v.size()-1; }
			void add_triangle(int a, int b, int c) { t.push_back(a); t.push_back(b); t.push_back(c); }
		} sink;
		srava_geo::make_geodesic(srava_geo::SEED_ICOSAHEDRON, n, r, sink);
		BRepBuilderAPI_Sewing sew(1.0e-7);
		for ( size_t i = 0 ; i + 2 < sink.t.size() + 1 && i < sink.t.size() ; i += 3 ) {
			BRepBuilderAPI_MakePolygon tri(sink.v[sink.t[i]], sink.v[sink.t[i+1]],
			                               sink.v[sink.t[i+2]], Standard_True);
			BRepBuilderAPI_MakeFace f(tri.Wire());
			if ( ! f.IsDone() ) { result = oca_err(thNEW(stdString,("icosphere: could not build a face"))); return; }
			sew.Add(f.Face());
		}
		sew.Perform();
		TopoDS_Shape sh = sew.SewedShape();
		if ( sh.IsNull() || sh.ShapeType() != TopAbs_SHELL ) { result = oca_err(thNEW(stdString,("icosphere: the faces did not sew into a closed shell"))); return; }
		BRepBuilderAPI_MakeSolid ms(TopoDS::Shell(sh));
		if ( ! ms.IsDone() ) { result = oca_err(thNEW(stdString,("icosphere: the shell could not be closed into a solid"))); return; }
		out = thNEW(ocShape,());
		out->set_shape(ms.Solid());
	} catch ( const Standard_Failure& e ) {
		std::string m = std::string("icosphere: OCCT failed [") + e.DynamicType()->Name() + "] (" +
		    ( ( e.GetMessageString() && e.GetMessageString()[0] ) ? e.GetMessageString() : "no message" ) + ")";
		result = oca_err(thNEW(stdString,(m.c_str())));
		return;
	}
}

/* この演算の結果。エラー時は compute() が result にエラー値を残して本体未設定で return するので
 * result 優先。保存 (Writer 起動) は agent が出力 pigDataCache の set_body 経由で行う。 */
sPtr<pigData>
ocaIcosphere_::get_result()
{
	return ( result != thNULL ) ? result : sPtr<pigData>::d_cast(out);
}
