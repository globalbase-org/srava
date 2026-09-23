/*
 * cgaPartAt — part_at(v, [x,y,z]) — 点を **含む** 片 (#3527 の 3 つ組を cgal で揃える)。
 *
 * ⚠⚠ **なぜ後から足したか**: #3510 の能力表に「片を取り出す」の行を立てたら、cgal だけ 3/4 で
 *   @part_at@ が無いと出た (2026-09-18)。#3527 の規約③「片の名前を付けたら **3 つ組で名乗る**」を
 *   *規約を決めた当の cgal が満たしていなかった*。⇒ **表を作ったことが歯抜けを見つけた**。
 *
 * ★★ @shell_at@ (いちばん近い殻) と意味が **わざと違う** — こちらは「**含む**」。
 *   立体は内側を持ち、曲面は持たない。⇒ 空洞の中と立体の外は「そこに材料は無い」と明示エラー。
 * ★ cgal は **厳密**に判定する (3D = Side_of_triangle_mesh / 2D = Polygon_2::bounded_side)。
 *   geomutils は double の巻き数なので、境界の近くはこちらのほうが強い。約束は同じ。
 * ⚠ 2D の点は **world**。#3533 と 段 5 で bbox / centroid / vert が world に揃ったので、
 *   ここだけ局所座標にすると *同じ値に 2 通りの座標系* ができる。面外は断る (射影しない)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"cg/c++/cgMesh.h"
#include	"cg/c++/ptscgWireCacheStreamWriterMesh.h"
#include	"common/affine.h"    /* point3 — 点 [x,y,z] の読み方と拒否の文言を一本化 */
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/cgaPartAt_.h"
#include	<stdio.h>

CLASS_TINYSTATE(cg/c++/cgaPartAt,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	cgaPartAt_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	/* ⚠ get_result は **public 側**に置く (protected だと codegen が転送を作らない)。 */
	virtual sPtr<pigData>	get_result();
	sPtr<cgMesh>		mesh;

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
class cgMesh;
TS_END_INTERFACE

#endif

cgaPartAt_::cgaPartAt_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
cgaPartAt_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	double p[3];
	const char *why = 0;
	char buf[256];
	if ( ! srava_affine::point3(( na > 1 ) ? (*args)[1] : sPtr<pigData>(),
	                            "part_at", p, &why, buf, (int)sizeof buf) ) {
		result = cga_err(thNEW(stdString,(why)));
		return;
	}
	sPtr<cgMesh2D> in2 = ( na > 0 ) ? sPtr<cgMesh2D>::d_cast((*args)[0]) : sPtr<cgMesh2D>();
	if ( in2.is_notNull() ) {
		const int i = in2->op_part_at(p);
		if ( i == -3 ) {
			result = cga_err(thNEW(stdString,(
			    "part_at: that point is not on the plane of this 2D region, so it cannot pick a "
			    "piece of it (part_at never projects the point — give a point on the plane, or "
			    "flatten the region with project_flatten(v) first)")));
			return;
		}
		if ( i == -1 ) {
			result = cga_err(thNEW(stdString,(
			    "part_at: no piece of this 2D region is at that point (the point is outside the "
			    "region, or inside a hole, where there is no material)")));
			return;
		}
		if ( i == -2 ) {
			result = cga_err(thNEW(stdString,(
			    "part_at: more than one piece of this 2D region contains that point, so its pieces "
			    "overlap (the region is not valid); check valid(v) first")));
			return;
		}
		mesh = sPtr<cgMesh>::d_cast(in2->op_part(i));
		if ( ! mesh.is_notNull() )
			result = cga_err(thNEW(stdString,("part_at: could not extract the part")));
		return;
	}
	sPtr<cgMesh3D> in3 = ( na > 0 ) ? sPtr<cgMesh3D>::d_cast((*args)[0]) : sPtr<cgMesh3D>();
	if ( ! in3.is_notNull() ) {
		result = cga_err(thNEW(stdString,("part_at: needs a mesh or a 2D region")));
		return;
	}
	why = 0;
	mesh = sPtr<cgMesh>::d_cast(in3->op_part_at(p, &why));
	if ( ! mesh.is_notNull() ) {
		char b[352];
		::snprintf(b, sizeof b, "part_at: %s", why ? why : "could not extract the part");
		result = cga_err(thNEW(stdString,(b)));
	}
}

/* エラー時は compute() が result にエラーを残すので result 優先。 */
sPtr<pigData>
cgaPartAt_::get_result()
{
	return ( result != thNULL ) ? result : sPtr<pigData>::d_cast(mesh);
}
