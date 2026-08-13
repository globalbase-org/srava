/*
 * mfaRect — 2D primitive の計算本体(mf 版)。mfCross を作る。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"mf/c++/mfMesh.h"
#include	"mf/c++/ptsmfWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/mfaRect_.h"

CLASS_TINYSTATE(mf/c++/mfaRect,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	mfaRect_(
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


mfaRect_::mfaRect_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
mfaRect_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	(void)na;
	double w = ( na > 0 ) ? (*args)[0]->get_flt() : 1.0;
	double h = ( na > 1 ) ? (*args)[1]->get_flt() : 1.0;
	if ( w <= 0.0 || h <= 0.0 ) {
		result = thNEW(pigDataError,(thNEW(stdString,("rect: width and height must be positive"))));
		return;
	}
	cross = mfCross::rect(w, h);
}

/* この演算の結果 (#3406, 2026-07-30 メモ: get_body/get_result を統一)。エラー時は
 * compute() が result にエラー値を残して cross を未設定のまま return するので result 優先。
 * 成功時は result=thNULL のままcrossを返す。保存(Writer 起動)は agent が出力 pigDataCache
 * の set_body 経由で行う。 */
sPtr<pigData>
mfaRect_::get_result()
{
	return ( result != thNULL ) ? result : cross;
}
