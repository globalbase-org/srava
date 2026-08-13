/*
 * cgaClosest — closest(a, b) の計算本体(ptsCalcBody 派生)= 最近接の [距離, [pa], [pb]] を返す**配列返し op**。
 *   pa=a 上の点 / pb=b 上の点。3D-3D 専用(AABB・両方向頂点-面の近似)。2D/混在はエラー。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"cg/c++/cgMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/cgaClosest_.h"

CLASS_TINYSTATE(cg/c++/cgaClosest,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	cgaClosest_(
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


cgaClosest_::cgaClosest_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

static sPtr<pigData> pt_array(const double p[3])
{
	sPtr<pigDataArray> a = thNEW(pigDataArray,());
	for ( int i = 0 ; i < 3 ; ++i ) a->push(thNEW(pigDataFloat,(p[i])));
	return a;
}

void
cgaClosest_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<cgMesh3D> a = ( na > 0 ) ? sPtr<cgMesh3D>::d_cast((*args)[0]) : sPtr<cgMesh3D>();
	sPtr<cgMesh3D> b = ( na > 1 ) ? sPtr<cgMesh3D>::d_cast((*args)[1]) : sPtr<cgMesh3D>();
	if ( ! a.is_notNull() || ! b.is_notNull() ) {
		result = thNEW(pigDataError,(thNEW(stdString,("closest: needs two 3D meshes"))));
		return;
	}
	double pa[3], pb[3];
	double d = a->op_proximity(*b.__get(), false, pa, pb);
	sPtr<pigDataArray> arr = thNEW(pigDataArray,());
	arr->push(thNEW(pigDataFloat,(d)));
	arr->push(pt_array(pa));
	arr->push(pt_array(pb));
	result = arr;
}
