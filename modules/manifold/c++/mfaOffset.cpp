/*
 * mfaOffset — offset(cross, d, subdiv) の計算本体(mf 版・cgaOffset の 2D ケースのミラー)。
 * 2D(mfCross)を d だけ膨張(d>0)/収縮(d<0)。CrossSection::Offset に委譲(Round 継ぎ)。
 * 3D(mfMesh)の offset(Minkowski)は Manifold に無いので未対応(明示エラー=CGAL を使う)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"mf/c++/mfMesh.h"
#include	"mf/c++/ptsmfWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/mfaOffset_.h"

CLASS_TINYSTATE(mf/c++/mfaOffset,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	mfaOffset_(
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


mfaOffset_::mfaOffset_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
mfaOffset_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<mfCross> in = ( na > 0 ) ? sPtr<mfCross>::d_cast((*args)[0]) : sPtr<mfCross>();
	double d = ( na > 1 ) ? (*args)[1]->get_flt() : 0.0;
	if ( ! in.is_notNull() ) {
		result = thNEW(pigDataError,(thNEW(stdString,(
		    "offset: Manifold kernel supports 2D offset only (use exact kernel for 3D)"))));
		return;
	}
	manifold::CrossSection r = in->cross().Offset(d, manifold::CrossSection::JoinType::Round, 2.0, 0);
	cross = thNEW(mfCross,(r));
}

/* この演算の結果 (#3406, 2026-07-30 メモ: get_body/get_result を統一)。エラー時は
 * compute() が result にエラー値を残して cross を未設定のまま return するので result 優先。
 * 成功時は result=thNULL のままcrossを返す。保存(Writer 起動)は agent が出力 pigDataCache
 * の set_body 経由で行う。 */
sPtr<pigData>
mfaOffset_::get_result()
{
	return ( result != thNULL ) ? result : cross;
}
