/*
 * ocaCylinder — cylinder(r,h) の計算本体 (#3437 P5)。
 * ★ 側面は**厳密な円筒面 1 枚**、上下は平面 2 枚 = Face 数 3。三角形近似は入らないので
 *   volume は π r² h とちょうど一致する (分割数という概念が無い)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"oc/c++/ocShape.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ocaCylinder_.h"

CLASS_TINYSTATE(oc/c++/ocaCylinder,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ocaCylinder_(
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


ocaCylinder_::ocaCylinder_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
ocaCylinder_::compute()
{
	ocShape::ensure_init();   /* ★ OCCT の診断出力を stdout から外す (ocShape.h 参照) */
	int na = ( args != 0 ) ? args->length() : 0;
	double r = ( na > 0 ) ? (*args)[0]->get_flt() : 1.0;
	double h = ( na > 1 ) ? (*args)[1]->get_flt() : 1.0;
	/* ★ #3570 段3: segs の引数そのものを撤去した (記述子の nin を減らした) ので、
	 *   ここに在った #3530 の「受けるが無視し、検査はする」は **届かなくなった**。
	 *   原則が「そのモジュールで必要のない引数は撤去する」に変わったため。 */
	if ( !(r > 0) || !(h > 0) ) {
		result = oca_err(thNEW(stdString,("cylinder: r and h must be > 0")));
		return;
	}
	/* 他カーネルの box / sphere と同じく **原点中心**・軸は +Z。
	 * BRepPrimAPI_MakeCylinder は基準点が底面中心なので h/2 下げる。 */
	/* ★ #3545 段 5: 幾何の組み立ては **幾何 lib 側** (ocShape::make_cylinder)。
	 *   ⇒ この TU は OCCT を触らない — 触ると投げうる inline を通っただけで
	 *     RTTI の型インスタンスが .o に出る (ocShape.h の注記)。 */
	char why[320]; why[0] = '\0';
	out = ocShape::make_cylinder(r, h, why, (int)sizeof why);
	if ( ! out.is_notNull() ) {
		result = oca_err(thNEW(stdString,( why[0] ? why : "cylinder: failed" )));
		return;
	}
}

sPtr<pigData>
ocaCylinder_::get_result()
{
	return ( result != thNULL ) ? result : out;
}
