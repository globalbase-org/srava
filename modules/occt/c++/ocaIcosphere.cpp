/*
 * ocaIcosphere — icosphere(r, subdiv) — 測地球 (正二十面体を細分) の計算本体 (occt 版・#3474)。
 * ★ **測地多面体**なので occt でも平面 Face を縫えば厳密に一致する。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"oc/c++/ocShape.h"
#include	<TopAbs_ShapeEnum.hxx>
#include	<vector>
#include	<cmath>
#include	<string>
#include	"common/geodesic.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ocaIcosphere_.h"

CLASS_TINYSTATE(oc/c++/ocaIcosphere,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ocaIcosphere_(
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


ocaIcosphere_::ocaIcosphere_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
ocaIcosphere_::compute()
{
	ocShape::ensure_init();
	int na = ( args != 0 ) ? args->length() : 0;
	double r      = ( na > 0 ) ? (*args)[0]->get_flt() : 1.0;
	int    subdiv = ( na > 1 ) ? (int)(*args)[1]->get_int() : 0;   /* 細分回数。0=20 面 */
	int    n      = srava_geo::subdiv_to_n(subdiv);
	if ( !(r > 0) ) { result = oca_err(thNEW(stdString,("icosphere: radius must be > 0"))); return; }
	/* ★ #3545 段 5: 幾何の組み立ては **幾何 lib 側**。⇒ この TU は OCCT を触らない
	 *   (触ると投げうる inline を通っただけで RTTI の型インスタンスが .o に出る)。 */
	char why[320]; why[0] = '\0';
	out = ocShape::make_icosphere(r, n, why, (int)sizeof why);
	if ( ! out.is_notNull() ) {
		result = oca_err(thNEW(stdString,( why[0] ? why : "icosphere: failed" )));
		return;
	}

}

/* この演算の結果。エラー時は compute() が result にエラー値を残して本体未設定で return するので
 * result 優先。保存 (Writer 起動) は agent が出力 pigDataCache の set_body 経由で行う。 */
sPtr<pigData>
ocaIcosphere_::get_result()
{
	return ( result != thNULL ) ? result : sPtr<pigData>::d_cast(out);
}
