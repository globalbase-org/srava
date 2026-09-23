/*
 * guaShell — shell(m, i) — **i 番目の殻** (面の連結成分) を取り出す op (#3527 段 4)。
 *
 * ★ part (塊) との違いは **空洞を断らないこと**。殻は面の連結成分そのものなので
 *   入れ子を知らなくても取り出せる。⇒ 中空の箱は 塊 1 個 ・ 殻 2 枚。
 *
 * ★★★ 向きは **そのまま返す** (案 A・ひさ裁定 2026-09-16)。空洞の殻は法線が内を向いて
 *   いるので @volume@ が **負**で返り、**符号がそのまま「外殻か空洞か」の判別子**になる。
 *   さらに **Σ 符号つき体積 == 全体の体積** が成り立つ (中空の箱 = 外殻 − 空洞)。
 *   ⚠ 向きを揃える案 (B) だと符号が消え、この 2 つとも失われる。
 *
 * ⚠ 2D に殻は無い ⇒ sig に 2D の行を置かない (ルータが先に弾く)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"gu/c++/guGeom.h"
#include	"common/affine.h"    /* point3 — 点 [x,y,z] の読み方と拒否の文言を一本化 */
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/guaShell_.h"
#include	<stdio.h>

CLASS_TINYSTATE(gu/c++/guaShell,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	guaShell_(
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

guaShell_::guaShell_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
guaShell_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	int idx = ( na > 1 ) ? (int)(*args)[1]->get_int() : 0;
	sPtr<guGeom> in = ( na > 0 ) ? sPtr<guGeom>::d_cast((*args)[0]) : sPtr<guGeom>();
	if ( ! in.is_notNull() ) {
		result = gua_err(thNEW(stdString,("shell: needs a mesh or 2D region")));
		return;
	}
	const char *why = 0;
	geom = in->op_shell(idx, &why);
	if ( ! geom.is_notNull() ) {
		char b[352];
		::snprintf(b, sizeof b, "shell: %s", why ? why : "could not extract it");
		result = gua_err(thNEW(stdString,(b)));
	}
}

/* エラー時は compute() が result にエラーを残すので result 優先。 */
sPtr<pigData>
guaShell_::get_result()
{
	return ( result != thNULL ) ? result : sPtr<pigData>::d_cast(geom);
}
