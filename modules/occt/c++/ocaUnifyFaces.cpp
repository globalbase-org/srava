/*
 * ocaUnifyFaces — unify_faces(shape) (#3518)。**同じ曲面に載る隣り合う面を 1 枚に畳む**。
 *                 形は変えない (面の分け方だけを変える)。ShapeUpgrade_UnifySameDomain。
 *
 * ---- ★★ なぜ要るのか — 面の枚数は幾何だけでは決まらない ----
 * B-rep の面は「1 つの曲面 + その (u,v) を切り取るワイヤ」なので、周期曲面 (円柱・球・
 * トーラス) では **パラメータの継ぎ目 (seam) をまたぐ領域が 2 枚に割れる**。実測
 * (縦置きトーラスの下面を柱で切る):
 *
 *     face[0] v=[0.0000,0.4115]     ← 矩形の下端
 *     face[1] v=[5.8717,6.2832]     ← 矩形の上端 (2π)      幾何としてはひと続きの帯
 *
 * ⇒ nfaces が「本物の 2 か所」なのか「継ぎ目で割れただけ」なのか区別できない。
 *   この op を通すと後者だけが畳まれ、枚数が幾何的な意味を持つようになる:
 *     縦置きトーラスの切り取り  6 枚 → **4 枚** (面積 0.659748 は不変)
 *
 * ★ 畳み方は **パラメータの原点をずらすのではない**。周期曲面なので 2π を超える範囲を
 *   そのまま使う (実測: v=[5.8717,6.6947] ← 6.6947 = 0.4115 + 2π)。曲面は元のままなので
 *   他の面との共有関係が壊れない。
 *
 * ---- ⚠ 何を畳み、何を畳まないか (実測・2026-09-13) ----
 *     box                      6 → 6     畳まない (曲面が違う)
 *     fillet(box,0.3)         26 → 26    ★ **畳まない** — 平面と円筒は接していても *別の曲面*
 *     box ||| 上に積んだ box   10 → 6     畳む (同一平面の継ぎ目が消える)
 *     cylinder / sphere         3/1 → 3/1 畳まない
 *   ⇒ 判定は「接しているか」ではなく「**同じ曲面に載っているか**」。体積はどれも不変。
 *
 * ★★ srava の @unify@ (nef の内壁除去) とは **別物**。あちらは体積が変わる (重なりが 1 回だけ
 *   数えられ内部の仕切りが消える)。こちらは形を一切変えない。⇒ #3519 の作法に倣って
 *   名前を分ける (@unify@ ではなく @unify_faces@)。
 * ★ 黙ってはやらない (#3442 の @unify@ と同じ方針)。面の分け方は利用者が見ている情報なので、
 *   ブールや project の出口で勝手に畳むと「枚数が減った理由」が式に出なくなる。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"oc/c++/ocShape.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ocaUnifyFaces_.h"

#include	<ShapeUpgrade_UnifySameDomain.hxx>
#include	<TopoDS_Shape.hxx>
#include	<Standard_Failure.hxx>
#include	<string>

CLASS_TINYSTATE(oc/c++/ocaUnifyFaces,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ocaUnifyFaces_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<ocGeom>	out;
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
class ocGeom;
TS_END_INTERFACE

#endif


ocaUnifyFaces_::ocaUnifyFaces_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
ocaUnifyFaces_::compute()
{
	ocShape::ensure_init();   /* ★ OCCT の診断出力を stdout から外す */
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<ocShape>  in = ( na > 0 ) ? sPtr<ocShape>::d_cast((*args)[0])  : sPtr<ocShape>();
	sPtr<ocFace2D> f2 = ( na > 0 && ! in.is_notNull() ) ? sPtr<ocFace2D>::d_cast((*args)[0])
	                                                   : sPtr<ocFace2D>();
	TopoDS_Shape src;
	if      ( in.is_notNull() ) src = in->shape();
	else if ( f2.is_notNull() ) src = f2->shape();
	if ( src.IsNull() ) {
		result = oca_err(thNEW(stdString,(
		    "unify_faces: input must be an OCCT solid (oc-brep3d) or 2D region (oc-face3d)")));
		return;
	}
	try {
		/* (shape, 稜も畳む, 面を畳む, B-spline を連結する) */
		ShapeUpgrade_UnifySameDomain us(src, Standard_True, Standard_True, Standard_True);
		us.Build();
		TopoDS_Shape r = us.Shape();
		if ( r.IsNull() ) {
			result = oca_err(thNEW(stdString,(
			    "unify_faces: OCCT could not rebuild the faces")));
			return;
		}
		/* ★ 入力と **同じ型**で返す (3D → 3D / 2D → 2D)。形は変わらないので当然。 */
		if ( in.is_notNull() ) {
			sPtr<ocShape> o = thNEW(ocShape,());
			o->set_shape(r);
			out = sPtr<ocGeom>::d_cast(o);
		} else {
			sPtr<ocFace2D> o = thNEW(ocFace2D,());
			o->set_shape(r);
			out = sPtr<ocGeom>::d_cast(o);
		}
	} catch ( const Standard_Failure& e ) {
		std::string m = std::string("unify_faces: OCCT failed [") + e.DynamicType()->Name() + "] (" +
		    ( ( e.GetMessageString() && e.GetMessageString()[0] ) ? e.GetMessageString() : "no message" ) + ")";
		result = oca_err(thNEW(stdString,(m.c_str())));
	}
}

sPtr<pigData>
ocaUnifyFaces_::get_result()
{
	return ( result != thNULL ) ? result : sPtr<pigData>::d_cast(out);
}
