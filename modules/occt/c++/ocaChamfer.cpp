/*
 * ocaChamfer — chamfer(shape, d) の計算本体 (#3437)。**全ての稜**を距離 d で 45 度に削ぐ。
 * ★ fillet と同じく B-rep 固有の加工だが、削いだ面は平面なので fillet より素直。
 * ★ 検算できる: 立方体 (辺 a) の全稜を距離 d で削いだ体積は **a³ − 6d²(a − d)** ちょうど。
 *   (12 稜の楔 − 24 の 2 重取り + 8 の 3 重取り、の包除。d = a/2 で菱形十二面体 = a³/4 になる。)
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"oc/c++/ocShape.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ocaChamfer_.h"


CLASS_TINYSTATE(oc/c++/ocaChamfer,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ocaChamfer_(
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


ocaChamfer_::ocaChamfer_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/


void
ocaChamfer_::compute()
{
	ocShape::ensure_init();   /* ★ OCCT の診断出力を stdout から外す (ocShape.h 参照) */
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<ocShape> in = ( na > 0 ) ? sPtr<ocShape>::d_cast((*args)[0]) : sPtr<ocShape>();
	if ( ! in.is_notNull() ) {
		result = thNEW(pigDataError,(thNEW(stdString,("chamfer: needs an OCCT shape"))));
		return;
	}
	double d = ( na > 1 ) ? (*args)[1]->get_flt() : 0.0;
	if ( d < 0.0 ) {
		result = thNEW(pigDataError,(thNEW(stdString,("chamfer: distance must be >= 0"))));
		return;
	}
		/* ★ 理由の受け皿は **この compute のローカル** (モジュール大域の static を置かない・
	 * in-proc では複数 op が同居しうるため。ひさ指示 2026-08-26)。 */
	char why[512];
	why[0] = '\0';
out = in->op_chamfer(d, why, (int)sizeof why);
	if ( ! out.is_notNull() ) {
		/* ★ OCCT が例外で失敗した場合はその理由を載せる (ocShape の oc_guard が捕まえている)。
		 * 例外でなく IsDone()==false の場合は理由が無いので従来の文言。 */
		char b[600];
		const char *msg = "chamfer: OCCT MakeChamfer failed (distance too large, or no edge to chamfer)";
		if ( why[0] != '\0' ) {
			::snprintf(b, sizeof b, "chamfer: OCCT failed: %s", why);
			msg = b;
		}
		result = thNEW(pigDataError,(thNEW(stdString,(msg))));
	}
}

sPtr<pigData>
ocaChamfer_::get_result()
{
	return ( result != thNULL ) ? result : out;
}
