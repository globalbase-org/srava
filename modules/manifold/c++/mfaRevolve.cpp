/*
 * mfaRevolve — revolve(cross, angle, segs) の計算本体(mf 版・cgaRevolve のミラー)。
 * 2D 断面(mfCross)を Y 軸まわりに回して 3D(mfMesh)を作る。Manifold::Revolve に委譲。
 *   angle=回転角(度・既定 360)/ segs=全周分割数(既定 32)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"mf/c++/mfMesh.h"
#include	"mf/c++/ptsmfWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/mfaRevolve_.h"

CLASS_TINYSTATE(mf/c++/mfaRevolve,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	mfaRevolve_(
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


mfaRevolve_::mfaRevolve_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
mfaRevolve_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<mfCross> in    = ( na > 0 ) ? sPtr<mfCross>::d_cast((*args)[0]) : sPtr<mfCross>();
	double        angle = ( na > 1 ) ? (*args)[1]->get_flt() : 360.0;
	int           nseg  = ( na > 2 ) ? (int)(*args)[2]->get_int() : 32;
	if ( ! in.is_notNull() ) {
		result = thNEW(pigDataError,(thNEW(stdString,("revolve: needs a 2D polygon"))));
		return;
	}
	if ( angle <= 0.0 ) {
		result = thNEW(pigDataError,(thNEW(stdString,("revolve: angle must be > 0"))));
		return;
	}
	if ( angle > 360.0 ) angle = 360.0;
	if ( nseg < 3 ) nseg = 3;
	if ( nseg > 4096 ) nseg = 4096;
	manifold::Manifold m = manifold::Manifold::Revolve(in->cross().ToPolygons(), nseg, angle);
	/* ★ 軸合わせ: Manifold::Revolve は断面 XY を回して **高さを Z 軸**にする。cgaRevolve は
	 *   profile(x=半径, y=高さ)を **Y 軸**まわりに回す(高さ=Y)。モデルは cg 規約前提(その後
	 *   @("x",90) で Z-up 化する)なので、-90°/X 回転 (x,y,z)→(x,z,-y) で高さを Z→Y に移して一致させる。 */
	sPtr<mfMesh> raw = thNEW(mfMesh,(m));
	static const double e[12] = {
	    1, 0, 0, 0,
	    0, 0, 1, 0,
	    0,-1, 0, 0
	};
	mesh = sPtr<mfMesh>::d_cast(raw->apply_affine(e));   /* apply_affine は sPtr<mfGeom> 返し */
}

/* この演算の結果 (#3406, 2026-07-30 メモ: get_body/get_result を統一)。エラー時は
 * compute() が result にエラー値を残して mesh を未設定のまま return するので result 優先。
 * 成功時は result=thNULL のままmeshを返す。保存(Writer 起動)は agent が出力 pigDataCache
 * の set_body 経由で行う。 */
sPtr<pigData>
mfaRevolve_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
