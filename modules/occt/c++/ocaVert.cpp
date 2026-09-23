/*
 * ocaVert — vert(v, i) — **i 番目の頂点の座標** (#3547 ④ / #3527 の規約③)。
 *
 * ⚠⚠ occt の「頂点」は **稜の端点**であって三角形の頂点ではない
 *   (立方体 = 8 ・ 球 = 2 (極) ・ 円 = 1 (継ぎ目))。⇒ mesh 系の vert とは *数え方が違う* が、
 *   それがこの表現の要点なので同じ op 名で出す (nverts / nfaces と同じ立場)。
 * ★ 並びは nverts / verts と同じ地図 ⇒ **verts(v) の i 番目 == vert(v,i)**。
 * ★ 成分数は名乗りの規約どおり — oc-cross2d は 2 ・ それ以外は world の 3 (bbox / centroid と同じ)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"oc/c++/ocShape.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ocaVert_.h"
#include	<stdio.h>

CLASS_TINYSTATE(oc/c++/ocaVert,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ocaVert_(
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
TS_END_INTERFACE

#endif

ocaVert_::ocaVert_(TS_ARGS0)
	: ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
	TS_CPARGS0
}

void
ocaVert_::compute()
{
	ocShape::ensure_init();
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<ocShape>  s3 = ( na > 0 ) ? sPtr<ocShape>::d_cast((*args)[0])  : sPtr<ocShape>();
	sPtr<ocFace2D> f2 = ( na > 0 ) ? sPtr<ocFace2D>::d_cast((*args)[0]) : sPtr<ocFace2D>();
	if ( ! s3.is_notNull() && ! f2.is_notNull() ) {
		result = oca_err(thNEW(stdString,("vert: needs an OCCT shape or 2D region")));
		return;
	}
	const long i = ( na > 1 ) ? (long)(*args)[1]->get_int() : 0;
	const int n = s3.is_notNull() ? s3->nverts() : f2->nverts();
	double p[3];
	const int ok = s3.is_notNull() ? s3->vert_at((int)i, p) : f2->vert_at((int)i, p);
	if ( ! ok ) {
		/* ⚠ 範囲を **数字で**言う (「範囲外」だけだと利用者が自分で nverts を打つことになる)。 */
		char b[192];
		::snprintf(b, sizeof b,
		    "vert: index %ld is out of range (this value has %d vertex%s; valid indices are 0..%d)",
		    i, n, ( n == 1 ) ? "" : "es", ( n > 0 ) ? n - 1 : 0);
		result = oca_err(thNEW(stdString,(b)));
		return;
	}
	/* ★ 成分数の規約は bbox / centroid と同じ (ocaBbox.cpp の注記)。 */
	const int nc = ( f2.is_notNull() && f2->on_z0_plane() ) ? 2 : 3;
	sPtr<pigDataArray> arr = thNEW(pigDataArray,());
	for ( int k = 0 ; k < nc ; ++k ) arr->push(thNEW(pigDataFloat,(p[k])));
	result = arr;
}
