/*
 * ocaLoft — loft(断面, 断面, …) (#3511)。断面の列を **なめらかな曲面で通す**。
 *
 * ★★ 断面の **置き場所は op が決めない**。利用者が @transform@ で空間に置いた 2D を
 *   そのまま受ける。これが書けるのは #3518 の 1 で @oc-face3d@ が z=0 平面の外へ
 *   出られるようになったから — メッシュ系の 2D は平面に縛られているので同じ形が書けない。
 *
 *     loft(rect(2,3), translate(rect(2,3),[0,0,5]))                   まっすぐな角柱
 *     loft(circle(1,0), translate(rotate(circle(1,0),"x",20),[0,0,4]))  傾いた断面も置ける
 *
 * ★ @loft@ と @loft_ruled@ を **別 op** にしてあるのは、線織面は三角形で厳密に表せる =
 *   メッシュ系でも実装できるのに対し、なめらかな方は解析曲面が要るため。
 *   どのカーネルがどちらを持てるかを #3510 の表に出せる。
 *
 * 実体は @ocShape::loft_from_args@ (2 つの op で共有・@ruled@ フラグだけが違う)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"oc/c++/ocShape.h"
#include	"ts2/c++/stdString.h"
#include	<stdio.h>
#include	"_ts2/c++/ocaLoft_.h"

CLASS_TINYSTATE(oc/c++/ocaLoft,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ocaLoft_(
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


ocaLoft_::ocaLoft_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
ocaLoft_::compute()
{
	ocShape::ensure_init();   /* ★ OCCT の診断出力を stdout から外す */
	const char *msg = 0;
	char why[512];
	why[0] = '\0';
	out = ocShape::loft_from_args(args, false, &msg, why, (int)sizeof why);
	if ( ! out.is_notNull() ) {
		char b[600];
		::snprintf(b, sizeof b, "loft: %s", msg ? msg : "failed");
		result = oca_err(thNEW(stdString,(b)));
	}
}

sPtr<pigData>
ocaLoft_::get_result()
{
	return ( result != thNULL ) ? result : sPtr<pigData>::d_cast(out);
}
