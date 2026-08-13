/*
 * mfaPolygon — polygon([[x,y],...]) の計算本体(mf 版・cgaPolygon のミラー)。2D 断面 mfCross を作る。
 * revolve/extrude の入力になる。args=[点配列]。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"mf/c++/mfMesh.h"
#include	"mf/c++/ptsmfWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	<vector>
#include	"_ts2/c++/mfaPolygon_.h"

CLASS_TINYSTATE(mf/c++/mfaPolygon,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	mfaPolygon_(
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


mfaPolygon_::mfaPolygon_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
mfaPolygon_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<pigDataArray> pts = ( na > 0 ) ? (*args)[0]->obt_array() : sPtr<pigDataArray>();
	int np = pts.is_notNull() ? pts->length() : 0;
	if ( np < 3 ) {
		result = thNEW(pigDataError,(thNEW(stdString,("polygon: needs >= 3 points [[x,y],...]"))));
		return;
	}
	std::vector<double> xy;
	xy.reserve((size_t)np * 2);
	for ( int i = 0 ; i < np ; ++i ) {
		/* 要素は obt_array() で取る (配列は要素を eager 解決しないので素の d_cast だと in-proc で null)。 */
		sPtr<pigDataArray> p = pts->get_ix(thNEW(pigDataInteger,((INTEGER64)i)))->obt_array();
		if ( ! p.is_notNull() || p->length() < 2 ) {
			result = thNEW(pigDataError,(thNEW(stdString,("polygon: each point must be [x,y]"))));
			return;
		}
		xy.push_back(p->get_ix(thNEW(pigDataInteger,((INTEGER64)0)))->get_flt());
		xy.push_back(p->get_ix(thNEW(pigDataInteger,((INTEGER64)1)))->get_flt());
	}
	cross = mfCross::polygon(&xy[0], np);
}

/* この演算の結果 (#3406, 2026-07-30 メモ: get_body/get_result を統一)。エラー時は
 * compute() が result にエラー値を残して cross を未設定のまま return するので result 優先。
 * 成功時は result=thNULL のままcrossを返す。保存(Writer 起動)は agent が出力 pigDataCache
 * の set_body 経由で行う。 */
sPtr<pigData>
mfaPolygon_::get_result()
{
	return ( result != thNULL ) ? result : cross;
}
