/*
 * mfaSimplifyCleanup — simplify_cleanup(m, tol) — **許容差以下の特徴を潰して掃除する** (#3512 続き)。
 * manifold 版。本体は mf_simplify_cleanup (mfMesh.cpp)。
 *
 * ★ 名前が `simplify_cleanup` なのは **約束が simplify と違う**から (命名は「元の op 名 + _修飾」):
 *     simplify(m, n)          面数を n 以下にする。**形は保つ** (体積が動かない)     … cgal
 *     simplify_cleanup(m,tol) **面がtol未満しか動かない**範囲で潰せるだけ潰す       … manifold
 *   前者は「面数」を指定して形を守り、後者は「形のずれの上限」を指定して面数は成り行き。
 *   ⇒ 同じ名前にすると *どちらを約束したのか言えなくなる*。
 * ★ 上流の約束: 結果は **元の頂点の部分集合**で、どの面も tol 未満しか動かない
 *   ⇒ 新しい座標を作らないので、ブール後に残った極小辺・針状三角形の掃除に向く。
 * ⚠ 効き始めると形は動く (球 r=1 で tol=0.03 のとき体積 −2%・0.1 で −10.8%)。
 *   **面数だけ振りたい測定には使わないこと** (それは cgal の simplify)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"mf/c++/mfMesh.h"
#include	"mf/c++/ptsmfWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	<stdio.h>
#include	"_ts2/c++/mfaSimplifyCleanup_.h"

CLASS_TINYSTATE(mf/c++/mfaSimplifyCleanup,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	mfaSimplifyCleanup_(
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
TS_END_INTERFACE

#endif


mfaSimplifyCleanup_::mfaSimplifyCleanup_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
mfaSimplifyCleanup_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<mfGeom> in = ( na > 0 ) ? sPtr<mfGeom>::d_cast((*args)[0]) : sPtr<mfGeom>();
	if ( ! in.is_notNull() ) {
		result = mfa_err(thNEW(stdString,("simplify_cleanup: needs a mesh")));
		return;
	}
	sPtr<mfMesh> in3 = sPtr<mfMesh>::d_cast(in);
	if ( ! in3.is_notNull() ) {
		result = mfa_err(thNEW(stdString,("simplify_cleanup: 3D only (a 2D region has no faces to drop)")));
		return;
	}
	double tol = ( na > 1 ) ? (*args)[1]->get_flt() : 0.0;

	const char *msg = 0;
	geom = mf_simplify_cleanup(in3, tol, &msg);
	if ( ! geom.is_notNull() ) {
		char b[128];
		::snprintf(b, sizeof b, "simplify_cleanup: %s", msg ? msg : "simplification failed");
		result = mfa_err(thNEW(stdString,(b)));
		return;
	}
	/* ★ #3498: Simplify は deferred op なので、評価点をここへ引き出して中断を取る。 */
	if ( (result = mf_eval_err(geom, brk_, "simplify_cleanup")) != thNULL ) { geom = thNULL; return; }
}

sPtr<pigData>
mfaSimplifyCleanup_::get_result()
{
	return ( result != thNULL ) ? result : geom;
}
