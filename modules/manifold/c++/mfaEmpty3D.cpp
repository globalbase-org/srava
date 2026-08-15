/*
 * mfaEmpty3D — empty3d() の計算本体(mf 版)。**値としての空集合**(頂点・面ゼロの mfMesh)を返す。
 * cgal 側 cgaEmpty3D と対。`{}`(fold の中立元)とは別物で、intersection(a, empty3d()) は空になる。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"mf/c++/mfMesh.h"
#include	"mf/c++/ptsmfWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/mfaEmpty3D_.h"

CLASS_TINYSTATE(mf/c++/mfaEmpty3D,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	mfaEmpty3D_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<mfMesh>	mesh;
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
class mfMesh;
class mfCross;
TS_END_INTERFACE

#endif


mfaEmpty3D_::mfaEmpty3D_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
mfaEmpty3D_::compute()
{
	mesh = thNEW(mfMesh,(manifold::Manifold()));   /* 既定構築 = 空の Manifold */
}

sPtr<pigData>
mfaEmpty3D_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
