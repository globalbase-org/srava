/*
 * ptaValid — valid(p) の計算本体 (#3528)。値返し (1=正常 / 0=問題)。
 *
 * ★ 共通定義 (空でない ∧ 閉じている ∧ 自己交差が無い) のうち **① だけ**が意味を持つ。
 *   ②③ は点群では構造的に恒真 — 面も辺も無いので「閉じている」「自己交差が無い」が
 *   自明に成り立つ。openvdb の距離場と同じ扱い (src/h/common/meshprops.h 冒頭の方針)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"pt/c++/ptCloud.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ptaValid_.h"

CLASS_TINYSTATE(pt/c++/ptaValid,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ptaValid_(
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
class ptCloud;
TS_END_INTERFACE

#endif


ptaValid_::ptaValid_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
ptaValid_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<ptCloud> in = ( na > 0 ) ? sPtr<ptCloud>::d_cast((*args)[0]) : sPtr<ptCloud>();
	if ( ! in.is_notNull() ) {
		result = pta_err(thNEW(stdString,("valid: needs a point cloud")));
		return;
	}
	result = thNEW(pigDataInteger,((INTEGER64)in->op_valid()));
}
