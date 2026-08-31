/*
 * mfaIntersection — 2 メッシュの和(mf 版・cgaIntersection のミラー)。args=[meshA, meshB]=mfMesh(reader が decode)。
 * Manifold の + で和を取り mfMesh に保持。get_result() で agent へ返す(保存は set_body 経由・#3406 2026-07-30: get_body 統合)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"mf/c++/mfMesh.h"
#include	"mf/c++/ptsmfWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	<stdio.h>
#include	"_ts2/c++/mfaIntersection_.h"

CLASS_TINYSTATE(mf/c++/mfaIntersection,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	mfaIntersection_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<mfGeom>	geom;
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
class mfGeom;
class mfCross;
TS_END_INTERFACE

#endif


mfaIntersection_::mfaIntersection_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
mfaIntersection_::compute()
{
	/* ★ #3436 P4: n 項で受ける。3 項以上は BatchBoolean (1 つの CSG ノード) へ。 */
	const char *msg = 0;
	geom = mf_bool_from_args(args, "intersection", &msg);
	if ( ! geom.is_notNull() ) {
		char b[128];
		::snprintf(b, sizeof b, "intersection: %s", msg ? msg : "boolean failed");
		result = thNEW(pigDataError,(thNEW(stdString,(b))));
	}
}

/* この演算の結果 (#3406, 2026-07-30 メモ: get_body/get_result を統一)。エラー時は
 * compute() が result にエラー値を残して geom を未設定のまま return するので result 優先。
 * 成功時は result=thNULL のままgeomを返す。保存(Writer 起動)は agent が出力 pigDataCache
 * の set_body 経由で行う。 */
sPtr<pigData>
mfaIntersection_::get_result()
{
	return ( result != thNULL ) ? result : geom;
}
