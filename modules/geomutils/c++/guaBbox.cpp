/*
 * guaBbox — bbox(m) (#3527 段 3)。軸平行 AABB。入れ子配列 [min隅, max隅] を返す。
 *   ★ 中身は src/h/common/meshprops.h / ringprops.h — **定義ごと 1 本**。mf / gg / ch はこれまで内部表現から
 *     素の配列へ写して同じヘッダを通していた。⇒ ここへ寄せても *答えは変わらない*。
 * ⚠⚠ #3533: **cross2d は局所 2 成分・face3d は world 3 成分**。face3d は z=0 に居ないので
 *   2 成分では答えられない (mfCross / cgMesh2D と同じ約束)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"gu/c++/guGeom.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/guaBbox_.h"

CLASS_TINYSTATE(gu/c++/guaBbox,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	guaBbox_(
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
class guGeom;
TS_END_INTERFACE

#endif

guaBbox_::guaBbox_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
guaBbox_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<guGeom> in = ( na > 0 ) ? sPtr<guGeom>::d_cast((*args)[0]) : sPtr<guGeom>();
	if ( ! in.is_notNull() ) {
		result = gua_err(thNEW(stdString,("bbox: needs a mesh or 2D region")));
		return;
	}
	double mn[3] = { 0, 0, 0 }, mx[3] = { 0, 0, 0 };
	int n = in->op_bbox(mn, mx);
	sPtr<pigDataArray> lo = thNEW(pigDataArray,());
	sPtr<pigDataArray> hi = thNEW(pigDataArray,());
	for ( int i = 0 ; i < n ; ++i ) {
		lo->push(thNEW(pigDataFloat,(mn[i])));
		hi->push(thNEW(pigDataFloat,(mx[i])));
	}
	sPtr<pigDataArray> arr = thNEW(pigDataArray,());   /* [min 隅, max 隅] */
	arr->push(lo);
	arr->push(hi);
	result = arr;
}
