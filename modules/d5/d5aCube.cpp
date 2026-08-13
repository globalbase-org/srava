/*
 * d5aCube — d5_cube(s) の計算本体 (rev4 Phase D-3・mfaBox のミラー)。原点隅の s×s×s 立方体を
 * d5Mesh に保持。get_result() で agent へ返す (保存は set_body 経由)。leaf producer (mesh 出力)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"d5/c++/d5Mesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/d5aCube_.h"

CLASS_TINYSTATE(d5/c++/d5aCube,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	d5aCube_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<d5Mesh>	mesh;
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
class d5Mesh;
TS_END_INTERFACE

#endif


d5aCube_::d5aCube_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
d5aCube_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	double s = ( na > 0 ) ? (*args)[0]->get_flt() : 1.0;
	mesh = d5Mesh::cube(s);
}

sPtr<pigData>
d5aCube_::get_result()
{
	return ( result != thNULL ) ? result : sPtr<pigData>::d_cast(mesh);
}
