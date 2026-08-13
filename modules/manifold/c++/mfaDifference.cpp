/*
 * mfaDifference — 2 メッシュの和(mf 版・cgaDifference のミラー)。args=[meshA, meshB]=mfMesh(reader が decode)。
 * Manifold の + で和を取り mfMesh に保持。get_result() で agent へ返す(保存は set_body 経由・#3406 2026-07-30: get_body 統合)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"mf/c++/mfMesh.h"
#include	"mf/c++/ptsmfWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/mfaDifference_.h"

CLASS_TINYSTATE(mf/c++/mfaDifference,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	mfaDifference_(
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


mfaDifference_::mfaDifference_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
mfaDifference_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<pigData> a = ( na > 0 ) ? (*args)[0] : sPtr<pigData>();
	sPtr<pigData> b = ( na > 1 ) ? (*args)[1] : sPtr<pigData>();
	sPtr<mfMesh>  a3 = sPtr<mfMesh>::d_cast(a),  b3 = sPtr<mfMesh>::d_cast(b);
	sPtr<mfCross> a2 = sPtr<mfCross>::d_cast(a), b2 = sPtr<mfCross>::d_cast(b);
	if ( a3.is_notNull() && b3.is_notNull() )      geom = a3->op_difference(b3);   /* 3D */
	else if ( a2.is_notNull() && b2.is_notNull() ) geom = a2->op_difference(b2);   /* 2D */
	else {
		result = thNEW(pigDataError,(thNEW(stdString,("difference: needs two meshes"))));
		return;
	}
	if ( ! geom.is_notNull() )
		result = thNEW(pigDataError,(thNEW(stdString,("difference: boolean failed"))));
}

/* この演算の結果 (#3406, 2026-07-30 メモ: get_body/get_result を統一)。エラー時は
 * compute() が result にエラー値を残して geom を未設定のまま return するので result 優先。
 * 成功時は result=thNULL のままgeomを返す。保存(Writer 起動)は agent が出力 pigDataCache
 * の set_body 経由で行う。 */
sPtr<pigData>
mfaDifference_::get_result()
{
	return ( result != thNULL ) ? result : geom;
}
