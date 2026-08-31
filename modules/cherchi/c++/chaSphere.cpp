/*
 * chaSphere — sphere(r,seg) の計算本体 (cherchi 版)。共通生成器なので他カーネルと頂点・面が一致する。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"ch/c++/chMesh.h"
#include	"ts2/c++/stdString.h"
#include	"common/geodesic.h"   /* cgal/manifold/nef と共通の生成器 = 頂点が bit 一致する */
#include	"_ts2/c++/chaSphere_.h"

CLASS_TINYSTATE(ch/c++/chaSphere,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	chaSphere_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<chMesh>	mesh;
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
class chMesh;
TS_END_INTERFACE

#endif


chaSphere_::chaSphere_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
chaSphere_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	double r   = ( na > 0 ) ? (*args)[0]->get_flt() : 1.0;
	int    seg = ( na > 1 ) ? (int)(*args)[1]->get_int() : 0;   /* 円周分割数。0=既定 */
	int    n   = srava_geo::seg_to_n(seg);

	mesh = thNEW(chMesh,());
	struct GeoSink {
		sPtr<chMesh> m;
		int  add_vertex(double x, double y, double z) { return m->add_vertex(x, y, z); }
		void add_triangle(int a, int b, int c)        { m->add_triangle(a, b, c); }
	} sink;
	sink.m = mesh;
	srava_geo::make_geodesic(srava_geo::SEED_OCTAHEDRON, n, r, sink);
}

sPtr<pigData>
chaSphere_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
