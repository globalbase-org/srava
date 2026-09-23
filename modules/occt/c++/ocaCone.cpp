/*
 * ocaCone — cone(r, h, seg) — 円錐 (原点中心・軸 +Z・底面 z=-h/2・頂点 z=+h/2) の計算本体 (occt 版・#3474)。
 * ★ occt は **解析曲面**で作る (近似しないので seg は無視)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"oc/c++/ocShape.h"
#include	"common/solids.h"   /* SOLID_PI (座標規約を共通ヘッダと共有) */

#include	<cmath>
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ocaCone_.h"

CLASS_TINYSTATE(oc/c++/ocaCone,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ocaCone_(
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


ocaCone_::ocaCone_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
ocaCone_::compute()
{
	ocShape::ensure_init();
	int na = ( args != 0 ) ? args->length() : 0;
	double r   = ( na > 0 ) ? (*args)[0]->get_flt() : 1.0;
	double h   = ( na > 1 ) ? (*args)[1]->get_flt() : 1.0;
	/* ★ #3570 段3: segs の引数そのものを撤去した (記述子の nin を減らした) ので、
	 *   ここに在った #3530 の「受けるが無視し、検査はする」は **届かなくなった**。
	 *   原則が「そのモジュールで必要のない引数は撤去する」に変わったため。 */
	if ( !(r > 0) ) { result = oca_err(thNEW(stdString,("cone: radius must be > 0"))); return; }
	if ( !(h > 0) ) { result = oca_err(thNEW(stdString,("cone: height must be > 0"))); return; }
	/* ★ #3545 段 5: 幾何の組み立ては **幾何 lib 側** (ocShape::make_cone)。
	 *   ⇒ この TU は OCCT を触らない — 触ると投げうる inline を通っただけで
	 *     RTTI の型インスタンスが .o に出る (ocShape.h の注記)。 */
	char why[320]; why[0] = '\0';
	out = ocShape::make_cone(r, h, why, (int)sizeof why);
	if ( ! out.is_notNull() ) {
		result = oca_err(thNEW(stdString,( why[0] ? why : "cone: failed" )));
		return;
	}

}

/* この演算の結果。エラー時は compute() が result にエラー値を残して本体未設定で return するので
 * result 優先。保存 (Writer 起動) は agent が出力 pigDataCache の set_body 経由で行う。 */
sPtr<pigData>
ocaCone_::get_result()
{
	return ( result != thNULL ) ? result : sPtr<pigData>::d_cast(out);
}
