/*
 * mfaMinkowski — minkowski(a, b) — **Minkowski 和 A ⊕ B** (#3511)。manifold 版。
 *
 * ★★ **Manifold は凸分解を使わない** — A の三角形ごとに (3 頂点 ⊕ B) の凸包を取り、
 *   1000 個ずつ BatchBoolean(Add) で畳む。境界の三角形分割を分解の代わりに使うので、
 *   **union の速さがそのまま効く** (nef の CGAL::minkowski_sum_3 は凸分解のペア数 m×n で効く)。
 * ⚠ nef と **bit 一致はしない** (凸分解 + 厳密有理数 対 三角形ごとの凸包 + double)。
 * ⚠ 2D は無い (CrossSection に Minkowski が無い)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"mf/c++/mfMesh.h"
#include	"mf/c++/ptsmfWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	<stdio.h>
#include	"_ts2/c++/mfaMinkowski_.h"

CLASS_TINYSTATE(mf/c++/mfaMinkowski,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	mfaMinkowski_(
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


mfaMinkowski_::mfaMinkowski_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
mfaMinkowski_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<mfMesh> a = ( na > 0 ) ? sPtr<mfMesh>::d_cast((*args)[0]) : sPtr<mfMesh>();
	sPtr<mfMesh> b = ( na > 1 ) ? sPtr<mfMesh>::d_cast((*args)[1]) : sPtr<mfMesh>();
	if ( ! a.is_notNull() || ! b.is_notNull() ) {
		result = mfa_err(thNEW(stdString,("minkowski: needs two 3D meshes")));
		return;
	}
	geom = a->op_minkowski(b);
	if ( ! geom.is_notNull() ) {
		result = mfa_err(thNEW(stdString,("minkowski: computation failed")));
		return;
	}
	/* ★★ #3498: 遅延 CSG 木を **ここで** 評価する (mfaUnion と同じ理由)。
	 *   ⚠ Minkowski は内部で大量の BatchBoolean を積むので、ここを省くと中断が届かない
	 *     区間が最も長くなる op になる。 */
	if ( (result = mf_eval_err(geom, brk_, "minkowski")) != thNULL ) geom = thNULL;
}

/* この演算の結果 (#3406, 2026-07-30 メモ: get_body/get_result を統一)。エラー時は
 * compute() が result にエラー値を残して geom を未設定のまま return するので result 優先。
 * 成功時は result=thNULL のままgeomを返す。保存(Writer 起動)は agent が出力 pigDataCache
 * の set_body 経由で行う。 */
sPtr<pigData>
mfaMinkowski_::get_result()
{
	return ( result != thNULL ) ? result : geom;
}
