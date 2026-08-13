/*
 * cgaCentroid — centroid(mesh) の計算本体(ptsCalcBody 派生)= 重心を**配列で返す値返し op**。
 *   2D=面積重心 [x,y] / 3D=体積重心 [x,y,z]。多態 op_centroid が out[] と次元を返す。
 * 配列返し: result=pigDataArray(各要素 pigDataFloat)。cgatsAgent が serialize→ プランナが VALUE
 * パースで pigDataArray に復元 → 添字可(c[0] 等)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"cg/c++/cgMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/cgaCentroid_.h"

CLASS_TINYSTATE(cg/c++/cgaCentroid,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	cgaCentroid_(
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


cgaCentroid_::cgaCentroid_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
cgaCentroid_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<cgMesh> in = ( na > 0 ) ? sPtr<cgMesh>::d_cast((*args)[0]) : sPtr<cgMesh>();
	if ( ! in.is_notNull() ) {
		result = thNEW(pigDataError,(thNEW(stdString,("centroid: needs a mesh"))));
		return;
	}
	double c[3] = { 0, 0, 0 };
	int n = in->op_centroid(c);
	sPtr<pigDataArray> arr = thNEW(pigDataArray,());
	for ( int i = 0 ; i < n ; ++i )
		arr->push(thNEW(pigDataFloat,(c[i])));
	result = arr;
}
