/*
 * ocaPolygon — polygon([[x,y],...]) — 2D 明示点列 (occt 版・#3474)。
 * ★ 連続する重複点は間引く (曲線を concat した継ぎ目で必ず出る零長エッジ。残すと
 *   OCCT の Wire 構築が落ちる。cgal の cgaPolygon と同じ扱い)。
 * ★ 平面の折れ線なのでメッシュ系と厳密に一致する。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"oc/c++/ocShape.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ocaPolygon_.h"

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

CLASS_TINYSTATE(oc/c++/ocaPolygon,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ocaPolygon_(
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


ocaPolygon_::ocaPolygon_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
ocaPolygon_::compute()
{
	ocShape::ensure_init();
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<pigDataArray> pts = ( na > 0 ) ? (*args)[0]->obt_array() : sPtr<pigDataArray>();
	int np = pts.is_notNull() ? pts->length() : 0;
	if ( np < 3 ) { result = oca_err(thNEW(stdString,("polygon: needs >= 3 points [[x,y],...]"))); return; }
	try {
		std::vector<gp_Pnt> v;
		for ( int i = 0 ; i < np ; ++i ) {
			sPtr<pigDataArray> xy = pts->get_ix(thNEW(pigDataInteger,((INTEGER64)i)))->obt_array();
			if ( ! xy.is_notNull() || xy->length() < 2 ) { result = oca_err(thNEW(stdString,("polygon: each point must be [x,y]"))); return; }
			double x = xy->get_ix(thNEW(pigDataInteger,((INTEGER64)0)))->get_flt();
			double y = xy->get_ix(thNEW(pigDataInteger,((INTEGER64)1)))->get_flt();
			if ( ! v.empty() && std::fabs(v.back().X() - x) < 1e-12
			                && std::fabs(v.back().Y() - y) < 1e-12 ) continue;   /* 零長エッジを間引く */
			v.push_back(gp_Pnt(x, y, 0.0));
		}
		if ( v.size() > 1 && std::fabs(v.front().X() - v.back().X()) < 1e-12
		                  && std::fabs(v.front().Y() - v.back().Y()) < 1e-12 ) v.pop_back();
		if ( v.size() < 3 ) { result = oca_err(thNEW(stdString,("polygon: needs >= 3 distinct points"))); return; }
		BRepBuilderAPI_MakePolygon poly;
		for ( size_t i = 0 ; i < v.size() ; ++i ) poly.Add(v[i]);
		poly.Close();
		if ( ! poly.IsDone() ) { result = oca_err(thNEW(stdString,("polygon: could not build the outline"))); return; }
		BRepBuilderAPI_MakeFace mf(poly.Wire());
		if ( ! mf.IsDone() ) { result = oca_err(thNEW(stdString,("polygon: could not build the face"))); return; }
		out = thNEW(ocFace2D,());
		out->set_shape(mf.Face());
	} catch ( const Standard_Failure& e ) {
		std::string m = std::string("polygon: OCCT failed [") + e.DynamicType()->Name() + "] (" +
		    ( ( e.GetMessageString() && e.GetMessageString()[0] ) ? e.GetMessageString() : "no message" ) + ")";
		result = oca_err(thNEW(stdString,(m.c_str())));
		return;
	}
}

/* この演算の結果。エラー時は compute() が result にエラー値を残して本体未設定で return するので
 * result 優先。 */
sPtr<pigData>
ocaPolygon_::get_result()
{
	return ( result != thNULL ) ? result : sPtr<pigData>::d_cast(out);
}
