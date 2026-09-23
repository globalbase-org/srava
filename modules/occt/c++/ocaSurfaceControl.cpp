/*
 * ocaSurfaceControl — surface_control([[[x,y,z],...],...]) — 格子を **制御点** として
 *   自由曲面を作る (#3532)。
 *
 * ★★ surface_through との違いは **点の意味**。ここでは点は面の *係数* であって
 *   通過点ではない。面は与えた点を通らない (通るのは四隅の 4 点だけ)。
 *   実測 (macMINI / OCCT 7.9.3 ・ 半径 1.5 の球冠を 5x9 で標本化):
 *       通過点として当てる  与えた点との距離 max 5.0e-14
 *       制御点として使う    与えた点との距離 max 5.4e-1   ← ★ 通らない
 *   ⇒ 「点を通ってほしい」なら surface_through を使う。
 *
 * ★ 手で格子を打って形を引っぱる用途 (CAD の自由曲面モデリング) がこちら。格子を動かすと
 *   形が直感的に動き、滑らかさは Bezier の構造が保証する。
 *
 * ⚠ Bezier の次数は (行数-1) x (列数-1) で、OCCT の上限は 25。超えたら **明示エラー**にする
 *   (黙って B-spline に切り替えない — 別物の面が黙って出るのは今回いちばん避けたい形)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"oc/c++/ocShape.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ocaSurfaceControl_.h"

#include	<BRepBuilderAPI_MakeFace.hxx>
#include	<Geom_BezierSurface.hxx>
#include	<TColgp_Array2OfPnt.hxx>
#include	<TopoDS_Face.hxx>
#include	<Precision.hxx>
#include	<gp_Pnt.hxx>
#include	<Standard_Failure.hxx>
#include	<vector>
#include	<string>
#include	<cstdio>

CLASS_TINYSTATE(oc/c++/ocaSurfaceControl,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ocaSurfaceControl_(
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


ocaSurfaceControl_::ocaSurfaceControl_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
ocaSurfaceControl_::compute()
{
	ocShape::ensure_init();
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<pigDataArray> rows = ( na > 0 ) ? (*args)[0]->obt_array() : sPtr<pigDataArray>();
	int nu = rows.is_notNull() ? rows->length() : 0;
	if ( nu < 2 ) {
		result = oca_err(thNEW(stdString,("surface_control: needs a grid [[[x,y,z],...],...] with >= 2 rows")));
		return;
	}
	/* ⚠ 矩形であることを **どこが違うか** まで言って検査する (ragged は黙って通さない)。 */
	std::vector<std::vector<gp_Pnt> > g;
	int nv = -1;
	for ( int i = 0 ; i < nu ; ++i ) {
		sPtr<pigDataArray> row = rows->get_ix(thNEW(pigDataInteger,((INTEGER64)i)))->obt_array();
		if ( ! row.is_notNull() ) {
			char m[160]; snprintf(m, sizeof m, "surface_control: row %d is not an array of points", i);
			result = oca_err(thNEW(stdString,(m))); return;
		}
		int n = row->length();
		if ( nv < 0 ) nv = n;
		else if ( n != nv ) {
			char m[160];
			snprintf(m, sizeof m, "surface_control: the grid must be rectangular (row %d has %d points, row 0 has %d)", i, n, nv);
			result = oca_err(thNEW(stdString,(m))); return;
		}
		std::vector<gp_Pnt> r;
		for ( int j = 0 ; j < n ; ++j ) {
			sPtr<pigDataArray> p = row->get_ix(thNEW(pigDataInteger,((INTEGER64)j)))->obt_array();
			if ( ! p.is_notNull() || p->length() < 2 ) {
				char m[160]; snprintf(m, sizeof m, "surface_control: point [%d][%d] must be [x,y,z] (or [x,y])", i, j);
				result = oca_err(thNEW(stdString,(m))); return;
			}
			double x = p->get_ix(thNEW(pigDataInteger,((INTEGER64)0)))->get_flt();
			double y = p->get_ix(thNEW(pigDataInteger,((INTEGER64)1)))->get_flt();
			double z = ( p->length() >= 3 ) ? p->get_ix(thNEW(pigDataInteger,((INTEGER64)2)))->get_flt() : 0.0;
			r.push_back(gp_Pnt(x, y, z));
		}
		g.push_back(r);
	}
	if ( nv < 2 ) {
		result = oca_err(thNEW(stdString,("surface_control: needs >= 2 points per row")));
		return;
	}
	/* ⚠ 次数の上限。**黙って別の面種に切り替えない**。 */
	const int maxdeg = Geom_BezierSurface::MaxDegree();
	if ( nu - 1 > maxdeg || nv - 1 > maxdeg ) {
		char m[224];
		snprintf(m, sizeof m,
		    "surface_control: grid %dx%d is too large — the Bezier degree (%dx%d) exceeds the OCCT limit of %d. "
		    "Use surface_through() for dense grids.", nu, nv, nu - 1, nv - 1, maxdeg);
		result = oca_err(thNEW(stdString,(m))); return;
	}
	try {
		TColgp_Array2OfPnt poles(1, nu, 1, nv);
		for ( int i = 0 ; i < nu ; ++i )
			for ( int j = 0 ; j < nv ; ++j ) poles.SetValue(i + 1, j + 1, g[i][j]);
		Handle(Geom_BezierSurface) surf = new Geom_BezierSurface(poles);
		BRepBuilderAPI_MakeFace mf(surf, Precision::Confusion());
		if ( ! mf.IsDone() ) {
			result = oca_err(thNEW(stdString,("surface_control: could not build the face"))); return;
		}
		out = thNEW(ocFace2D,());
		out->set_shape(mf.Face());
	} catch ( const Standard_Failure& e ) {
		std::string m = std::string("surface_control: OCCT failed [") + e.DynamicType()->Name() + "] (" +
		    ( ( e.GetMessageString() && e.GetMessageString()[0] ) ? e.GetMessageString() : "no message" ) + ")";
		result = oca_err(thNEW(stdString,(m.c_str())));
		return;
	}
}

sPtr<pigData>
ocaSurfaceControl_::get_result()
{
	return ( result != thNULL ) ? result : sPtr<pigData>::d_cast(out);
}
