/*
 * mfaCircle — 2D primitive の計算本体(mf 版)。mfCross を作る。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"mf/c++/mfMesh.h"
#include	"mf/c++/ptsmfWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	"common/segs.h"   /* ★ #3530: segs / n の共通検査 */
#include	"_ts2/c++/mfaCircle_.h"

CLASS_TINYSTATE(mf/c++/mfaCircle,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	mfaCircle_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<mfCross>	cross;
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
class mfCross;
TS_END_INTERFACE

#endif


mfaCircle_::mfaCircle_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
mfaCircle_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	(void)na;
	double r    = ( na > 0 ) ? (*args)[0]->get_flt() : 1.0;
	int    segs_in = ( na > 1 ) ? (int)(*args)[1]->get_int() : 0;   /* 0 = 未指定 */
	int    segs = 0;
	/* ★★ #3530: segs の意味を全 op / 全カーネルで 1 本に揃えた (src/h/common/segs.h)。
	 *   0 or 省略 = 既定値 / 1,2 / 負 = 明示エラー / 3 以上 = その値。 */
	if ( srava_geo::check_segs(segs_in, 32, &segs) != srava_geo::SEGS_OK ) {
		result = mfa_err(thNEW(stdString,(srava_geo::segs_error("circle").c_str()))); return;
	}
	/* ★ #3516 続き: 他の実装 (occt) が元から持っていた検査を揃えた。
	 *   ⚠ 無いと circle(0) が面積 0 を返し、circle(-1) は cgal=3.12 (≈π) /
	 *   manifold=0 と **カーネルごとに違う値**を黙って返していた。 */
	if ( !(r > 0) ) { result = mfa_err(thNEW(stdString,("circle: radius must be > 0"))); return; }
	cross = mfCross::circle(r, segs);
}

/* この演算の結果 (#3406, 2026-07-30 メモ: get_body/get_result を統一)。エラー時は
 * compute() が result にエラー値を残して cross を未設定のまま return するので result 優先。
 * 成功時は result=thNULL のままcrossを返す。保存(Writer 起動)は agent が出力 pigDataCache
 * の set_body 経由で行う。 */
sPtr<pigData>
mfaCircle_::get_result()
{
	return ( result != thNULL ) ? result : cross;
}
