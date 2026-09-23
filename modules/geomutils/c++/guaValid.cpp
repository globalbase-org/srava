/*
 * guaValid — valid(m) (#3527 段 3)。① 空でない ∧ ② 閉じている (2D は 3 点以上) ∧ ③ 自己交差が無い。
 *   ★ 中身は src/h/common/meshprops.h / ringprops.h — **定義ごと 1 本**。mf / gg / ch はこれまで内部表現から
 *     素の配列へ写して同じヘッダを通していた。⇒ ここへ寄せても *答えは変わらない*。
 * ⚠ 定義は 1 つ・答え方はカーネルごと (#3487)。cgal は EPECK の厳密述語で同じ 3 条件を
 *   答え続ける — 別のことを答えているのではない。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"gu/c++/guGeom.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/guaValid_.h"

CLASS_TINYSTATE(gu/c++/guaValid,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	guaValid_(
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

guaValid_::guaValid_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
guaValid_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<guGeom> in = ( na > 0 ) ? sPtr<guGeom>::d_cast((*args)[0]) : sPtr<guGeom>();
	if ( ! in.is_notNull() ) {
		result = gua_err(thNEW(stdString,("valid: needs a mesh or 2D region")));
		return;
	}
	result = thNEW(pigDataInteger,((INTEGER64)in->op_valid()));
}
