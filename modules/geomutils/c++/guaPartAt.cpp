/*
 * guaPartAt — part_at(v, [x,y,z]) — 点を **含む** 塊を取り出す op (#3527 段 4)。
 *
 * ★★ @shell_at@ が「いちばん近い殻」なのに、こちらは **含む**。非対称に見えるが理由がある:
 *     **立体は内側を持ち、曲面は持たない**。殻は面の連結成分 (曲面) なので「含む」が
 *     定義できず最近傍しか言えない。塊は立体なので内外が言える。
 *   ⇒ どちらも「その位置に在る片を指す」という 1 つの規約の、次元による 2 つの姿。
 *
 * ★ 判定は **塊の巻き数** = その塊を作る殻の巻き数の和 (外殻 +1 / 空洞 -1)。
 *   ⇒ 材料の中でだけ 1 になり、**空洞の中では 0**。⇒「空洞の中に塊は無い」と正しく答える。
 *   ⚠ 最近傍で代用するとここが嘘になる (空洞の中でも塊が返ってしまう)。
 *
 * ★ 番号と 2 通り要るわけ: 番号は **列挙のため**で指す先が無い。モデルの書き方を変えると
 *   片の集合そのものが変わるので番号は当然別の片を指す。⇒ 書き換えても同じ片を指し続けたい
 *   なら **位置で指すしかない** (occt の face / face_at が同じ理由で 2 通りある・#3518)。
 *
 * ⚠ 2D では点は **その 2D の平面上** になければならない。面外なら断る — 黙って射影すると
 *   「平面の外の点で訊いたのに答えが返る」= 嘘になる (#3534 と同じ線引き)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"gu/c++/guGeom.h"
#include	"common/affine.h"    /* point3 — 点 [x,y,z] の読み方と拒否の文言を一本化 */
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/guaPartAt_.h"
#include	<stdio.h>

CLASS_TINYSTATE(gu/c++/guaPartAt,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	guaPartAt_(
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

guaPartAt_::guaPartAt_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
guaPartAt_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<guGeom> in = ( na > 0 ) ? sPtr<guGeom>::d_cast((*args)[0]) : sPtr<guGeom>();
	if ( ! in.is_notNull() ) {
		result = gua_err(thNEW(stdString,("part_at: needs a mesh or 2D region")));
		return;
	}
	double p[3];
	const char *why = 0;
	char buf[256];
	if ( ! srava_affine::point3(( na > 1 ) ? (*args)[1] : sPtr<pigData>(),
	                            "part_at", p, &why, buf, (int)sizeof buf) ) {
		result = gua_err(thNEW(stdString,(why)));
		return;
	}
	why = 0;
	geom = in->op_part_at(p, &why);
	if ( ! geom.is_notNull() ) {
		char b[352];
		::snprintf(b, sizeof b, "part_at: %s", why ? why : "could not extract it");
		result = gua_err(thNEW(stdString,(b)));
	}
}

/* エラー時は compute() が result にエラーを残すので result 優先。 */
sPtr<pigData>
guaPartAt_::get_result()
{
	return ( result != thNULL ) ? result : sPtr<pigData>::d_cast(geom);
}
