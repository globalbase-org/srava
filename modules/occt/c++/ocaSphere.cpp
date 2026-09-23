/*
 * ocaSphere — sphere(r) の計算本体。★**厳密な球 1 面**。分割数の引数は受けるが無視する (近似しないので意味を持たない) (#3437 P5)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"oc/c++/ocShape.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ocaSphere_.h"
#include	<BRepPrimAPI_MakeSphere.hxx>
#include	<TopoDS_Shape.hxx>

CLASS_TINYSTATE(oc/c++/ocaSphere,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ocaSphere_(
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


ocaSphere_::ocaSphere_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/


void
ocaSphere_::compute()
{
	ocShape::ensure_init();   /* ★ OCCT の診断出力を stdout から外す (ocShape.h 参照) */
	int na = ( args != 0 ) ? args->length() : 0;
	double r = ( na > 0 ) ? (*args)[0]->get_flt() : 1.0;
	/* ★ #3570 段3: segs の引数そのものを撤去した (記述子の nin を減らした) ので、
	 *   ここに在った #3530 の「受けるが無視し、検査はする」は **届かなくなった**。
	 *   原則が「そのモジュールで必要のない引数は撤去する」に変わったため。 */
	/* ★ OCCT の球は **厳密な球面 1 枚**。他カーネルは多面体で近似するので分割数が要るが、
	 *   ここには無い。★このため volume は 4/3·π·r³ ちょうどになり、内接多面体を作る
	 *   他カーネルとは **一致しない** (kernel_agree に素で入れてはいけない)。 */
	if ( !(r > 0) ) {
		result = oca_err(thNEW(stdString,("sphere: radius must be > 0")));
		return;
	}
	BRepPrimAPI_MakeSphere mk(r);
	/* ★ プリミティブは遅延構築 (Shape() で Build())。IsDone() は立たないので IsNull() で見る。 */
	TopoDS_Shape sh = mk.Shape();
	if ( sh.IsNull() ) {
		result = oca_err(thNEW(stdString,("sphere: OCCT produced a null shape")));
		return;
	}
	out = thNEW(ocShape,());
	out->set_shape(sh);
}

sPtr<pigData>
ocaSphere_::get_result()
{
	return ( result != thNULL ) ? result : out;
}
