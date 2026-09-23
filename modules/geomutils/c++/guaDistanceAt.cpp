/*
 * guaDistanceAt — distance_at(v, p) — **点から値までの最短距離** (#3553・2026-09-18)。
 *
 * ★★ ひさ裁定: **新しい意味を作らない**。@*-face3d@ / @*-cross2d@ はどちらも
 *   *3D に埋め込まれた 2 次元* なので、3D の定義 (p から **面の集合** までの最短距離・符号なし)
 *   がそのまま当てはまる。⇒ occt / cgal / geogram / openvdb と同じ約束。
 *
 * ★ これで @mf@ / @ch@ は **初めて** distance_at を持つ (それまで 1 つも無かった)。
 *   2D 側 (mf-cross2d / mf-face3d) も同様。⇒ 1 実装で配る (#3527 の形)。
 * ⚠⚠ @gg-mesh3d@ は **sig に載せない** — geogram は @MeshFacetsAABB@ で自前に持っており、
 *   priority でこちらが勝つと *速い実装を総当たりで置き換える* ことになる。
 *   ⇒ 「歯抜けを埋める」ためのモジュールが、既にあるものを奪ってはいけない。
 * ⚠ @cg-*@ も載せない (cgal は AABB_tree で 3D を持つ・2D は現状のまま = ひさ判断)。
 *
 * ⚠ 実装は **総当たり** (AABB を持たない) ので O(nt)。大きなメッシュを何度も測るなら
 *   cgal / geogram を通すこと (meshprops.h の distance_at の注記)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"gu/c++/guGeom.h"
#include	"common/affine.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/guaDistanceAt_.h"
#include	<stdio.h>

CLASS_TINYSTATE(gu/c++/guaDistanceAt,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	guaDistanceAt_(
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

guaDistanceAt_::guaDistanceAt_(TS_ARGS0)
	: ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
	TS_CPARGS0
}

void
guaDistanceAt_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<guGeom> in = ( na > 0 ) ? sPtr<guGeom>::d_cast((*args)[0]) : sPtr<guGeom>();
	if ( ! in.is_notNull() ) {
		result = gua_err(thNEW(stdString,("distance_at: needs a mesh or 2D region")));
		return;
	}
	double p[3];
	const char *why = 0;
	char buf[256];
	/* ★ 点の綴りの検査は共通ヘッダに 1 本 (2 成分 = z=0 として受ける・affine.h)。 */
	if ( ! srava_affine::point3(( na > 1 ) ? (*args)[1] : sPtr<pigData>(),
	                            "distance_at", p, &why, buf, (int)sizeof buf) ) {
		result = gua_err(thNEW(stdString,(why)));
		return;
	}
	double d = 0.0;
	if ( ! in->op_distance_at(p, &d, &why) ) {
		char b[320];
		::snprintf(b, sizeof b, "distance_at: %s", ( why != 0 ) ? why : "could not measure");
		result = gua_err(thNEW(stdString,(b)));
		return;
	}
	result = thNEW(pigDataFloat,(d));
}
