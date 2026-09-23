/*
 * cgaDistanceAt — distance_at(m, [x,y,z]) — **点から境界までの最短距離** (#3514)。値返し・符号なし。
 *   ★ 既存の distance(a,b) は **2 つの立体**の間の距離。問うているものが違うので別名にした
 *     (docs の命名規約「約束が違えば元の名前 + 修飾」・位置で指す _at は face_at と同じ流儀)。
 *   ★ 閉形式で検定できる: 球 (半径 r) の中心から距離 d の点なら **|d - r|**。
 *   ★ cgal は AABB に三角形を載せて closest_point。**三角形の内部まで**含めて測る。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"cg/c++/cgMesh.h"
#include	"common/affine.h"	/* 点 [x,y,z] の解釈 (拒否の文言もここ) */
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/cgaDistanceAt_.h"

CLASS_TINYSTATE(cg/c++/cgaDistanceAt,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	cgaDistanceAt_(
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
class cgMesh;
TS_END_INTERFACE

#endif


cgaDistanceAt_::cgaDistanceAt_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
cgaDistanceAt_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<cgMesh> in = ( na > 0 ) ? sPtr<cgMesh>::d_cast((*args)[0]) : sPtr<cgMesh>();
	if ( ! in.is_notNull() ) {
		result = cga_err(thNEW(stdString,("distance_at: needs a mesh")));
		return;
	}
	double p[3];
	const char *why = 0;
	char buf[256];
	if ( ! srava_affine::point3(( na > 1 ) ? (*args)[1] : sPtr<pigData>(),
	                            "distance_at", p, &why, buf, (int)sizeof buf) ) {
		result = cga_err(thNEW(stdString,(why)));
		return;
	}
	double d = 0.0;
	int rc = in->op_distance_at(p, &d);
	if ( rc != 1 ) {
		result = cga_err(thNEW(stdString,("distance_at: could not measure the distance (empty value?)")));
		return;
	}
	result = thNEW(pigDataFloat,(d));
}
