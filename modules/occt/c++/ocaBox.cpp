/*
 * ocaBox — box(w,h,d) の計算本体。★6 枚の**平面 Face** で作る (三角形ではない) (#3437 P5)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"oc/c++/ocShape.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ocaBox_.h"
#include	<BRepPrimAPI_MakeBox.hxx>
#include	<gp_Pnt.hxx>
#include	<TopoDS_Shape.hxx>

CLASS_TINYSTATE(oc/c++/ocaBox,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ocaBox_(
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


ocaBox_::ocaBox_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/


void
ocaBox_::compute()
{
	ocShape::ensure_init();   /* ★ OCCT の診断出力を stdout から外す (ocShape.h 参照) */
	int na = ( args != 0 ) ? args->length() : 0;
	double w = 1.0, h = 1.0, d = 1.0;
	/* ★ #3461: boxa(寸法配列) も同じクラスで受ける (manifold の mfaBox と同じ形)。
	 *   引数が 1 個で、それが配列なら boxa。 */
	sPtr<pigDataArray> dims = ( na == 1 ) ? (*args)[0]->obt_array() : sPtr<pigDataArray>();
	if ( dims.is_notNull() ) {
		int nd = dims->length();
		if ( nd > 0 ) w = dims->get_ix(thNEW(pigDataInteger,((INTEGER64)0)))->get_flt();
		if ( nd > 1 ) h = dims->get_ix(thNEW(pigDataInteger,((INTEGER64)1)))->get_flt();
		if ( nd > 2 ) d = dims->get_ix(thNEW(pigDataInteger,((INTEGER64)2)))->get_flt();
	} else {
		if ( na > 0 ) w = (*args)[0]->get_flt();
		if ( na > 1 ) h = (*args)[1]->get_flt();
		if ( na > 2 ) d = (*args)[2]->get_flt();
	}
	if ( !(w > 0) || !(h > 0) || !(d > 0) ) {
		result = oca_err(thNEW(stdString,("box: sizes must be > 0")));
		return;
	}
	/* ★ #3474 (2026-09-05 ひさ指示): **角が原点** (0,0,0)〜(w,h,d)。
	 *   ⚠ ここは以前 **原点中心** (-w/2〜+w/2) だった。「他カーネルと同じく原点中心」という
	 *   コメント付きだったが、**事実誤認**で、cgal / manifold / geogram / nef / cherchi の box は
	 *   どれも角が原点。関数リファレンスにも「原点隅」と書いてある = occt が規約違反だった。
	 *   多数派 (= 文書化された規約) へ揃える。
	 *   ★ 体積は平行移動で変わらないので kernel_agree の box:0 は **通っていた** —
	 *   位置を見るモデル (box_cut) をここで足す。 */
	gp_Pnt corner(0, 0, 0);
	BRepPrimAPI_MakeBox mk(corner, w, h, d);
	/* ★ プリミティブは **遅延構築**で、Shape() を呼んだ時点で Build() が走る。
	 *   IsDone() は BRepBuilderAPI_Command の done フラグで、プリミティブでは立たない
	 *   (これで最初 "box: OCCT failed" になった)。IsNull() で判定するのが正しい。 */
	TopoDS_Shape sh = mk.Shape();
	if ( sh.IsNull() ) {
		result = oca_err(thNEW(stdString,("box: OCCT produced a null shape")));
		return;
	}
	out = thNEW(ocShape,());
	out->set_shape(sh);
}

sPtr<pigData>
ocaBox_::get_result()
{
	return ( result != thNULL ) ? result : out;
}
