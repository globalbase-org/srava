/*
 * mfaCircle — 2D primitive の計算本体(mf 版)。mfCross を作る。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"mf/c++/mfMesh.h"
#include	"mf/c++/ptsmfWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
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
	int    segs = ( na > 1 ) ? (int)(*args)[1]->get_int() : 32;
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
