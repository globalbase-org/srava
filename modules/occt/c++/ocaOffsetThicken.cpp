/*
 * ocaOffsetThicken — offset_thicken(2D, d) の計算本体 (#3547 ②・2026-09-18)。
 *
 * ★★ **面に厚み d を付けて立体にする** (2D → 3D)。平面でも曲面でも定義できるので、
 *   曲面上の面 (円柱の側面など) を太らせたいときはこちら。
 *
 * ⚠⚠ @offset@ とは **別の操作**なので名前を分けてある (ひさ裁定 2026-09-18):
 *     offset(2D,d)          面の *中で* 輪郭を動かす      → 2D のまま (cg / mf と同じ約束)
 *     offset_thicken(2D,d)  面の法線側に厚みを付ける      → 3D ・ **片側だけ**
 *   3D の @offset(solid,d)@ は「全方向に ⊕ 球(d)」なので、こちらを @offset@ と呼ぶと
 *   *同じ名前で約束が変わる* (#3510 の論点①)。命名規約は「元の op 名 + _修飾」(#3519)。
 *
 * ★ 検算できる: 平面なら **体積 = 面積 x |d|** ちょうど。円筒側面 (r,h) を外へ d なら
 *   π((r+d)² − r²)h。⇒ 実装した当日に回帰が書ける (test/srava_occt_face.sh ⑭)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"oc/c++/ocShape.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ocaOffsetThicken_.h"


CLASS_TINYSTATE(oc/c++/ocaOffsetThicken,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ocaOffsetThicken_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

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


ocaOffsetThicken_::ocaOffsetThicken_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}

void
ocaOffsetThicken_::compute()
{
	ocShape::ensure_init();   /* ★ OCCT の診断出力を stdout から外す (ocShape.h 参照) */
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<ocFace2D> in = ( na > 0 ) ? sPtr<ocFace2D>::d_cast((*args)[0]) : sPtr<ocFace2D>();
	if ( ! in.is_notNull() ) {
		/* ⚠ 3D を渡された場合に「2D が要る」とだけ言うと、利用者は offset を知らないままになる。 */
		result = oca_err(thNEW(stdString,(
		    "offset_thicken: needs a 2D region (oc-face3d / oc-cross2d); "
		    "for a 3D shape use offset(shape, d)")));
		return;
	}
	double d = ( na > 1 ) ? (*args)[1]->get_flt() : 0.0;
	/* ★ 理由の受け皿は **この compute のローカル** (モジュール大域の static を置かない・
	 *   in-proc では複数 op が同居しうるため。ひさ指示 2026-08-26)。 */
	char why[512];
	why[0] = '\0';
	out = in->op_thicken(d, why, (int)sizeof why);
	if ( ! out.is_notNull() ) {
		const char *msg = ( why[0] != '\0' ) ? why : "offset_thicken: failed";
		result = oca_err(thNEW(stdString,(msg)));
	}
}

sPtr<pigData>
ocaOffsetThicken_::get_result()
{
	return ( result != thNULL ) ? result : out;
}
