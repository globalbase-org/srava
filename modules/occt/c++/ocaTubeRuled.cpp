/*
 * ocaTubeRuled — occt の tube_ruled(path[, opts]) (#3593)。path = [[[x,y,z], r], …]。
 *
 * ★★★ なぜ要るのか (#3593)
 *
 *   掃引系の op は「なめらか / 線織」の **対**で揃えてある:
 *
 *       loft   (occt)                    loft_ruled (cgal / manifold / occt)
 *       tube   (occt)                    tube_ruled (cgal / cherchi / geogram / manifold /
 *                                                    nef_hybrid / nef_snc / openvdb)
 *
 *   ⇒ tube だけ occt が **線織版を持たない**非対称だった。occt を指名したまま
 *     「角の尖った管」を書く手段が無く、@"occt"::tube@ で代用すると *別の形*になる
 *     (スプラインの背骨は角を丸めるので、角のあるパスでは体積がはっきり膨らむ)。
 *
 * ★★ 分かれ目は **背骨の性質だけ**。断面はどちらも **厳密な円**のまま:
 *
 *       tube         点を通る C2 の B-spline を背骨にする      → 角が丸い
 *       tube_ruled   点を直線で結んだ折れ線を背骨にする        → 角が尖る (留め継ぎ)
 *
 *   これは loft / loft_ruled とまったく同じ作り方である (あちらも断面は厳密な平面のまま、
 *   断面「間」をなめらかに通すか直線で結ぶかだけが違う)。
 *
 * ⚠⚠ **cgal / manifold の tube_ruled とは一致しない**。背骨は同じ折れ線になるが、
 *   向こうは断面が segs 角形の近似で、こちらは厳密な円。⇒ 体積は構造的に違う
 *   (occt の sphere / cylinder / circle を一致検査に入れられないのと同じ理由)。
 *   ★ segs は **無視する** — 断面を近似しないので分割数に意味が無い。
 *
 * 実体は @ocShape::tube_from_args@ (2 つの op で共有・@ruled@ フラグだけが違う)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"oc/c++/ocShape.h"
#include	"ts2/c++/stdString.h"
#include	<stdio.h>
#include	"_ts2/c++/ocaTubeRuled_.h"

CLASS_TINYSTATE(oc/c++/ocaTubeRuled,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ocaTubeRuled_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	/* ⚠ get_result は **public** に書く。protected に書くと外側クラスへの転送 thunk が
	 *   生成されず、基底の get_result が使われて **値 (TEXT) が返る** (ocaTube で実際に踏んだ)。 */
	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<ocShape>	out;
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
class ocShape;
TS_END_INTERFACE

#endif

ocaTubeRuled_::ocaTubeRuled_(TS_ARGS0)
	: ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
	TS_CPARGS0
}

void
ocaTubeRuled_::compute()
{
	ocShape::ensure_init();   /* ★ OCCT の診断出力を stdout から外す (ocShape.h 参照) */
	const char *msg = 0;
	char why[512];
	why[0] = '\0';
	out = ocShape::tube_from_args(args, true /* polyline spine */, &msg, why, (int)sizeof why);
	if ( ! out.is_notNull() ) {
		char b[700];
		::snprintf(b, sizeof b, "tube_ruled: %s%s%s", msg ? msg : "failed",
		           why[0] ? " — " : "", why[0] ? why : "");
		result = oca_err(thNEW(stdString,(b)));
	}
}

sPtr<pigData>
ocaTubeRuled_::get_result()
{
	return ( result != thNULL ) ? result : out;
}
