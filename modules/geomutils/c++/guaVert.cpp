/*
 * guaVert — vert(v, i) — **i 番目の頂点の座標** (#3527 段 5)。
 *
 * ★ 3 つ組 (数える / 取り出す / 位置で指す) の「取り出す」側。nverts で数えられるのに
 *   **座標を読む op がどのモジュールにも 1 本も無かった**のが #3527 の「やること 4」。
 *   cgal には 09-17 の午前に入り、ここで mf / gg / ch へ広がる。
 *
 * ★ 返りの成分数は bbox / centroid と **同じ約束**:
 *     3D … [x,y,z]   /   cross2d … [x,y]   /   face3d … **[x,y,z] (world)**
 *   ⚠⚠ face3d を枠の中の 2 成分で返すと、違う平面に置いた同じ形が同じ答えを返し
 *     *置き場所が黙って落ちる*。#3533 が bbox / centroid で決めた「face3d は world」へ
 *     揃えた (ひさ判断 2026-09-17。同時に cgal 側も直した)。
 *
 * ⚠ 索引は **実装依存** (頂点の格納順)。位置で指す口はここには無い — 点は片ではないので
 *   「その位置に在る頂点」は *最近傍* の話になり、closest(p, …) が既にその役をしている。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"gu/c++/guGeom.h"

#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/guaVert_.h"
#include	<stdio.h>

CLASS_TINYSTATE(gu/c++/guaVert,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	guaVert_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

protected:
	virtual void	compute();
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
class guGeom;
class ptCloud;
TS_END_INTERFACE

#endif

guaVert_::guaVert_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
guaVert_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<guGeom> in = ( na > 0 ) ? sPtr<guGeom>::d_cast((*args)[0]) : sPtr<guGeom>();
	if ( ! in.is_notNull() ) {
		result = gua_err(thNEW(stdString,("vert: needs a mesh or 2D region")));
		return;
	}
	const int idx = ( na > 1 ) ? (int)(*args)[1]->get_int() : 0;
	const int n   = in->op_nverts();
	if ( idx < 0 || idx >= n ) {
		char b[176];
		::snprintf(b, sizeof b, "vert: index %d is out of range (the value has %d vertex/vertices)", idx, n);
		result = gua_err(thNEW(stdString,(b)));
		return;
	}
	double p[3] = {0,0,0};
	const int dim = in->op_vert(idx, p);
	if ( dim <= 0 ) {
		result = gua_err(thNEW(stdString,("vert: could not read the vertex")));
		return;
	}
	/* ★ 返りの次元ぶんだけ並べる (bbox / centroid と同じ約束)。 */
	sPtr<pigDataArray> arr = thNEW(pigDataArray,());
	for ( int i = 0 ; i < dim ; ++i ) arr->push(thNEW(pigDataFloat,(p[i])));
	result = arr;
}
