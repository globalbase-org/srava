/*
 * ocaDistanceAt — distance_at(m, [x,y,z]) — **点から境界までの最短距離** (#3514)。値返し・符号なし。
 *   ★ 既存の distance(a,b) は **2 つの立体**の間の距離。問うているものが違うので別名にした
 *     (docs の命名規約「約束が違えば元の名前 + 修飾」・位置で指す _at は face_at と同じ流儀)。
 *   ★ 閉形式で検定できる: 球 (半径 r) の中心から距離 d の点なら **|d - r|**。
 *   ★★ occt は **B-rep のまま** (BRepExtrema_DistShapeShape)。球なら |d - r| が閉形式のまま出る
 *     — メッシュ系は内接多面体との距離になるので、ここは構造的に違う値 (体積・面積と同じ関係)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"oc/c++/ocShape.h"
#include	"common/affine.h"	/* 点 [x,y,z] の解釈 (拒否の文言もここ) */
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ocaDistanceAt_.h"

CLASS_TINYSTATE(oc/c++/ocaDistanceAt,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ocaDistanceAt_(
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
class ocShape;
TS_END_INTERFACE

#endif


ocaDistanceAt_::ocaDistanceAt_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
ocaDistanceAt_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	/* ★★ #3553 (2026-09-18): **2D も受ける**。定義は 3D と同じ「p から面の集合までの最短距離」で、
	 *   @*-face3d@ / @*-cross2d@ はどちらも *3D に埋め込まれた 2 次元* なのでそのまま当てはまる。
	 *   ⇒ 新しい意味を作らない (「平面へ射影して 2D で測る」案は面外の高さを黙って捨てる)。 */
	sPtr<ocShape>  in = ( na > 0 ) ? sPtr<ocShape>::d_cast((*args)[0])  : sPtr<ocShape>();
	sPtr<ocFace2D> f2 = ( na > 0 ) ? sPtr<ocFace2D>::d_cast((*args)[0]) : sPtr<ocFace2D>();
	if ( ! in.is_notNull() && ! f2.is_notNull() ) {
		result = oca_err(thNEW(stdString,(
		    "distance_at: input must be a 3D shape (oc-brep3d) or a 2D region (oc-face3d)")));
		return;
	}
	double p[3];
	const char *why = 0;
	char buf[256];
	if ( ! srava_affine::point3(( na > 1 ) ? (*args)[1] : sPtr<pigData>(),
	                            "distance_at", p, &why, buf, (int)sizeof buf) ) {
		result = oca_err(thNEW(stdString,(why)));
		return;
	}
	double d = 0.0;
	int rc = in.is_notNull() ? in->op_distance_at(p, &d, &brk_)
	                        : f2->op_distance_at(p, &d, &brk_);
	if ( rc != 1 ) {
		result = oca_err(thNEW(stdString,("distance_at: could not measure the distance (empty value?)")));
		return;
	}
	result = thNEW(pigDataFloat,(d));
}
