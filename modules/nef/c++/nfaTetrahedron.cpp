/*
 * nfaTetrahedron — tetrahedron(r) — 正四面体 (原点中心・外接球半径 r) の計算本体 (nef 版・#3474)。
 * ★ common/solids.h の共通生成器なので、他のメッシュ系カーネルと
 *   **頂点・面の並びが一致する** (geodesic.h の sphere と同じ方針)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"nf/c++/nfMesh.h"
#include	"nf/c++/nfTriSink.h"
#include	"common/solids.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/nfaTetrahedron_.h"

CLASS_TINYSTATE(nf/c++/nfaTetrahedron,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	nfaTetrahedron_(
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


nfaTetrahedron_::nfaTetrahedron_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
nfaTetrahedron_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	double r = ( na > 0 ) ? (*args)[0]->get_flt() : 1.0;
	if ( !(r > 0) ) { result = nfa_err(thNEW(stdString,("tetrahedron: circumradius must be > 0"))); return; }

	nfTriSink sink;
	srava_geo::make_tetrahedron(r, sink);
	mesh = sink.finish();
}

/* この演算の結果。エラー時は compute() が result にエラー値を残して本体未設定で return するので
 * result 優先。保存 (Writer 起動) は agent が出力 pigDataCache の set_body 経由で行う。 */
sPtr<pigData>
nfaTetrahedron_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
