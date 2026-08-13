/*
 * mfaExtrude — extrude(cross, h) の計算本体(mf 版・cgaExtrude のミラー)。
 * 2D 断面(mfCross)を Z 方向へ h 押し出して 3D(mfMesh)。Manifold::Extrude に委譲(XY→Z・cg と同軸)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"mf/c++/mfMesh.h"
#include	"mf/c++/ptsmfWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/mfaExtrude_.h"

CLASS_TINYSTATE(mf/c++/mfaExtrude,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	mfaExtrude_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<mfMesh>	mesh;
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
class mfMesh;
TS_END_INTERFACE

#endif


mfaExtrude_::mfaExtrude_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
mfaExtrude_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<mfCross> in = ( na > 0 ) ? sPtr<mfCross>::d_cast((*args)[0]) : sPtr<mfCross>();
	double h = ( na > 1 ) ? (*args)[1]->get_flt() : 1.0;
	if ( ! in.is_notNull() ) {
		result = thNEW(pigDataError,(thNEW(stdString,("extrude: needs a 2D polygon"))));
		return;
	}
	if ( h == 0.0 ) {
		result = thNEW(pigDataError,(thNEW(stdString,("extrude: height must be non-zero"))));
		return;
	}
	manifold::Manifold m = manifold::Manifold::Extrude(in->cross().ToPolygons(), h);
	mesh = thNEW(mfMesh,(m));
}

/* この演算の結果 (#3406, 2026-07-30 メモ: get_body/get_result を統一)。エラー時は
 * compute() が result にエラー値を残して mesh を未設定のまま return するので result 優先。
 * 成功時は result=thNULL のままmeshを返す。保存(Writer 起動)は agent が出力 pigDataCache
 * の set_body 経由で行う。 */
sPtr<pigData>
mfaExtrude_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
