/*
 * ocaEmpty3D — empty3d() — **値としての空集合** (occt 版・#3474)。
 * ★ `{}` (空ハッシュ) との違い: `{}` は **fold の中立元**で「演算子を適用しない印」。
 *   empty3d() は **空集合そのもの**なので intersection(a, empty3d()) は正しく空になる。
 * ★ 空の Compound。B-rep でも「面を 1 つも持たない形状」として表せる。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"oc/c++/ocShape.h"
#include	<BRep_Builder.hxx>
#include	<TopoDS_Compound.hxx>
#include	<TopoDS_Shape.hxx>
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ocaEmpty3D_.h"

CLASS_TINYSTATE(oc/c++/ocaEmpty3D,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ocaEmpty3D_(
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


ocaEmpty3D_::ocaEmpty3D_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
ocaEmpty3D_::compute()
{
	ocShape::ensure_init();
	BRep_Builder bb;
	TopoDS_Compound comp;
	bb.MakeCompound(comp);
	out = thNEW(ocShape,());
	out->set_shape(comp);   /* 空の Compound = 空集合 */
}

sPtr<pigData>
ocaEmpty3D_::get_result()
{
	return ( result != thNULL ) ? result : sPtr<pigData>::d_cast(out);
}
