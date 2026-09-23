/*
 * vdaDistanceAt — distance_at(m, [x,y,z]) — **点から境界までの最短距離** (#3514)。値返し・符号なし。
 *   ★ 既存の distance(a,b) は **2 つの立体**の間の距離。問うているものが違うので別名にした
 *     (docs の命名規約「約束が違えば元の名前 + 修飾」・位置で指す _at は face_at と同じ流儀)。
 *   ★ 閉形式で検定できる: 球 (半径 r) の中心から距離 d の点なら **|d - r|**。
 *   ★★ openvdb は **距離が場そのもの**なので、探索も三角形化もせずに値を読むだけで出る。
 *     ⚠⚠ ただし狭帯域の外は飽和しているので明示エラーにする。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"vd/c++/vdGrid.h"
#include	"common/affine.h"	/* 点 [x,y,z] の解釈 (拒否の文言もここ) */
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/vdaDistanceAt_.h"

CLASS_TINYSTATE(vd/c++/vdaDistanceAt,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	vdaDistanceAt_(
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
class vdGrid;
TS_END_INTERFACE

#endif


vdaDistanceAt_::vdaDistanceAt_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
vdaDistanceAt_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<vdGrid> in = ( na > 0 ) ? sPtr<vdGrid>::d_cast((*args)[0]) : sPtr<vdGrid>();
	if ( ! in.is_notNull() ) {
		result = vda_err(thNEW(stdString,("distance_at: needs a mesh")));
		return;
	}
	double p[3];
	const char *why = 0;
	char buf[256];
	if ( ! srava_affine::point3(( na > 1 ) ? (*args)[1] : sPtr<pigData>(),
	                            "distance_at", p, &why, buf, (int)sizeof buf) ) {
		result = vda_err(thNEW(stdString,(why)));
		return;
	}
	double d = 0.0;
	int rc = in->op_distance_at(p, &d);
	/* ⚠⚠ 狭帯域の外は background に飽和しているので、**黙って飽和値を返さない**。 */
	if ( rc < 0 ) {
		char m[256];
		::snprintf(m, sizeof m,
		    "distance_at: the point [%g,%g,%g] is outside the narrow band "
		    "(the level set only stores distances near the surface; "
		    "re-voxelize with a wider band or a larger dx)", p[0], p[1], p[2]);
		result = vda_err(thNEW(stdString,(m)));
		return;
	}
	if ( rc != 1 ) {
		result = vda_err(thNEW(stdString,("distance_at: could not measure the distance (empty value?)")));
		return;
	}
	result = thNEW(pigDataFloat,(d));
}
