/*
 * nfaCentroid — centroid(m) — 体積重心 [x,y,z] を返す (発散定理)。
 *
 * ★ 値を返すだけの op (→value)。2D 型を要さないので、2D 型を持たないこのカーネルでも置ける。
 *   無いと「値の素性を訊く」ためだけに別カーネルへ cast させることになり、cast が通らない
 *   値 (非有界・非多様体) では確認手段そのものが消える (#3478) — それが #3487 の動機。
 */
#include	<cstdio>
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"nf/c++/nfMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/nfaCentroid_.h"

CLASS_TINYSTATE(nf/c++/nfaCentroid,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	nfaCentroid_(
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


nfaCentroid_::nfaCentroid_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
nfaCentroid_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<nfMesh> in = ( na > 0 ) ? sPtr<nfMesh>::d_cast((*args)[0]) : sPtr<nfMesh>();
	if ( ! in.is_notNull() ) {
		result = nfa_err(thNEW(stdString,("centroid: needs a Nef mesh")));
		return;
	}
	double c[3] = { 0, 0, 0 };
	if ( ! in->op_centroid(c) ) {
		char b[256];
		::snprintf(b, sizeof b,
		    "centroid: the value has no boundary representation (%s) — try part()/unify() first",
		    nf_why(in));
		result = nfa_err(thNEW(stdString,(b)));
		return;
	}
	sPtr<pigDataArray> arr = thNEW(pigDataArray,());
	for ( int i = 0 ; i < 3 ; ++i )
		arr->push(thNEW(pigDataFloat,(c[i])));
	result = arr;
}
