/*
 * ocaRevolve — revolve(cross2d, angle[, segs]) (#3471)。2D 領域 (oc-face3d) を Y 軸まわりに
 *   angle 度だけ回して回転体にする。
 * ★ BRepPrimAPI_MakeRevol。回転面は **厳密な回転面** (球・円錐・トーラス等) になる。
 * ⚠ **segs は無視する**。回転面が厳密に作られるので分割数に意味が無い
 *   (occt の sphere が seg を無視するのと同じ扱い)。互換のため受け取るだけ。
 * ⚠ 断面が回転軸をまたぐと自己交差する。OCCT が失敗するのでそのまま明示エラーにする。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"oc/c++/ocShape.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ocaRevolve_.h"

#include	<BRepPrimAPI_MakeRevol.hxx>
#include	<BRepGProp.hxx>
#include	<GProp_GProps.hxx>
#include	<math.h>
#include	<gp_Ax1.hxx>
#include	<gp_Pnt.hxx>
#include	<gp_Dir.hxx>
#include	<BRepBuilderAPI_MakeFace.hxx>
#include	<TopoDS_Shape.hxx>
#include	<TopExp_Explorer.hxx>
#include	<TopoDS.hxx>
#include	<BRep_Builder.hxx>
#include	<TopoDS_Compound.hxx>
#include	<gp_Vec.hxx>
#include	<Standard_Failure.hxx>
#include	<string>

CLASS_TINYSTATE(oc/c++/ocaRevolve,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ocaRevolve_(
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

ocaRevolve_::ocaRevolve_(TS_ARGS0)
	: ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
	TS_CPARGS0
}

void
ocaRevolve_::compute()
{
	ocShape::ensure_init();
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<ocFace2D> in = ( na > 0 ) ? sPtr<ocFace2D>::d_cast((*args)[0]) : sPtr<ocFace2D>();
	if ( ! in.is_notNull() ) {
		result = oca_err(thNEW(stdString,("revolve: input must be a 2D region (oc-face3d)")));
		return;
	}
	double deg = ( na > 1 ) ? (*args)[1]->get_flt() : 360.0;
	/* ★ #3570 段3: segs の引数そのものを撤去した (記述子の nin を減らした) ので、
	 *   ここに在った #3530 の「受けるが無視し、検査はする」は **届かなくなった**。
	 *   原則が「そのモジュールで必要のない引数は撤去する」に変わったため。 */
	if ( !(deg > 0) || deg > 360.0 ) {
		result = oca_err(thNEW(stdString,("revolve: angle must be in (0, 360] degrees")));
		return;
	}
	double rad = deg * 3.14159265358979323846 / 180.0;
	try {
		/* ★ 回転軸は **Y 軸** (原点まわり)。cgal / manifold の revolve と同じ規約に揃える。 */
		gp_Ax1 axis(gp_Pnt(0, 0, 0), gp_Dir(0, 1, 0));
		BRep_Builder bb;
		TopoDS_Compound comp;
		bb.MakeCompound(comp);
		int n = 0;
		for ( TopExp_Explorer e(in->shape(), TopAbs_FACE) ; e.More() ; e.Next() ) {
			BRepPrimAPI_MakeRevol mk(TopoDS::Face(e.Current()), axis, rad);
			TopoDS_Shape s = mk.Shape();
			if ( s.IsNull() ) continue;
			bb.Add(comp, s);
			++n;
		}
		if ( n == 0 ) {
			result = oca_err(thNEW(stdString,(
			    "revolve: the 2D region has no face to revolve")));
			return;
		}
		/* ★★ #3518 の 5: extrude と同じ検査。回転でも「掃引が単調でない」= 断面が軸に
		 *   またがっていると、境界からのフラックスが打ち消して 0 になる。
		 *   ★ 閾値は **面積 x 重心の回る距離 (パップスの定理の体積)** に対する相対。 */
		{
			GProp_GProps gp;
			BRepGProp::VolumeProperties(comp, gp);
			double vol = gp.Mass();
			GProp_GProps fa;
			BRepGProp::SurfaceProperties(in->shape(), fa);
			gp_Pnt c = fa.CentreOfMass();
			/* 軸は Y 軸なので重心の回転半径は sqrt(x^2 + z^2)。 */
			double r = ::sqrt(c.X()*c.X() + c.Z()*c.Z());
			double scale = fa.Mass() * r * rad;   /* パップス = 面積 x 2πr x (θ/2π) */
			if ( scale <= 0.0 ) scale = 1.0;
			if ( ( ( vol < 0 ) ? -vol : vol ) <= 1e-9 * scale ) {
				result = oca_err(thNEW(stdString,(
				    "revolve: the 2D region straddles the axis of revolution, so the solid has "
				    "no well-defined inside (its signed volume cancels to 0); move the region "
				    "off the axis")));
				return;
			}
		}
		out = thNEW(ocShape,());
		out->set_shape(comp);
	} catch ( const Standard_Failure& e ) {
		std::string m = std::string("revolve: OCCT failed [") + e.DynamicType()->Name() + "] (" +
		    ( ( e.GetMessageString() && e.GetMessageString()[0] ) ? e.GetMessageString() : "no message" ) + ")";
		result = oca_err(thNEW(stdString,(m.c_str())));
		return;
	}
}

sPtr<pigData>
ocaRevolve_::get_result()
{
	return ( result != thNULL ) ? result : sPtr<pigData>::d_cast(out);
}
