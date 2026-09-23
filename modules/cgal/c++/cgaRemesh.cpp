/*
 * cgaRemesh — remesh(m, len[, iter[, sharp]]) — 辺長を揃えて**三角形の形を良くする** (#3512)。cgal 版。
 * 本体は cg_remesh_3d (cgRemesh.cpp)。
 *
 * ★ **面数を減らす op ではない** (それは simplify)。同じ形のまま三角形を張り直す。
 *   単位箱を len=0.25 で回すと 12 面 → 252 面に増え、体積 1 / 面積 6 はそのまま。
 * ★ 効くのは「入力の質」で、用途は 2 つ:
 *   ① 針状三角形 (sliver) はブールの脆さの主因なので、潰してから渡すと頑健性が変わる。
 *      1x1x0.02 の板 (最小角 1.15°) を len=0.05 で回すと **最小角 14.87° / 下位 10% が 32.6°**
 *      まで上がり、体積 0.02・面積 2.08 は動かない (2026-09-12 実測)。
 *   ② 入力の**面密度を揃えた**ベンチが組める (面数の違うモデルを比べていない、と言える)。
 * ⚠ 2D (cg-cross2d) には無い — 等方リメッシュは曲面の三角形分割の話で、多角形領域には対応物が無い。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"cg/c++/cgMesh.h"
#include	"cg/c++/cgaBoolError.h"
#include	"cg/c++/ptscgWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	<stdio.h>
#include	"_ts2/c++/cgaRemesh_.h"

CLASS_TINYSTATE(cg/c++/cgaRemesh,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	cgaRemesh_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<cgMesh>	mesh;     /* 張り直した mesh (get_result が agent へ返す) */
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


cgaRemesh_::cgaRemesh_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
cgaRemesh_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<cgMesh> in = ( na > 0 ) ? sPtr<cgMesh>::d_cast((*args)[0]) : sPtr<cgMesh>();
	if ( ! in.is_notNull() ) {
		result = cga_err(thNEW(stdString,("remesh: needs a mesh")));
		return;
	}
	sPtr<cgMesh3D> in3 = sPtr<cgMesh3D>::d_cast(in);
	if ( ! in3.is_notNull() ) {
		result = cga_err(thNEW(stdString,("remesh: 3D only (a 2D region has no triangles to redo)")));
		return;
	}
	/* ★ 既定値は **op が入れる** (記述子の nreq=2 が「以降は省略可」と言うだけ)。
	 *   iter=3 … CGAL の既定は 1 だが、1 回では辺長のばらつきが残る (実測で最小角の
	 *   下位 10% が目に見えて違う)。sharp=60° … 箱の 90° を拾い、測地球の 41.8° は拾わない。 */
	double len   = ( na > 1 ) ? (*args)[1]->get_flt() : 0.0;
	int    iter  = ( na > 2 ) ? (int)(*args)[2]->get_flt() : 3;
	double sharp = ( na > 3 ) ? (*args)[3]->get_flt() : 60.0;

	const char *msg = 0;
	mesh = cg_remesh_3d(in3, len, iter, sharp, &msg);
	if ( ! mesh.is_notNull() ) {
		char b[256];
		::snprintf(b, sizeof b, "remesh: %s", msg ? msg : "remeshing failed");
		result = cga_err(thNEW(stdString,(b)));
	}
}

/* この演算の結果 (#3406, 2026-07-30 メモ: get_body/get_result を統一)。エラー時は
 * compute() が result にエラー値を残して mesh を未設定のまま return するので result 優先。 */
sPtr<pigData>
cgaRemesh_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
