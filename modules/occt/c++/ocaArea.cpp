/*
 * ocaArea — area(cross2d) (#3471)。2D 領域 (oc-cross2d) の面積を返す値 op。
 * ★ GProp_GProps の SurfaceProperties。**穴は自動的に引かれる** (Face の向きで表現されるため)
 *   ので、'O' や 'あ' のような字形でも外周 − 内周が出る。
 * ★ 輪郭が Bezier / B-spline のままなので、**多角形近似を経ずに厳密な面積**が出る。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"oc/c++/ocShape.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ocaArea_.h"
#include	<stdio.h>

CLASS_TINYSTATE(oc/c++/ocaArea,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ocaArea_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

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

ocaArea_::ocaArea_(TS_ARGS0)
	: ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
	TS_CPARGS0
}

void
ocaArea_::compute()
{
	ocShape::ensure_init();
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<ocFace2D> in = ( na > 0 ) ? sPtr<ocFace2D>::d_cast((*args)[0]) : sPtr<ocFace2D>();
	if ( in.is_notNull() ) {
		char buf[64];
		::snprintf(buf, sizeof buf, "%.17g", in->area());
		result = thNEW(pigDataString,(buf));
		return;
	}
	/* ★ #3487: 3D は **表面積**。他の 6 カーネルが 3D で area を持つので、ここだけ
	 *   2D 専用のままだと式の途中でカーネルが裏返る。B-rep のまま GProp で積むので、
	 *   球なら 4πr² がそのまま出る (mesh 系の内接多面体とは構造的に違う値)。 */
	sPtr<ocShape> s3 = ( na > 0 ) ? sPtr<ocShape>::d_cast((*args)[0]) : sPtr<ocShape>();
	if ( s3.is_notNull() ) {
		result = thNEW(pigDataFloat,(s3->op_area()));
		return;
	}
	result = oca_err(thNEW(stdString,(
	    "area: input must be a 2D region (oc-cross2d) or a 3D shape (oc-brep3d)")));
}
