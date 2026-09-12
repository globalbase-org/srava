/*
 * ocaPyramid — pyramid(n, h, r) — 正 n 角錐 (底面は z=0 の XY 平面・頂点は z=h) の計算本体 (occt 版・#3474)。
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
#include	"_ts2/c++/ocaPyramid_.h"

CLASS_TINYSTATE(oc/c++/ocaPyramid,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ocaPyramid_(
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


ocaPyramid_::ocaPyramid_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
ocaPyramid_::compute()
{
	ocShape::ensure_init();
	int na = ( args != 0 ) ? args->length() : 0;
	int    n = ( na > 0 ) ? (int)(*args)[0]->get_int() : 3;
	double h = ( na > 1 ) ? (*args)[1]->get_flt() : 1.0;
	double r = ( na > 2 ) ? (*args)[2]->get_flt() : 1.0;
	if ( !(n >= 3) ) { result = oca_err(thNEW(stdString,("pyramid: n must be >= 3"))); return; }
	if ( !(h > 0) ) { result = oca_err(thNEW(stdString,("pyramid: height must be > 0"))); return; }
	if ( !(r > 0) ) { result = oca_err(thNEW(stdString,("pyramid: radius must be > 0"))); return; }
	try {
		/* ★ 平面 n+1 枚でできる **多面体**なのでメッシュ系と厳密に一致する (prism と同じ理由)。
		 *   底面は z=0 の正 n 角形・頂点は (0,0,h)。common/solids.h の make_pyramid と同じ座標。 */
		std::vector<gp_Pnt> ring;
		for ( int k = 0 ; k < n ; ++k ) {
			double a = 2.0 * srava_geo::SOLID_PI * (double)k / (double)n;
			ring.push_back(gp_Pnt(r * std::cos(a), r * std::sin(a), 0.0));
		}
		gp_Pnt apex(0, 0, h);
		BRepBuilderAPI_Sewing sew(1.0e-7);
		for ( int k = 0 ; k < n ; ++k ) {          /* 側面 */
			BRepBuilderAPI_MakePolygon tri(ring[k], ring[(k + 1) % n], apex, Standard_True);
			BRepBuilderAPI_MakeFace f(tri.Wire());
			if ( ! f.IsDone() ) { result = oca_err(thNEW(stdString,("pyramid: could not build a side face"))); return; }
			sew.Add(f.Face());
		}
		BRepBuilderAPI_MakePolygon base;           /* 底面 */
		for ( int k = 0 ; k < n ; ++k ) base.Add(ring[k]);
		base.Close();
		BRepBuilderAPI_MakeFace bf(base.Wire());
		if ( ! bf.IsDone() ) { result = oca_err(thNEW(stdString,("pyramid: could not build the base face"))); return; }
		sew.Add(bf.Face());
		sew.Perform();
		TopoDS_Shape sh = sew.SewedShape();
		if ( sh.IsNull() || sh.ShapeType() != TopAbs_SHELL ) { result = oca_err(thNEW(stdString,("pyramid: the faces did not sew into a closed shell"))); return; }
		BRepBuilderAPI_MakeSolid ms(TopoDS::Shell(sh));
		if ( ! ms.IsDone() ) { result = oca_err(thNEW(stdString,("pyramid: the shell could not be closed into a solid"))); return; }
		out = thNEW(ocShape,());
		out->set_shape(ms.Solid());
	} catch ( const Standard_Failure& e ) {
		std::string m = std::string("pyramid: OCCT failed [") + e.DynamicType()->Name() + "] (" +
		    ( ( e.GetMessageString() && e.GetMessageString()[0] ) ? e.GetMessageString() : "no message" ) + ")";
		result = oca_err(thNEW(stdString,(m.c_str())));
		return;
	}
}

/* この演算の結果。エラー時は compute() が result にエラー値を残して本体未設定で return するので
 * result 優先。保存 (Writer 起動) は agent が出力 pigDataCache の set_body 経由で行う。 */
sPtr<pigData>
ocaPyramid_::get_result()
{
	return ( result != thNULL ) ? result : sPtr<pigData>::d_cast(out);
}
