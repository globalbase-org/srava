/*
 * cgaBbox — bbox(mesh) の計算本体(ptsCalcBody 派生)= 軸平行バウンディングボックスを
 *   **入れ子配列で返す値返し op**。2D=[[minx,miny],[maxx,maxy]] / 3D=[[minx,miny,minz],[maxx,maxy,maxz]]。
 *   多態 op_bbox が mn[]/mx[] と次元を返す。
 * 配列返し: result=pigDataArray(min 配列, max 配列)。cgatsAgent が serialize→ プランナが VALUE
 * パースで入れ子 pigDataArray に復元 → 添字可(b[0]=min隅, b[1][2]=maxz 等。closest と同じ仕掛け)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"cg/c++/cgMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/cgaBbox_.h"

CLASS_TINYSTATE(cg/c++/cgaBbox,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	cgaBbox_(
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


cgaBbox_::cgaBbox_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
cgaBbox_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<cgMesh> in = ( na > 0 ) ? sPtr<cgMesh>::d_cast((*args)[0]) : sPtr<cgMesh>();
	if ( ! in.is_notNull() ) {
		result = thNEW(pigDataError,(thNEW(stdString,("bbox: needs a mesh"))));
		return;
	}
	double mn[3] = { 0, 0, 0 }, mx[3] = { 0, 0, 0 };
	int n = in->op_bbox(mn, mx);
	sPtr<pigDataArray> lo = thNEW(pigDataArray,());
	sPtr<pigDataArray> hi = thNEW(pigDataArray,());
	for ( int i = 0 ; i < n ; ++i ) {
		lo->push(thNEW(pigDataFloat,(mn[i])));
		hi->push(thNEW(pigDataFloat,(mx[i])));
	}
	sPtr<pigDataArray> arr = thNEW(pigDataArray,());   /* [min 隅, max 隅] */
	arr->push(lo);
	arr->push(hi);
	result = arr;
}
