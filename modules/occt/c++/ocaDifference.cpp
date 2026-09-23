/*
 * ocaDifference — difference(a,b) の計算本体。★OCCT のブールは**失敗しうる** (誤った形を返すより「作れない」で止まる) (#3437 P5)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"oc/c++/ocShape.h"
#include	"ts2/c++/stdString.h"
#include	<stdio.h>
#include	"_ts2/c++/ocaDifference_.h"


CLASS_TINYSTATE(oc/c++/ocaDifference,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ocaDifference_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<ocShape>	out;
	sPtr<ocFace2D>	out2;   /* ★ #3518 の 4: 2D x 3D の結果 (2D) */
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
class ocFace2D;
TS_END_INTERFACE

#endif


ocaDifference_::ocaDifference_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/


void
ocaDifference_::compute()
{
	ocShape::ensure_init();   /* ★ OCCT の診断出力を stdout から外す (ocShape.h 参照) */
	/* ★ #3436 P4: n 項で受ける。3 項以上は SetArguments/SetTools で 1 回の BOPAlgo へ。 */
	/* ★★ #3518 の 4: **2D x 3D** — 面を立体で切り取る。返りは 2D。
	 *   sig が許すのは (oc-face3d, oc-brep3d) の **この向きの 2 項だけ**。
	 *   ⚠ 逆向き (立体 - 面) と union は sig に **載せていない** ので planner が断る。
	 *     理由: 体積 0 の面で立体を切っても何も変わらない (黙って no-op になる) /
	 *     次元の違う和は表現できる型が無い。★ 断る場所を op でなく sig にしておくと、
	 *     「どう書けるか」が記述子 1 箇所に集まる (#3436 の作法)。 */
	{
		int na2 = ( args != 0 ) ? args->length() : 0;
		sPtr<ocFace2D> f2 = ( na2 == 2 ) ? sPtr<ocFace2D>::d_cast((*args)[0]) : sPtr<ocFace2D>();
		sPtr<ocShape>  s3 = ( na2 == 2 ) ? sPtr<ocShape>::d_cast((*args)[1])  : sPtr<ocShape>();
		if ( f2.is_notNull() && s3.is_notNull() ) {
			char w2[512];
			w2[0] = '\0';
			out2 = f2->op_bool_solid(s3->shape(), 1, w2, (int)sizeof w2, &brk_);
			if ( ! out2.is_notNull() ) {
				if ( (result = oc_abort_err(brk_, "difference")) != thNULL ) return;
				char b2[224];
				::snprintf(b2, sizeof b2, "difference: cutting the 2D region with the solid failed (%s)",
				           w2[0] ? w2 : "OCCT boolean failed");
				result = oca_err(thNEW(stdString,(b2)));
			}
			return;
		}
	}
	const char *msg = 0;
	char why[512];              /* ★ 理由の受け皿はローカル (static を置かない) */
	why[0] = '\0';
	out = ocShape::bool_from_args(args, "difference", &msg, why, (int)sizeof why, &brk_);
	if ( ! out.is_notNull() ) {
		/* ★ #3498: 中断された算法は IsDone()==false で返る = 失敗と見分けがつかない。
		 *   幾何のせいにする前に、まず中断を見る (ocShape.h の oc_abort_err)。 */
		if ( (result = oc_abort_err(brk_, "difference")) != thNULL ) return;
		char b[160];
		::snprintf(b, sizeof b, "difference: %s", msg ? msg : "OCCT boolean failed");
		result = oca_err(thNEW(stdString,(b)));
	}
}

sPtr<pigData>
ocaDifference_::get_result()
{
	if ( result != thNULL ) return result;
	if ( out2.is_notNull() ) return sPtr<pigData>::d_cast(out2);   /* ★ 2D x 3D の結果 */
	return out;
}
