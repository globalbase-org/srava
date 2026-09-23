/*
 * guaPart — part(v, i) — **i 番目の塊** を取り出す op (#3527 段 4)。
 *
 * ★★★ 段 4 の本丸は **入れ子**。塊は「符号つき体積 (面積) が正の殻 (リング)」と
 *   **その直接の空洞 (穴)** の両方を境界に持つので、取り出すには
 *   「どの空洞がどの塊のものか」を解かねばならない。数えるだけなら符号で足りる
 *   (そこが #3514 で nparts だけ入って part が見送られた分かれ目)。
 *   ⇒ 実体は meshprops.h の nesting() (3D・巻き数) と ringprops.h の nesting() (2D・点の内外)。
 *
 * ★★ shell との違いは **空洞を断らないか / 抱えるか** の 1 点:
 *     part  … 値を *分割する* 片。**Σ |volume(part)| == |volume(m)|** (符号なし)
 *     shell … 面の連結成分。   **Σ volume(shell) == volume(m)**   (符号つき)
 *   この 2 式が別であることが part と shell が別物である理由そのもの。
 *
 * ⚠ 3D で殻 1 枚だけ返すと *中身の詰まった立体* = **別のもの**になり体積が黙って増える
 *   (同心の中空球で 28.64 → 32.73 の実測がある)。⇒ 必ず塊単位で取る。
 *
 * ⚠ 索引は **実装依存** (走査順)。版を跨いで同じ塊を指す保証は無い ⇒ 位置で指すなら part_at。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"gu/c++/guGeom.h"
#include	"common/affine.h"    /* point3 — 点 [x,y,z] の読み方と拒否の文言を一本化 */
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/guaPart_.h"
#include	<stdio.h>

CLASS_TINYSTATE(gu/c++/guaPart,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	guaPart_(
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

guaPart_::guaPart_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
guaPart_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	int idx = ( na > 1 ) ? (int)(*args)[1]->get_int() : 0;
	sPtr<guGeom> in = ( na > 0 ) ? sPtr<guGeom>::d_cast((*args)[0]) : sPtr<guGeom>();
	if ( ! in.is_notNull() ) {
		result = gua_err(thNEW(stdString,("part: needs a mesh or 2D region")));
		return;
	}
	const char *why = 0;
	geom = in->op_part(idx, &why);
	if ( ! geom.is_notNull() ) {
		char b[352];
		::snprintf(b, sizeof b, "part: %s", why ? why : "could not extract it");
		result = gua_err(thNEW(stdString,(b)));
	}
}

/* エラー時は compute() が result にエラーを残すので result 優先。 */
sPtr<pigData>
guaPart_::get_result()
{
	return ( result != thNULL ) ? result : sPtr<pigData>::d_cast(geom);
}
