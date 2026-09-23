/*
 * mfaProjectFlatten — project_flatten(area2d) の計算本体 (#3534・cgaProjectFlatten のミラー)。
 *   world 座標の (x,y) をそのまま取り z を捨てる = **z=0 平面への直投影**。
 *
 * ★★ 「実形のまま寝かせる」ではない理由・用法は cgaProjectFlatten.cpp の冒頭を参照
 *   (同じ決定の 2 実装なので説明は 1 箇所に置く)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"mf/c++/mfMesh.h"
#include	"mf/c++/ptsmfWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/mfaProjectFlatten_.h"

CLASS_TINYSTATE(mf/c++/mfaProjectFlatten,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	mfaProjectFlatten_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<mfCross>	cross;
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
class mfCross;
TS_END_INTERFACE

#endif


mfaProjectFlatten_::mfaProjectFlatten_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
mfaProjectFlatten_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<mfCross> in = ( na > 0 ) ? sPtr<mfCross>::d_cast((*args)[0]) : sPtr<mfCross>();
	if ( ! in.is_notNull() ) {
		result = mfa_err(thNEW(stdString,("project_flatten: needs a 2D region")));
		return;
	}
	sPtr<mfCross> out = in->project_flatten();
	if ( ! out.is_notNull() ) {
		/* ★ 文言は cgal / occt と **同じ**にしてある — 同じ状況なので。
		 *   ⚠ 片方だけ直さないこと (同じ式を書いた利用者が別の説明を読むことになる)。 */
		result = mfa_err(thNEW(stdString,(
		    "project_flatten: the 2D region stands on a plane that contains world +Z, so "
		    "its shadow on z=0 collapses to a line; rotate the region so its plane is not "
		    "parallel to the projection direction")));
		return;
	}
	cross = out;
}

sPtr<pigData>
mfaProjectFlatten_::get_result()
{
	return ( result != thNULL ) ? result : cross;
}
