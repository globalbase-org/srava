/*
 * mfaRefine — refine(m, len) — **形を厳密に変えずに**面密度だけ上げる (#3512)。manifold 版。
 * 本体は mf_refine_to_length (mfMesh.cpp)。
 *
 * ★ cgal 版と **同じ約束**: 形は変わらない / すべての辺が len 以下になる / 面数は増える。
 *   ⚠ 作り方は違う — manifold は辺ごとに分割数を選んで**内部頂点も足す**ので少ない面数で
 *     済むが、三角形の形は変わる (単位箱で最小角 45° → 23.2°)。cgal は全面を同じ n で
 *     **相似分割**するので面数は増えるかわり最小角が動かない。どちらも *質を上げる op ではない*
 *     (それは remesh)。数値は docs/srava_module_reference.md の各節。
 * ★ RefineToLength は manifold の **eager op** なので ExecutionContext で中断できる (#3498)。
 * ⚠ 2D (mf-cross2d) には無い — CrossSection に対応物が無い。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"mf/c++/mfMesh.h"
#include	"mf/c++/ptsmfWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	<stdio.h>
#include	"_ts2/c++/mfaRefine_.h"

CLASS_TINYSTATE(mf/c++/mfaRefine,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	mfaRefine_(
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


mfaRefine_::mfaRefine_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
mfaRefine_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<mfGeom> in = ( na > 0 ) ? sPtr<mfGeom>::d_cast((*args)[0]) : sPtr<mfGeom>();
	if ( ! in.is_notNull() ) {
		result = mfa_err(thNEW(stdString,("refine: needs a mesh")));
		return;
	}
	sPtr<mfMesh> in3 = sPtr<mfMesh>::d_cast(in);
	if ( ! in3.is_notNull() ) {
		result = mfa_err(thNEW(stdString,("refine: 3D only (a 2D region has no triangles to split)")));
		return;
	}
	double len = ( na > 1 ) ? (*args)[1]->get_flt() : 0.0;

	const char *msg = 0;
	geom = mf_refine_to_length(in3, len, brk_, &msg);
	if ( ! geom.is_notNull() ) {
		char b[128];
		::snprintf(b, sizeof b, "refine: %s", msg ? msg : "refinement failed");
		result = mfa_err(thNEW(stdString,(b)));
		return;
	}
	/* ★ #3498: 入力が遅延 CSG 木だった場合に備えて評価点を compute() の中へ引き出しておく
	 *   (RefineToLength 自体は eager なので、ここで止まるのは入力側の木)。 */
	if ( (result = mf_eval_err(geom, brk_, "refine")) != thNULL ) { geom = thNULL; return; }
}

sPtr<pigData>
mfaRefine_::get_result()
{
	return ( result != thNULL ) ? result : geom;
}
