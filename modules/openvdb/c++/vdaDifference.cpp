/*
 * vdaDifference — difference(a, b) の計算本体 (#3434 P2)。
 * ★ ボリュームのブールは **点ごとの min/max だけ** = 位相の場合分けが存在しない。
 *   自己交差・非多様体・汚い入力でも必ず答えが出る (退化という概念が無い) のがこの表現の要点。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"vd/c++/vdGrid.h"
#include	"vd/c++/vdArena.h"   /* ★ #3441: op あたりの TBB 予算 */
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/vdaDifference_.h"

#include	<stdio.h>

CLASS_TINYSTATE(vd/c++/vdaDifference,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	vdaDifference_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<vdGrid>	out;
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
class vdGrid;
TS_END_INTERFACE

#endif


vdaDifference_::vdaDifference_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
vdaDifference_::compute()
{
	/* ★ #3441: op 内並列 (TBB) は **op あたり**の予算で走らせる。予算未指定なら素通し。
	 *   ⚠ 包み忘れるとその op だけ無制限になるので、compute() 単位で一律に包む。 */
	vd_in_arena([&]{
	vdGrid::ensure_init();
	/* ★ #3436 P4: n 項で受ける (agent の中で逐次に畳む = 中間 .vdb の往復が消える)。 */
	const char *msg = 0;
	char ebuf[224];
	out = vdGrid::bool_from_args(args, "difference", &msg, ebuf, (int)sizeof ebuf);
	if ( ! out.is_notNull() ) {
		char b[256];
		::snprintf(b, sizeof b, "difference: %s", msg ? msg : "openvdb CSG failed");
		result = thNEW(pigDataError,(thNEW(stdString,(b))));
	}
	});
}

sPtr<pigData>
vdaDifference_::get_result()
{
	return ( result != thNULL ) ? result : out;
}
