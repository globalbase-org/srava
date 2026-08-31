/*
 * ocaIntersection — intersection(a,b) の計算本体。★OCCT のブールは**失敗しうる** (誤った形を返すより「作れない」で止まる) (#3437 P5)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"oc/c++/ocShape.h"
#include	"ts2/c++/stdString.h"
#include	<stdio.h>
#include	"_ts2/c++/ocaIntersection_.h"


CLASS_TINYSTATE(oc/c++/ocaIntersection,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ocaIntersection_(
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


ocaIntersection_::ocaIntersection_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/


void
ocaIntersection_::compute()
{
	ocShape::ensure_init();   /* ★ OCCT の診断出力を stdout から外す (ocShape.h 参照) */
	/* ★ #3436 P4: n 項で受ける。3 項以上は SetArguments/SetTools で 1 回の BOPAlgo へ。 */
	const char *msg = 0;
	char why[512];              /* ★ 理由の受け皿はローカル (static を置かない) */
	why[0] = '\0';
	out = ocShape::bool_from_args(args, "intersection", &msg, why, (int)sizeof why);
	if ( ! out.is_notNull() ) {
		char b[160];
		::snprintf(b, sizeof b, "intersection: %s", msg ? msg : "OCCT boolean failed");
		result = thNEW(pigDataError,(thNEW(stdString,(b))));
	}
}

sPtr<pigData>
ocaIntersection_::get_result()
{
	return ( result != thNULL ) ? result : out;
}
