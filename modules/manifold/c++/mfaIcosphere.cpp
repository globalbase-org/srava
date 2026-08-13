/*
 * mfaIcosphere — icosphere(細分回数指定)生成の計算本体(mf 版・cgaIcosphere のミラー)。
 * args=[radius, subdiv](INLINE)。subdiv=細分回数(0=正二十面体 20 面・4 倍刻み)。
 * 種=正二十面体を n=2^subdiv 分割して球面投影。
 * ★cgaIcosphere と同一アルゴリズム(src/h/common/geodesic.h)で頂点・面が一致。
 *   円周分割数で指定したいときは sphere(r, seg)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"mf/c++/mfMesh.h"
#include	"mf/c++/ptsmfWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	"common/geodesic.h"   /* subdiv_to_n / SEED_ICOSAHEDRON */
#include	"_ts2/c++/mfaIcosphere_.h"

CLASS_TINYSTATE(mf/c++/mfaIcosphere,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	mfaIcosphere_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<mfMesh>	mesh;
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
class mfMesh;
TS_END_INTERFACE

#endif


mfaIcosphere_::mfaIcosphere_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


void
mfaIcosphere_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	double r      = ( na > 0 ) ? (*args)[0]->get_flt() : 1.0;
	int    subdiv = ( na > 1 ) ? (int)(*args)[1]->get_int() : 0;   /* 細分回数。既定 0(二十面体 20 面) */
	int    n      = srava_geo::subdiv_to_n(subdiv);
	mesh = mfMesh::geodesic(srava_geo::SEED_ICOSAHEDRON, n, r);
}

sPtr<pigData>
mfaIcosphere_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
