/*
 * cgaRefine — refine(m, len) — **形を厳密に変えずに**面密度だけ上げる (#3512)。cgal 版。
 * 本体は cg_refine_3d (cgRemesh.cpp)。
 *
 * ★ 3 つの「張り直し」op の中で、これだけが **質に手を触れない**:
 *     refine    形も三角形の形も変えない。面数だけ増える  ← これ
 *     remesh    形は保つが三角形を張り直して質を上げる
 *     simplify  形は保つが面数を落とす (質は下がる)
 * ★ 用途は **面密度を揃えた公平なベンチ** (#3510 の「密度を揃えていない」批判への材料)。
 *   ★ 部分三角形が元と相似なので、最小角は実測で **45° → 45°** と一切動かない。
 * ★ cgal 版は **EPECK のまま**回る (新しい頂点が有理数の重心座標)。⇒ 体積が有理数として
 *   厳密に一致する (単位箱で == 1)。remesh / simplify が EPICK コピーに落ちるのとは違う。
 * ⚠ 2D (cg-cross2d) には無い — 三角形を持たない。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"cg/c++/cgMesh.h"
#include	"cg/c++/cgaBoolError.h"
#include	"cg/c++/ptscgWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	<stdio.h>
#include	"_ts2/c++/cgaRefine_.h"

CLASS_TINYSTATE(cg/c++/cgaRefine,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	cgaRefine_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<cgMesh>	mesh;     /* 細分した mesh (get_result が agent へ返す) */
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
class cgMesh;
TS_END_INTERFACE

#endif


cgaRefine_::cgaRefine_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
cgaRefine_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<cgMesh> in = ( na > 0 ) ? sPtr<cgMesh>::d_cast((*args)[0]) : sPtr<cgMesh>();
	if ( ! in.is_notNull() ) {
		result = cga_err(thNEW(stdString,("refine: needs a mesh")));
		return;
	}
	sPtr<cgMesh3D> in3 = sPtr<cgMesh3D>::d_cast(in);
	if ( ! in3.is_notNull() ) {
		result = cga_err(thNEW(stdString,("refine: 3D only (a 2D region has no triangles to split)")));
		return;
	}
	double len = ( na > 1 ) ? (*args)[1]->get_flt() : 0.0;

	const char *msg = 0;
	mesh = cg_refine_3d(in3, len, &msg);
	if ( ! mesh.is_notNull() ) {
		char b[256];
		::snprintf(b, sizeof b, "refine: %s", msg ? msg : "refinement failed");
		result = cga_err(thNEW(stdString,(b)));
	}
}

/* この演算の結果 (#3406, 2026-07-30 メモ: get_body/get_result を統一)。エラー時は
 * compute() が result にエラー値を残して mesh を未設定のまま return するので result 優先。 */
sPtr<pigData>
cgaRefine_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
