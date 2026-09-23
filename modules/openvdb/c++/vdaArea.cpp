/*
 * vdaArea — area(m) — 表面積 (tools::levelSetArea)。
 *
 * ★ 値を返すだけの op (→value)。**メッシュを作らずに**格子から出す (体積と同じ方針)。
 *   解像度に依存する近似値なので、メッシュ系との一致は相対誤差で見る。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"vd/c++/vdGrid.h"
#include	"vd/c++/vdArena.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/vdaArea_.h"

#include	<string>

CLASS_TINYSTATE(vd/c++/vdaArea,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	vdaArea_(
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


vdaArea_::vdaArea_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
vdaArea_::compute()
{
	std::string vdwhy;
	/* ★ #3474 続き: 例外境界。openvdb が投げると受け手が無く、ワーカースレッド
	 *   由来なら agent ごと死ぬ (vdArena.h の vd_arena_guard 参照)。 */
	if ( ! vd_arena_guard("area", [&]{

	vdGrid::ensure_init();
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<vdGrid> in = ( na > 0 ) ? sPtr<vdGrid>::d_cast((*args)[0]) : sPtr<vdGrid>();
	if ( ! in.is_notNull() ) {
		result = vda_err(thNEW(stdString,("area: needs an openvdb grid")));
		return;
	}
	double a = in->op_area(&brk_);
	/* ★★ #3498: 中断時は途中までの総和。vdaVolume.cpp の同じ箇所の理由を参照。 */
	if ( (result = vd_abort_err(brk_, "area")) != thNULL ) return;
	result = thNEW(pigDataFloat,(a));
	}, vdwhy) )
		result = vda_err(thNEW(stdString,(vdwhy.c_str())));
}
