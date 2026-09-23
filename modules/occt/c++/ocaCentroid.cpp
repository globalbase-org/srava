/*
 * ocaCentroid — centroid(m) — 体積重心 [x,y,z] を返す。
 *
 * ★ 値を返すだけの op (→value)。B-rep のまま測るので、解析曲面 (球・円柱・トーラス) では
 *   mesh 系の内接多面体と **構造的に違う値**が出る (体積と同じ事情)。検証は閉形式で行う。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"oc/c++/ocShape.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ocaCentroid_.h"

CLASS_TINYSTATE(oc/c++/ocaCentroid,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ocaCentroid_(
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


ocaCentroid_::ocaCentroid_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
ocaCentroid_::compute()
{
	ocShape::ensure_init();
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<ocShape> in = ( na > 0 ) ? sPtr<ocShape>::d_cast((*args)[0]) : sPtr<ocShape>();
	/* ★ #3518 の 6: **2D も受ける** (face / face_at で取り出した面がどこに在るかを訊く)。 */
	sPtr<ocFace2D> f2 = ( ! in.is_notNull() && na > 0 ) ? sPtr<ocFace2D>::d_cast((*args)[0])
	                                                    : sPtr<ocFace2D>();
	if ( ! in.is_notNull() && ! f2.is_notNull() ) {
		result = oca_err(thNEW(stdString,("centroid: needs an OCCT shape")));
		return;
	}
	double c[3];
	if ( in.is_notNull() ) in->op_centroid(c); else f2->op_centroid(c);
	/* ★★ #3544: bbox と同じ — **答えの形が型で決まる** (#two-2d-types)。
	 *   oc-cross2d ⇒ 局所の 2 成分 / oc-face3d ⇒ world の 3 成分。
	 *   ★ 名乗りと同じ述語 (幾何) を見る ⇒ ocaBbox.cpp の注記。 */
	const int nc = ( f2.is_notNull() && f2->on_z0_plane() ) ? 2 : 3;
	sPtr<pigDataArray> arr = thNEW(pigDataArray,());
	for ( int i = 0 ; i < nc ; ++i )
		arr->push(thNEW(pigDataFloat,(c[i])));
	result = arr;
}
