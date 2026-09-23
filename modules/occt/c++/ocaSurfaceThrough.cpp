/*
 * ocaSurfaceThrough — surface_through(格子 [, mode]) — 格子を **通過点** として自由曲面を
 *   当てる (#3532)。OCCT @c GeomAPI_PointsToBSplineSurface。
 *
 * ★★ surface_control との違いは **点の意味**。ここでは面が点を通る。
 *   実測 (macMINI / OCCT 7.9.3 ・ 半径 1.5 の球冠を 5x9 で標本化):
 *       通過点・近似  与えた点との距離 max 5.0e-14 / 球からのずれ max 5.3e-4
 *       通過点・補間  与えた点との距離 max 4.6e-14 / 球からのずれ max 1.3e-3
 *   ⚠ 「必ず通る」を強制する補間のほうが **点と点の間で暴れる** (1.3e-3 対 5.3e-4)。
 *     ⇒ 既定は近似 ("fit")。厳密に通したいときだけ "interp" を書く。
 *
 * ★ 入力は 2 通りを **同じ op** で受ける (どちらも OCCT に口がある)。
 *       [[[x,y,z],...],...]   点の格子      → TColgp_Array2OfPnt
 *       [[z,z,...],...]       高さ場        → TColStd_Array2OfReal (x,y は等間隔)
 *   ⇒ 行の先頭要素が配列かどうかで判別する。**綴りが違えば別の物**なので黙って混ぜない
 *     (1 つの格子の中に点と数が混在していたらエラー)。
 *
 * ⚠ 値配列は op へ渡す段が O(N^1.9) (2026-09-13 実測)。実用上限は 2 万点 = 141x141 程度。
 *   手打ちの格子を想定した op なので実害は無いが、密な格子を渡す用途には向かない。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"oc/c++/ocShape.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ocaSurfaceThrough_.h"

#include	<BRepBuilderAPI_MakeFace.hxx>
#include	<GeomAPI_PointsToBSplineSurface.hxx>
#include	<Geom_BSplineSurface.hxx>
#include	<TColgp_Array2OfPnt.hxx>
#include	<TColStd_Array2OfReal.hxx>
#include	<TopoDS_Face.hxx>
#include	<Precision.hxx>
#include	<gp_Pnt.hxx>
#include	<Standard_Failure.hxx>
#include	<vector>
#include	<string>
#include	<cstring>
#include	<cstdio>

CLASS_TINYSTATE(oc/c++/ocaSurfaceThrough,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ocaSurfaceThrough_(
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


ocaSurfaceThrough_::ocaSurfaceThrough_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
ocaSurfaceThrough_::compute()
{
	ocShape::ensure_init();
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<pigDataArray> rows = ( na > 0 ) ? (*args)[0]->obt_array() : sPtr<pigDataArray>();
	int nu = rows.is_notNull() ? rows->length() : 0;
	if ( nu < 2 ) {
		result = oca_err(thNEW(stdString,("surface_through: needs a grid [[[x,y,z],...],...] or [[z,...],...] with >= 2 rows")));
		return;
	}
	/* ---- mode: 既定は近似。"interp" のときだけ厳密に通す ---- */
	bool interp = false;
	if ( na > 1 ) {
		sPtr<stdString> m = (*args)[1]->get_str();
		const char* s = m.is_notNull() ? m->get_str() : 0;
		if ( s != 0 && s[0] != '\0' ) {
			if      ( std::strcmp(s, "interp") == 0 ) interp = true;
			else if ( std::strcmp(s, "fit")    == 0 ) interp = false;
			else {
				char e[160];
				snprintf(e, sizeof e, "surface_through: mode must be \"fit\" (default) or \"interp\", not \"%s\"", s);
				result = oca_err(thNEW(stdString,(e))); return;
			}
		}
	}
	/* ---- 先頭要素で 点の格子 / 高さ場 を判別する ---- */
	sPtr<pigDataArray> r0 = rows->get_ix(thNEW(pigDataInteger,((INTEGER64)0)))->obt_array();
	if ( ! r0.is_notNull() || r0->length() < 2 ) {
		result = oca_err(thNEW(stdString,("surface_through: row 0 must be an array of >= 2 points (or >= 2 heights)")));
		return;
	}
	const bool heights = ! r0->get_ix(thNEW(pigDataInteger,((INTEGER64)0)))->obt_array().is_notNull();
	int nv = r0->length();

	try {
		TColgp_Array2OfPnt   pts(1, nu, 1, nv);
		TColStd_Array2OfReal zs (1, nu, 1, nv);
		for ( int i = 0 ; i < nu ; ++i ) {
			sPtr<pigDataArray> row = rows->get_ix(thNEW(pigDataInteger,((INTEGER64)i)))->obt_array();
			if ( ! row.is_notNull() ) {
				char m[160]; snprintf(m, sizeof m, "surface_through: row %d is not an array", i);
				result = oca_err(thNEW(stdString,(m))); return;
			}
			if ( row->length() != nv ) {
				char m[176];
				snprintf(m, sizeof m, "surface_through: the grid must be rectangular (row %d has %d entries, row 0 has %d)",
				    i, row->length(), nv);
				result = oca_err(thNEW(stdString,(m))); return;
			}
			for ( int j = 0 ; j < nv ; ++j ) {
				sPtr<pigData>      e = row->get_ix(thNEW(pigDataInteger,((INTEGER64)j)));
				sPtr<pigDataArray> p = e->obt_array();
				/* ⚠ 点と高さを **黙って混ぜない**。綴りが違えば別の物。 */
				if ( heights && p.is_notNull() ) {
					char m[176]; snprintf(m, sizeof m,
					    "surface_through: entry [%d][%d] is a point but row 0 is a height field — do not mix", i, j);
					result = oca_err(thNEW(stdString,(m))); return;
				}
				if ( ! heights && ! p.is_notNull() ) {
					char m[176]; snprintf(m, sizeof m,
					    "surface_through: entry [%d][%d] is a number but row 0 is a grid of points — do not mix", i, j);
					result = oca_err(thNEW(stdString,(m))); return;
				}
				if ( heights ) { zs.SetValue(i + 1, j + 1, e->get_flt()); continue; }
				if ( p->length() < 2 ) {
					char m[176]; snprintf(m, sizeof m, "surface_through: point [%d][%d] must be [x,y,z] (or [x,y])", i, j);
					result = oca_err(thNEW(stdString,(m))); return;
				}
				double x = p->get_ix(thNEW(pigDataInteger,((INTEGER64)0)))->get_flt();
				double y = p->get_ix(thNEW(pigDataInteger,((INTEGER64)1)))->get_flt();
				double z = ( p->length() >= 3 ) ? p->get_ix(thNEW(pigDataInteger,((INTEGER64)2)))->get_flt() : 0.0;
				pts.SetValue(i + 1, j + 1, gp_Pnt(x, y, z));
			}
		}
		/* ★ 高さ場は x,y が等間隔 — 原点 0・刻み 1 の格子に置く (置き直しは transform で書ける)。 */
		GeomAPI_PointsToBSplineSurface mk;
		if ( heights ) {
			if ( interp ) mk.Interpolate(zs, 0.0, 1.0, 0.0, 1.0);
			else          mk.Init(zs, 0.0, 1.0, 0.0, 1.0, 3, 8, GeomAbs_C2, 1.0e-4);
		} else {
			if ( interp ) mk.Interpolate(pts);
			else          mk.Init(pts, 3, 8, GeomAbs_C2, 1.0e-4);
		}
		Handle(Geom_BSplineSurface) surf = mk.Surface();
		if ( surf.IsNull() ) {
			result = oca_err(thNEW(stdString,("surface_through: the fit produced no surface"))); return;
		}
		BRepBuilderAPI_MakeFace mf(surf, Precision::Confusion());
		if ( ! mf.IsDone() ) {
			result = oca_err(thNEW(stdString,("surface_through: could not build the face"))); return;
		}
		out = thNEW(ocFace2D,());
		out->set_shape(mf.Face());
	} catch ( const Standard_Failure& e ) {
		std::string m = std::string("surface_through: OCCT failed [") + e.DynamicType()->Name() + "] (" +
		    ( ( e.GetMessageString() && e.GetMessageString()[0] ) ? e.GetMessageString() : "no message" ) + ")";
		result = oca_err(thNEW(stdString,(m.c_str())));
		return;
	}
}

sPtr<pigData>
ocaSurfaceThrough_::get_result()
{
	return ( result != thNULL ) ? result : sPtr<pigData>::d_cast(out);
}
