/*
 * d2aCount — 共有 op dcount(d2-shape2d) の計算本体 (rev4 次元分担デモ)。値返し(2D の点数)。
 * d3 側の dcount(d3-mesh3d) は d3aNverts (頂点数) を使う。**同じ op 名を次元で分担**し、
 * decide_executor が入力型で正しいカーネルへ振ることを実証する。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"d2/c++/d2Shape.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/d2aCount_.h"

CLASS_TINYSTATE(d2/c++/d2aCount,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	d2aCount_(
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


d2aCount_::d2aCount_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
d2aCount_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<d2Shape> in = ( na > 0 ) ? sPtr<d2Shape>::d_cast((*args)[0]) : sPtr<d2Shape>();
	if ( ! in.is_notNull() ) {
		result = thNEW(pigDataError,(thNEW(stdString,("dcount: needs a d2 shape"))));
		return;
	}
	result = thNEW(pigDataInteger,((INTEGER64)in->np()));
}
