/*
 * cgaIcosphere — icosphere(細分回数指定)生成の計算本体(ptsCalcBody 派生)。cgaSphere のミラー。
 * args=[radius, subdiv](INLINE)。subdiv=細分回数(0=正二十面体 20 面・1=80・2=320…4 倍刻み)。
 * 種=正二十面体を n=2^subdiv 分割して球面投影。旧 sphere(r, subdiv) の意味論はこの op が継ぐ
 *   (旧 sphere(1,2)=162v/320f == icosphere(1,2))。
 * ★manifold の icosphere と同一アルゴリズム(src/h/common/geodesic.h)で頂点・面が一致。
 *   円周分割数で指定したいときは sphere(r, seg)。実体は cgMesh3D.cpp の cga_make_geodesic。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"cg/c++/cgMesh.h"
#include	"cg/c++/ptscgWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	"common/geodesic.h"   /* subdiv_to_n / SEED_ICOSAHEDRON */
#include	"_ts2/c++/cgaIcosphere_.h"
void cga_make_geodesic(cgMesh::Mesh& ball, int seed, int n, double r);
CLASS_TINYSTATE(cg/c++/cgaIcosphere,pig/c++/ptsCalcBody)
#if 0
TS_BEGIN_IMPLEMENT
class TS_THISCLASS : public TS_BASECLASS {
public:
	cgaIcosphere_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);
	sRptr<ptsObject,tinyState>		parent;
	virtual sPtr<pigData>	get_result();
protected:
	virtual void	compute();
	sPtr<cgMesh3D>	mesh;
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
cgaIcosphere_::cgaIcosphere_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}
sPtr<pigData>
cgaIcosphere_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
void
cgaIcosphere_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	double r      = ( na > 0 ) ? (*args)[0]->get_flt() : 1.0;
	int    subdiv = ( na > 1 ) ? (int)(*args)[1]->get_int() : 0;   /* 細分回数。既定 0(二十面体 20 面) */
	int    n      = srava_geo::subdiv_to_n(subdiv);

	mesh = thNEW(cgMesh3D,());
	cga_make_geodesic(mesh->mesh(), srava_geo::SEED_ICOSAHEDRON, n, r);
}
