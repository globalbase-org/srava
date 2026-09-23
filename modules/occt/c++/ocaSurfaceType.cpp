/*
 * ocaSurfaceType — surface_type(cross2d) (#3518 の 6)。2D 領域が **どんな曲面の上にあるか**を
 *                  文字列で返す。
 *
 * ---- ★★ なぜ要るのか ----
 * face / face_at (#3518 の 2) で立体から面を取り出せるようになった結果、oc-face3d には
 * **平面でないもの**が普通に入るようになった。ところが利用者には、手元の 2D が
 * 「cast できるのか / polygonize できるのか / extrude して意味があるのか」を
 * **踏む前に判断する手段が無かった** (どれも平面を前提にしている)。
 * ⇒ 踏んでエラーを読むのではなく、**訊けるようにする**。
 *
 * 返りは OCCT の曲面種の名前:
 *     "plane" / "cylinder" / "cone" / "sphere" / "torus" /
 *     "bezier" / "bspline" / "revolution" / "extrusion" / "offset" / "other"
 * ⚠ 領域が **複数の面**を持ち、種類が揃っていないときは "mixed"。
 *   (ブールで切ると継ぎ目で割れて複数面になる — #3518 の④で実測。面数は nfaces で訊ける。)
 * ⚠ 空の領域 (面が 1 枚も無い) は "empty"。★ 0 面はエラーではない (交わらない ∩ の答え)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"oc/c++/ocShape.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ocaSurfaceType_.h"

#include	<TopExp_Explorer.hxx>
#include	<TopoDS.hxx>
#include	<TopoDS_Face.hxx>
#include	<BRepAdaptor_Surface.hxx>
#include	<Standard_Failure.hxx>
#include	<string.h>
#include	<string>

CLASS_TINYSTATE(oc/c++/ocaSurfaceType,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ocaSurfaceType_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

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


ocaSurfaceType_::ocaSurfaceType_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

static const char*
oc_surface_name(const TopoDS_Face &f)
{
	switch ( BRepAdaptor_Surface(f).GetType() ) {
	case GeomAbs_Plane:              return "plane";
	case GeomAbs_Cylinder:           return "cylinder";
	case GeomAbs_Cone:               return "cone";
	case GeomAbs_Sphere:             return "sphere";
	case GeomAbs_Torus:              return "torus";
	case GeomAbs_BezierSurface:      return "bezier";
	case GeomAbs_BSplineSurface:     return "bspline";
	case GeomAbs_SurfaceOfRevolution:return "revolution";
	case GeomAbs_SurfaceOfExtrusion: return "extrusion";
	case GeomAbs_OffsetSurface:      return "offset";
	default:                         return "other";
	}
}

void
ocaSurfaceType_::compute()
{
	ocShape::ensure_init();   /* ★ OCCT の診断出力を stdout から外す */
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<ocFace2D> in = ( na > 0 ) ? sPtr<ocFace2D>::d_cast((*args)[0]) : sPtr<ocFace2D>();
	if ( ! in.is_notNull() || in->shape().IsNull() ) {
		result = oca_err(thNEW(stdString,(
		    "surface_type: input must be a 2D region (oc-face3d)")));
		return;
	}
	try {
		const char *t = 0;
		for ( TopExp_Explorer e(in->shape(), TopAbs_FACE) ; e.More() ; e.Next() ) {
			const char *u = oc_surface_name(TopoDS::Face(e.Current()));
			if ( t == 0 )                    t = u;
			else if ( ::strcmp(t, u) != 0 ) { t = "mixed"; break; }
		}
		result = thNEW(pigDataString,(( t != 0 ) ? t : "empty"));
	} catch ( const Standard_Failure& e ) {
		std::string m = std::string("surface_type: OCCT failed [") + e.DynamicType()->Name() + "] (" +
		    ( ( e.GetMessageString() && e.GetMessageString()[0] ) ? e.GetMessageString() : "no message" ) + ")";
		result = oca_err(thNEW(stdString,(m.c_str())));
	}
}

sPtr<pigData>
ocaSurfaceType_::get_result()
{
	return result;
}
