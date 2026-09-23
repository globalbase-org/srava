/*
 * ocaCircle — circle(r[, segs]) — 2D 円 (occt 版・#3474)。
 * ★ **厳密**な円 (Geom_Circle 1 本の Wire)。segs は近似しないので **無視する**
 *   (occt の sphere / cylinder / torus と同じ扱い)。
 * ⚠ したがってメッシュ系 (内接正多角形) とは面積が構造的に違う = カーネル一致の表には
 *   入れられない。検証は閉形式 (πr²) で行う。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"oc/c++/ocShape.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ocaCircle_.h"

#include	<cmath>

CLASS_TINYSTATE(oc/c++/ocaCircle,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ocaCircle_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<ocFace2D>	out;
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
class ocFace2D;
TS_END_INTERFACE

#endif


ocaCircle_::ocaCircle_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
ocaCircle_::compute()
{
	ocShape::ensure_init();
	int na = ( args != 0 ) ? args->length() : 0;
	double r = ( na > 0 ) ? (*args)[0]->get_flt() : 1.0;
	/* ★ #3570 段3: segs の引数そのものを撤去した (記述子の nin を減らした) ので、
	 *   ここに在った #3530 の「受けるが無視し、検査はする」は **届かなくなった**。
	 *   原則が「そのモジュールで必要のない引数は撤去する」に変わったため。 */
	if ( !(r > 0) ) { result = oca_err(thNEW(stdString,("circle: radius must be > 0"))); return; }
	/* ★ #3545 段 5: 幾何の組み立ては **幾何 lib 側** (ocFace2D::make_circle)。
	 *   ⇒ この TU は OCCT を触らない — 触ると投げうる inline を通っただけで
	 *     RTTI の型インスタンスが .o に出る (ocShape.h の注記)。 */
	char why[320]; why[0] = '\0';
	out = ocFace2D::make_circle(r, why, (int)sizeof why);
	if ( ! out.is_notNull() ) {
		result = oca_err(thNEW(stdString,( why[0] ? why : "circle: failed" )));
		return;
	}

}

/* この演算の結果。エラー時は compute() が result にエラー値を残して本体未設定で return するので
 * result 優先。 */
sPtr<pigData>
ocaCircle_::get_result()
{
	return ( result != thNULL ) ? result : sPtr<pigData>::d_cast(out);
}
