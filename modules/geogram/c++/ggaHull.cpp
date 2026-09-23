/*
 * ggaHull — hull(a[,b,…]) — 与えた形すべての**凸包**(#3511)。geogram 版。
 * 本体は ggMesh::hull_from_args (ggMesh.cpp)。
 *
 * ★ **情報を落とす op** — 頂点しか見ないので穴も凹みも消える。
 * ★ ブールと違い arrangement を通らないので、**閉じていない / 自己交差した入力でも通る**。
 * ★ hull(hull(a,b),c) = hull(a,b,c) なので sig は fold 形 (木に分解してよい)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"gg/c++/ggMesh.h"
#include	"ts2/c++/stdString.h"
#include	<stdio.h>
#include	"_ts2/c++/ggaHull_.h"

CLASS_TINYSTATE(gg/c++/ggaHull,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ggaHull_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<ggMesh>	mesh;
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
class ggMesh;
TS_END_INTERFACE

#endif


ggaHull_::ggaHull_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
ggaHull_::compute()
{
	/* ★ #3511: 1 個以上を受け、全頂点を 1 つの点集合にして 1 回で凸包を取る。 */
	const char *msg = 0;
	/* ★ 理由の受け皿は **この compute のローカル** (モジュール大域の static を置かない・
	 * in-proc では複数 op が同居しうるため。ひさ指示 2026-08-26)。 */
	char why[512];
	why[0] = '\0';
	mesh = ggMesh::hull_from_args(args, &msg, why, (int)sizeof why);
	if ( ! mesh.is_notNull() ) {
		char b[600];   /* geogram の FATAL 文は長い (file/line 付き) */
		::snprintf(b, sizeof b, "hull: %s", msg ? msg : "convex hull failed");
		result = gga_err(thNEW(stdString,(b)));
	}
}

sPtr<pigData>
ggaHull_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
