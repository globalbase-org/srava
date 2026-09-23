/*
 * ocaBbox — bbox(m) — 軸平行 AABB を [min 隅, max 隅] で返す。
 *
 * ★ 値を返すだけの op (→value)。B-rep のまま測るので、解析曲面 (球・円柱・トーラス) では
 *   mesh 系の内接多面体と **構造的に違う値**が出る (体積と同じ事情)。検証は閉形式で行う。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"oc/c++/ocShape.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ocaBbox_.h"

CLASS_TINYSTATE(oc/c++/ocaBbox,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ocaBbox_(
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


ocaBbox_::ocaBbox_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
ocaBbox_::compute()
{
	ocShape::ensure_init();
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<ocShape> in = ( na > 0 ) ? sPtr<ocShape>::d_cast((*args)[0]) : sPtr<ocShape>();
	/* ★ #3518 の 6: **2D も受ける** (face / face_at で取り出した面がどこに在るかを訊く)。 */
	sPtr<ocFace2D> f2 = ( ! in.is_notNull() && na > 0 ) ? sPtr<ocFace2D>::d_cast((*args)[0])
	                                                    : sPtr<ocFace2D>();
	if ( ! in.is_notNull() && ! f2.is_notNull() ) {
		result = oca_err(thNEW(stdString,("bbox: needs an OCCT shape")));
		return;
	}
	double mn[3], mx[3];
	if ( in.is_notNull() ) in->op_bbox(mn, mx); else f2->op_bbox(mn, mx);
	/* ★★ #3544: **答えの形が型で決まる** (docs/srava_language_reference.md#two-2d-types)。
	 *   oc-cross2d = z=0 の簡易表現 ⇒ **局所の 2 成分** / oc-face3d = 一般表現 ⇒ world の 3 成分。
	 *   ⚠ cg / mf は #3533 からこの規約で、occt だけ 3 成分固定だった (2D が 1 型だったため)。
	 *   ★ occt の名乗りは **幾何から導く** (ocShape.h の type_name)。⇒ ここも同じ述語を見る。
	 *     値に訊いた答えと名乗りが 2 度と離れないよう、間にビットを挟まない。
	 *   ⚠ 面内回転 (@rotate(r,"z",90)@) は sig のスタンプでは face3d だが幾何は z=0 なので
	 *     **2 成分**になる。承知の上の 1 ケース (ocShape.h / test/srava_face3d.sh に注記)。 */
	const int nc = ( f2.is_notNull() && f2->on_z0_plane() ) ? 2 : 3;
	sPtr<pigDataArray> lo = thNEW(pigDataArray,());
	sPtr<pigDataArray> hi = thNEW(pigDataArray,());
	for ( int i = 0 ; i < nc ; ++i ) {
		lo->push(thNEW(pigDataFloat,(mn[i])));
		hi->push(thNEW(pigDataFloat,(mx[i])));
	}
	sPtr<pigDataArray> arr = thNEW(pigDataArray,());   /* [min 隅, max 隅] */
	arr->push(lo);
	arr->push(hi);
	result = arr;
}
