/*
 * cgaDistance — distance(a, b) の計算本体(ptsCalcBody 派生)= 2 メッシュ間の最近接距離を返す**値返し op**。
 *   3D-3D 専用(AABB・両方向頂点-面の近似)。2D/混在はエラー。√を含むので double。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"cg/c++/cgMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/cgaDistance_.h"

CLASS_TINYSTATE(cg/c++/cgaDistance,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	cgaDistance_(
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


cgaDistance_::cgaDistance_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
cgaDistance_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<cgMesh3D> a = ( na > 0 ) ? sPtr<cgMesh3D>::d_cast((*args)[0]) : sPtr<cgMesh3D>();
	sPtr<cgMesh3D> b = ( na > 1 ) ? sPtr<cgMesh3D>::d_cast((*args)[1]) : sPtr<cgMesh3D>();
	if ( ! a.is_notNull() || ! b.is_notNull() ) {
		result = thNEW(pigDataError,(thNEW(stdString,("distance: needs two 3D meshes"))));
		return;
	}
	double pa[3], pb[3];
	double d = a->op_proximity(*b.__get(), false, pa, pb);
	result = thNEW(pigDataFloat,(d));
}
