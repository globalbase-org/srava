/*
 * ocaExtrude — extrude(cross2d, h) (#3471)。2D 領域 (oc-face3d) を +Z 方向へ h だけ押し出す。
 * ★ BRepPrimAPI_MakePrism。輪郭が Bezier / B-spline のままなので、**側面は平面の帯ではなく
 *   厳密な押し出し面**になる。ここが「文字を解析曲面のまま立体にする」の実体。
 * ⚠ 同名の extrude が cgal / manifold にもあるが入力型が違う ((cg-cross2d) / (mf-cross2d)) ので、
 *   sig ディスパッチで自然に分かれる。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"oc/c++/ocShape.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ocaExtrude_.h"

#include	<BRepPrimAPI_MakePrism.hxx>
#include	<BRepGProp.hxx>
#include	<GProp_GProps.hxx>
#include	<BRepBuilderAPI_MakeFace.hxx>
#include	<TopoDS_Shape.hxx>
#include	<TopExp_Explorer.hxx>
#include	<TopoDS.hxx>
#include	<BRep_Builder.hxx>
#include	<TopoDS_Compound.hxx>
#include	<gp_Vec.hxx>
#include	<Standard_Failure.hxx>
#include	<string>

CLASS_TINYSTATE(oc/c++/ocaExtrude,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ocaExtrude_(
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

ocaExtrude_::ocaExtrude_(TS_ARGS0)
	: ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
	TS_CPARGS0
}

void
ocaExtrude_::compute()
{
	ocShape::ensure_init();
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<ocFace2D> in = ( na > 0 ) ? sPtr<ocFace2D>::d_cast((*args)[0]) : sPtr<ocFace2D>();
	if ( ! in.is_notNull() ) {
		result = oca_err(thNEW(stdString,("extrude: input must be a 2D region (oc-face3d)")));
		return;
	}
	double h = ( na > 1 ) ? (*args)[1]->get_flt() : 1.0;
	if ( h == 0.0 ) {
		result = oca_err(thNEW(stdString,("extrude: height must not be 0")));
		return;
	}
	try {
		/* ★ 入力は Face の Compound (text は字ごとに Face を持つ)。Face ごとに押し出して束ねる。
		 *   ⚠ Compound をそのまま MakePrism に渡すと Solid にならないことがあるので Face 単位で。 */
		BRep_Builder bb;
		TopoDS_Compound comp;
		bb.MakeCompound(comp);
		int n = 0;
		for ( TopExp_Explorer e(in->shape(), TopAbs_FACE) ; e.More() ; e.Next() ) {
			BRepPrimAPI_MakePrism mk(TopoDS::Face(e.Current()), gp_Vec(0, 0, h));
			TopoDS_Shape s = mk.Shape();
			if ( s.IsNull() ) continue;
			bb.Add(comp, s);
			++n;
		}
		if ( n == 0 ) {
			result = oca_err(thNEW(stdString,(
			    "extrude: the 2D region has no face to extrude")));
			return;
		}
		/* ★★ #3518 の 5: **符号つき体積が 0 なら明示エラー**。
		 *   ⚠ 理由は「潰れている」ではなく *prism の前提 (掃引の単調性) が満たされていない*。
		 *     BRepPrimAPI_MakePrism は「底面 + 平行移動した天面 + 境界稜を掃いた側面」で
		 *     境界を組むので、掃引方向が面の中を向いていると底と天の一部が領域の内部に入り、
		 *     境界からのフラックス (= BRepGProp の体積) が厳密に打ち消し合って 0 になる。
		 *     ⇒ 領域そのものは体積を持つ (ミンコフスキー和) が、**この境界表現は間違っている**。
		 *   ★ 曲面とは無関係に元から在った穴 — 平面の面でも面内方向へ押し出せば 0 になる。
		 *   ⚠ 重い自己交差検査 (BOPAlgo_CheckerSI) は呼ばない (#3501 と同じ理由で
		 *     測定対象カーネルに無用な負荷を乗せない)。valid() を呼べば利用者が捕まえられる。
		 *   ★ 閾値は **面積 x 掃引長に対する相対** (絶対値だと寸法の単位で意味が変わる)。 */
		{
			GProp_GProps gp;
			BRepGProp::VolumeProperties(comp, gp);
			double vol = gp.Mass();
			double scale = in->area() * ( ( h < 0 ) ? -h : h );
			if ( scale <= 0.0 ) scale = 1.0;
			if ( ( ( vol < 0 ) ? -vol : vol ) <= 1e-9 * scale ) {
				result = oca_err(thNEW(stdString,(
				    "extrude: the sweep direction lies inside the 2D region, so the prism has "
				    "no well-defined inside (its signed volume cancels to 0); move the region "
				    "or the direction so the sweep leaves the surface")));
				return;
			}
		}
		out = thNEW(ocShape,());
		out->set_shape(comp);
	} catch ( const Standard_Failure& e ) {
		std::string m = std::string("extrude: OCCT failed [") + e.DynamicType()->Name() + "] (" +
		    ( ( e.GetMessageString() && e.GetMessageString()[0] ) ? e.GetMessageString() : "no message" ) + ")";
		result = oca_err(thNEW(stdString,(m.c_str())));
		return;
	}
}

sPtr<pigData>
ocaExtrude_::get_result()
{
	return ( result != thNULL ) ? result : sPtr<pigData>::d_cast(out);
}
