/*
 * guaCentroid — centroid(m) (#3527 段 3)。3D は体積重心・2D は **面積重心** (穴は負寄与)。配列返し。
 *   ★ 中身は src/h/common/meshprops.h / ringprops.h — **定義ごと 1 本**。mf / gg / ch はこれまで内部表現から
 *     素の配列へ写して同じヘッダを通していた。⇒ ここへ寄せても *答えは変わらない*。
 * ⚠ cross2d は 2 成分・face3d は world 3 成分 (bbox と同じ)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"gu/c++/guGeom.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/guaCentroid_.h"

CLASS_TINYSTATE(gu/c++/guaCentroid,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	guaCentroid_(
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

guaCentroid_::guaCentroid_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
guaCentroid_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<guGeom> in = ( na > 0 ) ? sPtr<guGeom>::d_cast((*args)[0]) : sPtr<guGeom>();
	if ( ! in.is_notNull() ) {
		result = gua_err(thNEW(stdString,("centroid: needs a mesh or 2D region")));
		return;
	}
	double c[3] = { 0, 0, 0 };
	int n = in->op_centroid(c);
	sPtr<pigDataArray> arr = thNEW(pigDataArray,());
	for ( int i = 0 ; i < n ; ++i )
		arr->push(thNEW(pigDataFloat,(c[i])));
	result = arr;
}
