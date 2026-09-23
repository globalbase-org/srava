/*
 * guaShellAt — shell_at(m, [x,y,z]) — 点に **いちばん近い殻** を取り出す op (#3527 段 4)。
 *
 * ★ 殻は曲面なので「含む」が定義できない ⇒ 最近傍しか言えない (part_at との非対称の理由)。
 * ⚠⚠ 同距離の殻が 2 つ以上あるときは **断る**。黙って片方を選ぶと「同じ式に 2 通りの値」に
 *   なる (#3516 / #3518-1 で潰してきた穴)。
 *   ★ 中空の箱の z=0.5 平面上の点がまさにそれ — 外殻の底 (z=0) と空洞の底 (z=1) から等距離。
 * ⚠ cgal は EPECK の厳密比較で同距離を見るが、こちらは double ⇒ **相対許容差**で見る。
 *   向きは安全側 — 許容差を持たせると *断る側* に倒れる。厳密比較にすると丸めで同距離が
 *   同距離に見えなくなり、**黙って片方を選ぶ**という一番まずい形になる。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"gu/c++/guGeom.h"
#include	"common/affine.h"    /* point3 — 点 [x,y,z] の読み方と拒否の文言を一本化 */
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/guaShellAt_.h"
#include	<stdio.h>

CLASS_TINYSTATE(gu/c++/guaShellAt,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	guaShellAt_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	/* ⚠ get_result は **public 側**に置く。protected だと codegen が外側クラスへの
	 *   転送を作らず、値は作れているのに format 'TEXT' で保存される (2026-09-17 に踏んだ)。 */
	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<guGeom>	geom;
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
TS_END_INTERFACE

#endif

guaShellAt_::guaShellAt_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
guaShellAt_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<guGeom> in = ( na > 0 ) ? sPtr<guGeom>::d_cast((*args)[0]) : sPtr<guGeom>();
	if ( ! in.is_notNull() ) {
		result = gua_err(thNEW(stdString,("shell_at: needs a mesh or 2D region")));
		return;
	}
	double p[3];
	const char *why = 0;
	char buf[256];
	if ( ! srava_affine::point3(( na > 1 ) ? (*args)[1] : sPtr<pigData>(),
	                            "shell_at", p, &why, buf, (int)sizeof buf) ) {
		result = gua_err(thNEW(stdString,(why)));
		return;
	}
	why = 0;
	geom = in->op_shell_at(p, &why);
	if ( ! geom.is_notNull() ) {
		char b[352];
		::snprintf(b, sizeof b, "shell_at: %s", why ? why : "could not extract it");
		result = gua_err(thNEW(stdString,(b)));
	}
}

/* エラー時は compute() が result にエラーを残すので result 優先。 */
sPtr<pigData>
guaShellAt_::get_result()
{
	return ( result != thNULL ) ? result : sPtr<pigData>::d_cast(geom);
}
