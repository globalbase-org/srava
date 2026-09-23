/*
 * ocaSetPoles — set_poles(face, 格子) — 自由曲面の **制御網を差し替える** (#3532)。
 *
 * ★★ poles() と対で「通過点で当てて、制御点で整える」を合成で書くための道具。
 *   ⇒ 「通過点と制御点を 1 op で混在させる」を持たずに済ませるのが狙い (ocaPoles.cpp の頭注)。
 *
 * ⚠⚠ **トリムは保たれない** — 差し替えた曲面の自然境界で面を建て直す。
 *   OCCT に「既存の面の輪郭を新しい曲面の上に載せ直す」ctor が無いため。
 *   ⇒ 輪郭が 1 本でない面 (穴あき等) は **黙って落とさず明示エラー**にする。
 *     1 本であっても自然境界とは限らないので、その旨は関数リファレンスに書く。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"oc/c++/ocShape.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ocaSetPoles_.h"

#include	<BRep_Tool.hxx>
#include	<BRepBuilderAPI_MakeFace.hxx>
#include	<Geom_BSplineSurface.hxx>
#include	<Geom_BezierSurface.hxx>
#include	<TopExp_Explorer.hxx>
#include	<TopoDS.hxx>
#include	<TopoDS_Face.hxx>
#include	<TopLoc_Location.hxx>
#include	<Precision.hxx>
#include	<gp_Pnt.hxx>
#include	<Standard_Failure.hxx>
#include	<vector>
#include	<string>
#include	<cstdio>

CLASS_TINYSTATE(oc/c++/ocaSetPoles,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ocaSetPoles_(
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


ocaSetPoles_::ocaSetPoles_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
ocaSetPoles_::compute()
{
	ocShape::ensure_init();
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<ocFace2D> in = ( na > 0 ) ? sPtr<ocFace2D>::d_cast((*args)[0]) : sPtr<ocFace2D>();
	if ( ! in.is_notNull() || in->shape().IsNull() ) {
		result = oca_err(thNEW(stdString,("set_poles: input must be a 2D region (oc-face3d)")));
		return;
	}
	sPtr<pigDataArray> rows = ( na > 1 ) ? (*args)[1]->obt_array() : sPtr<pigDataArray>();
	int nu = rows.is_notNull() ? rows->length() : 0;
	if ( nu < 2 ) {
		result = oca_err(thNEW(stdString,("set_poles: needs a grid [[[x,y,z],...],...] with >= 2 rows")));
		return;
	}
	try {
		TopoDS_Face f; int nf = 0, nw = 0;
		for ( TopExp_Explorer e(in->shape(), TopAbs_FACE) ; e.More() ; e.Next() ) {
			if ( nf == 0 ) f = TopoDS::Face(e.Current());
			++nf;
		}
		if ( nf != 1 ) {
			char m[160]; snprintf(m, sizeof m, "set_poles: needs exactly 1 face (found %d)", nf);
			result = oca_err(thNEW(stdString,(m))); return;
		}
		for ( TopExp_Explorer e(f, TopAbs_WIRE) ; e.More() ; e.Next() ) ++nw;
		if ( nw != 1 ) {
			/* ⚠ 建て直すと輪郭が消えるので、**消える前に**断る。 */
			char m[192]; snprintf(m, sizeof m,
			    "set_poles: the face has %d wires — rebuilding from the poles would drop the trim", nw);
			result = oca_err(thNEW(stdString,(m))); return;
		}
		TopLoc_Location loc;
		Handle(Geom_Surface) s = BRep_Tool::Surface(f, loc);
		Handle(Geom_BSplineSurface) bs = Handle(Geom_BSplineSurface)::DownCast(s);
		Handle(Geom_BezierSurface)  bz = Handle(Geom_BezierSurface)::DownCast(s);
		if ( bs.IsNull() && bz.IsNull() ) {
			result = oca_err(thNEW(stdString,(
			    "set_poles: the surface has no control net (only bezier / bspline do — see surface_type())")));
			return;
		}
		const int onu = bs.IsNull() ? bz->NbUPoles() : bs->NbUPoles();
		const int onv = bs.IsNull() ? bz->NbVPoles() : bs->NbVPoles();
		/* ⚠ 形が違えば **黙って合わせない**。poles() が返した形をそのまま直す使い方が前提。 */
		if ( nu != onu ) {
			char m[192]; snprintf(m, sizeof m,
			    "set_poles: the grid has %d rows but the surface has %d (use poles() to get the right shape)", nu, onu);
			result = oca_err(thNEW(stdString,(m))); return;
		}
		std::vector<std::vector<gp_Pnt> > g;
		for ( int i = 0 ; i < nu ; ++i ) {
			sPtr<pigDataArray> row = rows->get_ix(thNEW(pigDataInteger,((INTEGER64)i)))->obt_array();
			if ( ! row.is_notNull() ) {
				char m[160]; snprintf(m, sizeof m, "set_poles: row %d is not an array of points", i);
				result = oca_err(thNEW(stdString,(m))); return;
			}
			if ( row->length() != onv ) {
				char m[192]; snprintf(m, sizeof m,
				    "set_poles: row %d has %d points but the surface has %d columns", i, row->length(), onv);
				result = oca_err(thNEW(stdString,(m))); return;
			}
			std::vector<gp_Pnt> r;
			for ( int j = 0 ; j < onv ; ++j ) {
				sPtr<pigDataArray> p = row->get_ix(thNEW(pigDataInteger,((INTEGER64)j)))->obt_array();
				if ( ! p.is_notNull() || p->length() < 2 ) {
					char m[176]; snprintf(m, sizeof m, "set_poles: point [%d][%d] must be [x,y,z] (or [x,y])", i, j);
					result = oca_err(thNEW(stdString,(m))); return;
				}
				double x = p->get_ix(thNEW(pigDataInteger,((INTEGER64)0)))->get_flt();
				double y = p->get_ix(thNEW(pigDataInteger,((INTEGER64)1)))->get_flt();
				double z = ( p->length() >= 3 ) ? p->get_ix(thNEW(pigDataInteger,((INTEGER64)2)))->get_flt() : 0.0;
				gp_Pnt q(x, y, z);
				/* ★ poles() は面の位置を効かせて返すので、こちらは戻してから入れる。 */
				if ( ! loc.IsIdentity() ) q.Transform(loc.Transformation().Inverted());
				r.push_back(q);
			}
			g.push_back(r);
		}
		Handle(Geom_Surface) ns;
		if ( ! bs.IsNull() ) {
			Handle(Geom_BSplineSurface) c = Handle(Geom_BSplineSurface)::DownCast(bs->Copy());
			for ( int i = 0 ; i < nu ; ++i ) for ( int j = 0 ; j < onv ; ++j ) c->SetPole(i + 1, j + 1, g[i][j]);
			ns = c;
		} else {
			Handle(Geom_BezierSurface) c = Handle(Geom_BezierSurface)::DownCast(bz->Copy());
			for ( int i = 0 ; i < nu ; ++i ) for ( int j = 0 ; j < onv ; ++j ) c->SetPole(i + 1, j + 1, g[i][j]);
			ns = c;
		}
		BRepBuilderAPI_MakeFace mf(ns, Precision::Confusion());
		if ( ! mf.IsDone() ) {
			result = oca_err(thNEW(stdString,("set_poles: could not build the face"))); return;
		}
		TopoDS_Shape nfce = mf.Face();
		if ( ! loc.IsIdentity() ) nfce.Location(loc);
		out = thNEW(ocFace2D,());
		out->set_shape(TopoDS::Face(nfce));
	} catch ( const Standard_Failure& e ) {
		std::string m = std::string("set_poles: OCCT failed [") + e.DynamicType()->Name() + "] (" +
		    ( ( e.GetMessageString() && e.GetMessageString()[0] ) ? e.GetMessageString() : "no message" ) + ")";
		result = oca_err(thNEW(stdString,(m.c_str())));
		return;
	}
}

sPtr<pigData>
ocaSetPoles_::get_result()
{
	return ( result != thNULL ) ? result : sPtr<pigData>::d_cast(out);
}
