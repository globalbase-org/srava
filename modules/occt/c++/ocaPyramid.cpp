/*
 * ocaPyramid — pyramid(n, h, r) — 正 n 角錐 (底面は z=0 の XY 平面・頂点は z=h) の計算本体 (occt 版・#3474)。
 * ★ 平面多面体なので **メッシュ系と厳密に一致する** (prism / box と同じ)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"oc/c++/ocShape.h"
#include	"common/solids.h"   /* SOLID_PI (座標規約を共通ヘッダと共有) */

#include	<TopAbs_ShapeEnum.hxx>
#include	<vector>
#include	<cmath>
#include	<string>
#include	"ts2/c++/stdString.h"
#include	"common/segs.h"   /* ★ #3530: segs / n の共通検査 */
#include	"_ts2/c++/ocaPyramid_.h"

CLASS_TINYSTATE(oc/c++/ocaPyramid,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ocaPyramid_(
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


ocaPyramid_::ocaPyramid_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
ocaPyramid_::compute()
{
	ocShape::ensure_init();
	int na = ( args != 0 ) ? args->length() : 0;
	int    n = ( na > 0 ) ? (int)(*args)[0]->get_int() : 3;
	double h = ( na > 1 ) ? (*args)[1]->get_flt() : 1.0;
	double r = ( na > 2 ) ? (*args)[2]->get_flt() : 1.0;
	if ( srava_geo::check_sides(n, &n) != srava_geo::SEGS_OK ) {   /* ★ #3530: n は形そのもの = 既定値なし */
		result = oca_err(thNEW(stdString,(srava_geo::sides_error("pyramid").c_str()))); return; }
	if ( !(h > 0) ) { result = oca_err(thNEW(stdString,("pyramid: height must be > 0"))); return; }
	if ( !(r > 0) ) { result = oca_err(thNEW(stdString,("pyramid: radius must be > 0"))); return; }
	/* ★ #3545 段 5: 幾何の組み立ては **幾何 lib 側**。⇒ この TU は OCCT を触らない
	 *   (触ると投げうる inline を通っただけで RTTI の型インスタンスが .o に出る)。 */
	char why[320]; why[0] = '\0';
	out = ocShape::make_pyramid(n, h, r, why, (int)sizeof why);
	if ( ! out.is_notNull() ) {
		result = oca_err(thNEW(stdString,( why[0] ? why : "pyramid: failed" )));
		return;
	}

}

/* この演算の結果。エラー時は compute() が result にエラー値を残して本体未設定で return するので
 * result 優先。保存 (Writer 起動) は agent が出力 pigDataCache の set_body 経由で行う。 */
sPtr<pigData>
ocaPyramid_::get_result()
{
	return ( result != thNULL ) ? result : sPtr<pigData>::d_cast(out);
}
