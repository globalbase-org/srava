/*
 * ocaPrism — prism(n, h, r) (#3471)。正 n 角柱。★ **プリミティブ (leaf)** で 2D→3D op ではない
 *   (cgal の cgaPrism / manifold の mfaPrism と同じ位置づけ。extrude/revolve とは別物)。
 * ★ r は **外接円半径** (cgal の cgaPrism と同じ定義に揃える。ここがずれると体積が合わない)。
 * ★ 平面 n+2 枚でできるので **メッシュ系と厳密に一致する** — occt の sphere や tube と違い、
 *   カーネル一致の表に入れられる (box と同じ理由)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"oc/c++/ocShape.h"
#include	"ts2/c++/stdString.h"
#include	"common/segs.h"   /* ★ #3530: segs / n の共通検査 */
#include	"_ts2/c++/ocaPrism_.h"

#include	<BRepBuilderAPI_MakePolygon.hxx>
#include	<BRepBuilderAPI_MakeFace.hxx>
#include	<BRepPrimAPI_MakePrism.hxx>
#include	<TopoDS_Shape.hxx>
#include	<gp_Pnt.hxx>
#include	<gp_Vec.hxx>
#include	<Standard_Failure.hxx>
#include	<cmath>
#include	<string>

CLASS_TINYSTATE(oc/c++/ocaPrism,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ocaPrism_(
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

ocaPrism_::ocaPrism_(TS_ARGS0)
	: ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
	TS_CPARGS0
}

void
ocaPrism_::compute()
{
	ocShape::ensure_init();
	int na = ( args != 0 ) ? args->length() : 0;
	int    n = ( na > 0 ) ? (int)(*args)[0]->get_int() : 6;
	double h = ( na > 1 ) ? (*args)[1]->get_flt() : 1.0;
	double r = ( na > 2 ) ? (*args)[2]->get_flt() : 1.0;
	if ( srava_geo::check_sides(n, &n) != srava_geo::SEGS_OK ) {   /* ★ #3530: n は形そのもの = 既定値なし */
		result = oca_err(thNEW(stdString,(srava_geo::sides_error("prism").c_str()))); return; }
	if ( !(h > 0) ) { result = oca_err(thNEW(stdString,("prism: height must be > 0"))); return; }
	if ( !(r > 0) ) { result = oca_err(thNEW(stdString,("prism: radius must be > 0"))); return; }
	try {
		/* ★ #3474 (2026-09-05 ひさ指示): 底面 z=0・天面 z=h。
		 *   ⚠ ここは以前 **原点中心 (z = -h/2 〜 +h/2)** だった = cgal / manifold の prism
		 *   (z = 0 〜 h) との **カーネル間不一致**。prism(n,h,r) ≡ extrude(ngon(n,r), h) は
		 *   モジュールリファレンスにも書かれた規約なので、occt 側を揃える。
		 *   ★ 体積は平行移動で変わらないので kernel_agree (体積比較) では検出できなかった。
		 *   位置まで見るには boolean を挟む必要がある (下の agree モデル prism_cut がそれ)。 */
		BRepBuilderAPI_MakePolygon poly;
		for ( int k = 0 ; k < n ; ++k ) {
			double a = 2.0 * 3.14159265358979323846 * (double)k / (double)n;
			poly.Add(gp_Pnt(r * std::cos(a), r * std::sin(a), 0.0));
		}
		poly.Close();
		if ( ! poly.IsDone() ) { result = oca_err(thNEW(stdString,("prism: could not build the base polygon"))); return; }
		BRepBuilderAPI_MakeFace mf(poly.Wire());
		if ( ! mf.IsDone() ) { result = oca_err(thNEW(stdString,("prism: could not build the base face"))); return; }
		BRepPrimAPI_MakePrism mk(mf.Face(), gp_Vec(0, 0, h));
		TopoDS_Shape s = mk.Shape();
		if ( s.IsNull() ) { result = oca_err(thNEW(stdString,("prism: OCCT produced a null shape"))); return; }
		out = thNEW(ocShape,());
		out->set_shape(s);
	} catch ( const Standard_Failure& e ) {
		std::string m = std::string("prism: OCCT failed [") + e.DynamicType()->Name() + "] (" +
		    ( ( e.GetMessageString() && e.GetMessageString()[0] ) ? e.GetMessageString() : "no message" ) + ")";
		result = oca_err(thNEW(stdString,(m.c_str())));
		return;
	}
}

sPtr<pigData>
ocaPrism_::get_result()
{
	return ( result != thNULL ) ? result : sPtr<pigData>::d_cast(out);
}
