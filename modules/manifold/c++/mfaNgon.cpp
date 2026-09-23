/*
 * mfaNgon — 2D primitive の計算本体(mf 版)。mfCross を作る。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"mf/c++/mfMesh.h"
#include	"mf/c++/ptsmfWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	"common/segs.h"   /* ★ #3530: segs / n の共通検査 */
#include	"_ts2/c++/mfaNgon_.h"

CLASS_TINYSTATE(mf/c++/mfaNgon,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	mfaNgon_(
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


mfaNgon_::mfaNgon_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
mfaNgon_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	(void)na;
	int    n = ( na > 0 ) ? (int)(*args)[0]->get_int() : 3;
	double r = ( na > 1 ) ? (*args)[1]->get_flt() : 1.0;
	/* ★ #3516 続き: 他の実装 (occt) が元から持っていた検査を揃えた。
	 *   ⚠ 無いと ngon(2,1) が「2 角形」として面積 1.299 を返し、ngon(6,-1) は
	 *   ngon(6,1) と同じ面積を返していた (符号が黙って落ちる)。 */
	if ( srava_geo::check_sides(n, &n) != srava_geo::SEGS_OK ) {   /* ★ #3530: n は形そのもの = 既定値なし */
		result = mfa_err(thNEW(stdString,(srava_geo::sides_error("ngon").c_str()))); return; }
	if ( !(r > 0) )  { result = mfa_err(thNEW(stdString,("ngon: radius must be > 0"))); return; }
	cross = mfCross::ngon(n, r);
}

/* この演算の結果 (#3406, 2026-07-30 メモ: get_body/get_result を統一)。エラー時は
 * compute() が result にエラー値を残して cross を未設定のまま return するので result 優先。
 * 成功時は result=thNULL のままcrossを返す。保存(Writer 起動)は agent が出力 pigDataCache
 * の set_body 経由で行う。 */
sPtr<pigData>
mfaNgon_::get_result()
{
	return ( result != thNULL ) ? result : cross;
}
