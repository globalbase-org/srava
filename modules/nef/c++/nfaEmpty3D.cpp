/*
 * nfaEmpty3D — empty3d() — **値としての空集合** (nef 版・#3474)。
 * ★ `{}` (空ハッシュ) との違い: `{}` は **fold の中立元**で「演算子を適用しない印」。
 *   empty3d() は **空集合そのもの**なので intersection(a, empty3d()) は正しく空になる。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"nf/c++/nfMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/nfaEmpty3D_.h"

CLASS_TINYSTATE(nf/c++/nfaEmpty3D,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	nfaEmpty3D_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<nfNefMesh>	mesh;
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
class nfNefMesh;
TS_END_INTERFACE

#endif


nfaEmpty3D_::nfaEmpty3D_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
nfaEmpty3D_::compute()
{
	/* 既定構築の Nef が空集合 (頂点・面ゼロ)。 */
	mesh = thNEW(NF_MESH,());
}

sPtr<pigData>
nfaEmpty3D_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
