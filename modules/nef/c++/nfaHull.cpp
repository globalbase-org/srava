/*
 * nfaHull — hull(a[,b,…]) — 与えた Nef すべての**凸包**(#3511)。nef 版。
 * 本体は nf_hull_from_args (nfMesh.cpp)。
 *
 * ★ **情報を落とす op** — 頂点しか見ないので穴も凹みも消える。
 * ★ nef としては **安い** 部類 — minkowski と違い凸分解もブールも通らず、頂点を抜いて
 *   convex_hull_3 を 1 回回すだけ。
 * ★ hull(hull(a,b),c) = hull(a,b,c) なので sig は fold 形 (木に分解してよい)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"nf/c++/nfMesh.h"
#include	"ts2/c++/stdString.h"
#include	<stdio.h>
#include	"_ts2/c++/nfaHull_.h"

CLASS_TINYSTATE(nf/c++/nfaHull,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	nfaHull_(
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


nfaHull_::nfaHull_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
nfaHull_::compute()
{
	/* ★ #3511: 1 個以上を受け、全頂点を 1 つの点集合にして 1 回で凸包を取る。 */
	const char *msg = 0;
	mesh = nf_hull_from_args(args, &msg);
	if ( ! mesh.is_notNull() ) {
		char b[256];
		::snprintf(b, sizeof b, "hull: %s", msg ? msg : "convex hull failed");
		result = nfa_err(thNEW(stdString,(b)));
	}
}

/* この演算の結果。エラー時は compute() が result にエラー値を残して mesh 未設定で return するので
 * result 優先。保存 (Writer 起動) は agent が出力 pigDataCache の set_body 経由で行う。 */
sPtr<pigData>
nfaHull_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
