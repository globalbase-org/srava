/*
 * ocaTetrahedron — tetrahedron(r) — 正四面体 (原点中心・外接球半径 r) の計算本体 (occt 版・#3474)。
 * ★ 平面多面体なので **メッシュ系と厳密に一致する** (prism / box と同じ)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"oc/c++/ocShape.h"
#include	"common/solids.h"   /* SOLID_PI (座標規約を共通ヘッダと共有) */

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
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ocaTetrahedron_.h"

CLASS_TINYSTATE(oc/c++/ocaTetrahedron,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ocaTetrahedron_(
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


ocaTetrahedron_::ocaTetrahedron_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
ocaTetrahedron_::compute()
{
	ocShape::ensure_init();
	int na = ( args != 0 ) ? args->length() : 0;
	double r = ( na > 0 ) ? (*args)[0]->get_flt() : 1.0;
	if ( !(r > 0) ) { result = oca_err(thNEW(stdString,("tetrahedron: circumradius must be > 0"))); return; }
	try {
		/* ★ 正四面体も **平面 4 枚**なのでメッシュ系と厳密に一致する。
		 *   common/solids.h の make_tetrahedron と同じ座標 (立方体の対角 4 頂点 × r/√3)。 */
		const double s = r / std::sqrt(3.0);
		gp_Pnt v[4] = { gp_Pnt( s,  s,  s), gp_Pnt( s, -s, -s),
		                gp_Pnt(-s,  s, -s), gp_Pnt(-s, -s,  s) };
		static const int F[4][3] = { {0,1,2}, {0,2,3}, {0,3,1}, {1,3,2} };
		BRepBuilderAPI_Sewing sew(1.0e-7);
		for ( int k = 0 ; k < 4 ; ++k ) {
			BRepBuilderAPI_MakePolygon tri(v[F[k][0]], v[F[k][1]], v[F[k][2]], Standard_True);
			BRepBuilderAPI_MakeFace f(tri.Wire());
			if ( ! f.IsDone() ) { result = oca_err(thNEW(stdString,("tetrahedron: could not build a face"))); return; }
			sew.Add(f.Face());
		}
		sew.Perform();
		TopoDS_Shape sh = sew.SewedShape();
		if ( sh.IsNull() || sh.ShapeType() != TopAbs_SHELL ) { result = oca_err(thNEW(stdString,("tetrahedron: the faces did not sew into a closed shell"))); return; }
		BRepBuilderAPI_MakeSolid ms(TopoDS::Shell(sh));
		if ( ! ms.IsDone() ) { result = oca_err(thNEW(stdString,("tetrahedron: the shell could not be closed into a solid"))); return; }
		out = thNEW(ocShape,());
		out->set_shape(ms.Solid());
	} catch ( const Standard_Failure& e ) {
		std::string m = std::string("tetrahedron: OCCT failed [") + e.DynamicType()->Name() + "] (" +
		    ( ( e.GetMessageString() && e.GetMessageString()[0] ) ? e.GetMessageString() : "no message" ) + ")";
		result = oca_err(thNEW(stdString,(m.c_str())));
		return;
	}
}

/* この演算の結果。エラー時は compute() が result にエラー値を残して本体未設定で return するので
 * result 優先。保存 (Writer 起動) は agent が出力 pigDataCache の set_body 経由で行う。 */
sPtr<pigData>
ocaTetrahedron_::get_result()
{
	return ( result != thNULL ) ? result : sPtr<pigData>::d_cast(out);
}
