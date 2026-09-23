/*
 * ocaPoles — poles(face) — 自由曲面の **制御網**を格子として取り出す (#3532)。
 *
 * ★★ 用途は「通過点で当てて、制御点で整える」を **既存 op の合成**で書けるようにすること。
 *       S = surface_through(格子);        // 点を通る面を当てる
 *       P = poles(S);                     // その制御網を見る
 *       T = set_poles(S, 直した格子);     // 手で整えた網に差し替える
 *   ⇒ 「通過点と制御点を 1 つの op で混在させる」(制約付き最小二乗) を持たずに済む。
 *     混在は定式化としては成立するが OCCT に口が無く、op というより研究項目の規模になる。
 *
 * ⚠ 解析曲面 (平面・球・円柱…) は制御網を持たない。⇒ 明示エラーにして surface_type を案内する。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"oc/c++/ocShape.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ocaPoles_.h"

#include	<BRep_Tool.hxx>
#include	<Geom_BSplineSurface.hxx>
#include	<Geom_BezierSurface.hxx>
#include	<TopExp_Explorer.hxx>
#include	<TopoDS.hxx>
#include	<TopoDS_Face.hxx>
#include	<TopLoc_Location.hxx>
#include	<gp_Pnt.hxx>
#include	<Standard_Failure.hxx>
#include	<string>
#include	<cstdio>

CLASS_TINYSTATE(oc/c++/ocaPoles,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ocaPoles_(
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


ocaPoles_::ocaPoles_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

/* 面 1 枚を取り出す。0 枚 / 2 枚以上は **数を言って**断る。 */
static int
oc_single_face(const TopoDS_Shape &s, TopoDS_Face *out, int *nfound)
{
	int n = 0;
	for ( TopExp_Explorer e(s, TopAbs_FACE) ; e.More() ; e.Next() ) {
		if ( n == 0 ) *out = TopoDS::Face(e.Current());
		++n;
	}
	*nfound = n;
	return ( n == 1 );
}

void
ocaPoles_::compute()
{
	ocShape::ensure_init();
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<ocFace2D> in = ( na > 0 ) ? sPtr<ocFace2D>::d_cast((*args)[0]) : sPtr<ocFace2D>();
	if ( ! in.is_notNull() || in->shape().IsNull() ) {
		result = oca_err(thNEW(stdString,("poles: input must be a 2D region (oc-face3d)")));
		return;
	}
	try {
		TopoDS_Face f; int nf = 0;
		if ( ! oc_single_face(in->shape(), &f, &nf) ) {
			char m[160]; snprintf(m, sizeof m, "poles: needs exactly 1 face (found %d)", nf);
			result = oca_err(thNEW(stdString,(m))); return;
		}
		TopLoc_Location loc;
		Handle(Geom_Surface) s = BRep_Tool::Surface(f, loc);
		Handle(Geom_BSplineSurface) bs = Handle(Geom_BSplineSurface)::DownCast(s);
		Handle(Geom_BezierSurface)  bz = Handle(Geom_BezierSurface)::DownCast(s);
		if ( bs.IsNull() && bz.IsNull() ) {
			result = oca_err(thNEW(stdString,(
			    "poles: the surface has no control net (only bezier / bspline do — see surface_type())")));
			return;
		}
		const int nu = bs.IsNull() ? bz->NbUPoles() : bs->NbUPoles();
		const int nv = bs.IsNull() ? bz->NbVPoles() : bs->NbVPoles();
		sPtr<pigDataArray> rows = thNEW(pigDataArray,());
		for ( int i = 1 ; i <= nu ; ++i ) {
			sPtr<pigDataArray> row = thNEW(pigDataArray,());
			for ( int j = 1 ; j <= nv ; ++j ) {
				gp_Pnt p = bs.IsNull() ? bz->Pole(i, j) : bs->Pole(i, j);
				if ( ! loc.IsIdentity() ) p.Transform(loc.Transformation());   /* ★ 面の位置を効かせる */
				sPtr<pigDataArray> xyz = thNEW(pigDataArray,());
				xyz->push(thNEW(pigDataFloat,(p.X())));
				xyz->push(thNEW(pigDataFloat,(p.Y())));
				xyz->push(thNEW(pigDataFloat,(p.Z())));
				row->push(xyz);
			}
			rows->push(row);
		}
		result = rows;
	} catch ( const Standard_Failure& e ) {
		std::string m = std::string("poles: OCCT failed [") + e.DynamicType()->Name() + "] (" +
		    ( ( e.GetMessageString() && e.GetMessageString()[0] ) ? e.GetMessageString() : "no message" ) + ")";
		result = oca_err(thNEW(stdString,(m.c_str())));
		return;
	}
}

sPtr<pigData>
ocaPoles_::get_result()
{
	return result;
}
