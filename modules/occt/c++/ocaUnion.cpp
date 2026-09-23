/*
 * ocaUnion — union(a,b) の計算本体。★OCCT のブールは**失敗しうる** (誤った形を返すより「作れない」で止まる) (#3437 P5)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"oc/c++/ocShape.h"
#include	"ts2/c++/stdString.h"
#include	<stdio.h>
#include	"_ts2/c++/ocaUnion_.h"


CLASS_TINYSTATE(oc/c++/ocaUnion,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ocaUnion_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<ocShape>	out;
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
class ocShape;
TS_END_INTERFACE

#endif


ocaUnion_::ocaUnion_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/


void
ocaUnion_::compute()
{
	ocShape::ensure_init();   /* ★ OCCT の診断出力を stdout から外す (ocShape.h 参照) */
	/* ★ #3436 P4: n 項で受ける。3 項以上は SetArguments/SetTools で 1 回の BOPAlgo へ。 */
	const char *msg = 0;
	char why[512];              /* ★ 理由の受け皿はローカル (static を置かない) */
	why[0] = '\0';
	out = ocShape::bool_from_args(args, "union", &msg, why, (int)sizeof why, &brk_);
	if ( ! out.is_notNull() ) {
		/* ★ #3498: 中断された算法は IsDone()==false で返る = 失敗と見分けがつかない。
		 *   幾何のせいにする前に、まず中断を見る (ocShape.h の oc_abort_err)。 */
		if ( (result = oc_abort_err(brk_, "union")) != thNULL ) return;
		char b[160];
		::snprintf(b, sizeof b, "union: %s", msg ? msg : "OCCT boolean failed");
		result = oca_err(thNEW(stdString,(b)));
	}
}

sPtr<pigData>
ocaUnion_::get_result()
{
	return ( result != thNULL ) ? result : out;
}
