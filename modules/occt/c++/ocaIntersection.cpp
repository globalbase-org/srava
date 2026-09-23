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
	/* ★★ #3518 の 4: **2D x 3D** — 面を立体で切り取る。返りは 2D。
	 *   ★ intersection は可換なので **両向き**を受ける。
	 *   ⚠ union の 2D x 3D は sig に **載せていない** (次元の違う和を表現できる型が無い)。
	 *     断る場所を op でなく sig に置くと「どう書けるか」が記述子 1 箇所に集まる。 */
	{
		int na2 = ( args != 0 ) ? args->length() : 0;
		/* ★ intersection は **可換**なので両向きを受ける (面∩立体 = 立体∩面)。
		 *   ⚠ difference は順序に意味があるので片向きだけ。 */
		sPtr<ocFace2D> f2 = ( na2 == 2 ) ? sPtr<ocFace2D>::d_cast((*args)[0]) : sPtr<ocFace2D>();
		sPtr<ocShape>  s3 = ( na2 == 2 ) ? sPtr<ocShape>::d_cast((*args)[1])  : sPtr<ocShape>();
		if ( na2 == 2 && ! f2.is_notNull() ) {          /* 逆向き (立体, 面) */
			f2 = sPtr<ocFace2D>::d_cast((*args)[1]);
			s3 = sPtr<ocShape>::d_cast((*args)[0]);
		}
		if ( f2.is_notNull() && s3.is_notNull() ) {
			char w2[512];
			w2[0] = '\0';
			out2 = f2->op_bool_solid(s3->shape(), 0, w2, (int)sizeof w2, &brk_);
			if ( ! out2.is_notNull() ) {
				if ( (result = oc_abort_err(brk_, "intersection")) != thNULL ) return;
				char b2[224];
				::snprintf(b2, sizeof b2, "intersection: intersecting the 2D region with the solid failed (%s)",
				           w2[0] ? w2 : "OCCT boolean failed");
				result = oca_err(thNEW(stdString,(b2)));
			}
			return;
		}
	}
	const char *msg = 0;
	char why[512];              /* ★ 理由の受け皿はローカル (static を置かない) */
	why[0] = '\0';
	out = ocShape::bool_from_args(args, "intersection", &msg, why, (int)sizeof why, &brk_);
	if ( ! out.is_notNull() ) {
		/* ★ #3498: 中断された算法は IsDone()==false で返る = 失敗と見分けがつかない。
		 *   幾何のせいにする前に、まず中断を見る (ocShape.h の oc_abort_err)。 */
		if ( (result = oc_abort_err(brk_, "intersection")) != thNULL ) return;
		char b[160];
		::snprintf(b, sizeof b, "intersection: %s", msg ? msg : "OCCT boolean failed");
		result = oca_err(thNEW(stdString,(b)));
	}
}

sPtr<pigData>
ocaIntersection_::get_result()
{
	if ( result != thNULL ) return result;
	if ( out2.is_notNull() ) return sPtr<pigData>::d_cast(out2);   /* ★ 2D x 3D の結果 */
	return out;
}
