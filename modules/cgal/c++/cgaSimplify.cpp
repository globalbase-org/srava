/*
 * cgaSimplify — simplify(m, n) — **形を保ったまま面数だけ落とす** (#3512)。cgal 版。
 * 本体は cg_simplify_3d (cgRemesh.cpp)。
 *
 * ★★ これが入ると「面数」と「形」を**分離して**測れる。いままで面数を振る唯一の口は
 *   `sphere(r, seg)` の seg だったが、それは **球の族そのものを変えて**しまう (面数と一緒に
 *   囲む体積も動く)。simplify なら 1 つの形から面数だけ違う族を作れる。
 * ★ LindstromTurk を使うのは **体積を保つ制約を解いて**頂点位置を決めるから。実測でも
 *   細かく張り直した単位箱 (7412 面) を 2000 / 500 / 200 / 100 / 50 / 22 面へ落として
 *   **体積 1・面積 6 が動かなかった** (2026-09-12)。
 *   ⚠ 保たれるのは体積と面積で、**三角形の質は落ちる** (同じ実測で最小角が 23.7° → 0.46°)。
 *     質が要るなら simplify の後に remesh を噛ませる = 2 つは対で使う。
 * ⚠ 目標面数は **「以下」で止まる** (CGAL の Face_count_stop_predicate は下から抜ける)。
 *   2000 を頼むと 1998 が返る、という程度のずれが普通に出る。
 * ⚠ 2D (cg-cross2d) には無い — 面を持たないので「面数を落とす」対象が無い。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"cg/c++/cgMesh.h"
#include	"cg/c++/cgaBoolError.h"
#include	"cg/c++/ptscgWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	<stdio.h>
#include	"_ts2/c++/cgaSimplify_.h"

CLASS_TINYSTATE(cg/c++/cgaSimplify,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	cgaSimplify_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<cgMesh>	mesh;     /* 簡約した mesh (get_result が agent へ返す) */
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


cgaSimplify_::cgaSimplify_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
cgaSimplify_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<cgMesh> in = ( na > 0 ) ? sPtr<cgMesh>::d_cast((*args)[0]) : sPtr<cgMesh>();
	if ( ! in.is_notNull() ) {
		result = cga_err(thNEW(stdString,("simplify: needs a mesh")));
		return;
	}
	sPtr<cgMesh3D> in3 = sPtr<cgMesh3D>::d_cast(in);
	if ( ! in3.is_notNull() ) {
		result = cga_err(thNEW(stdString,("simplify: 3D only (a 2D region has no faces to drop)")));
		return;
	}
	int nfaces = ( na > 1 ) ? (int)(*args)[1]->get_flt() : 0;

	const char *msg = 0;
	mesh = cg_simplify_3d(in3, nfaces, &msg);
	if ( ! mesh.is_notNull() ) {
		char b[256];
		::snprintf(b, sizeof b, "simplify: %s", msg ? msg : "simplification failed");
		result = cga_err(thNEW(stdString,(b)));
	}
}

/* この演算の結果 (#3406, 2026-07-30 メモ: get_body/get_result を統一)。エラー時は
 * compute() が result にエラー値を残して mesh を未設定のまま return するので result 優先。 */
sPtr<pigData>
cgaSimplify_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
