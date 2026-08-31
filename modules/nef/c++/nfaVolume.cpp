/*
 * nfaVolume — volume(mesh) の計算本体 (nef 版)。値返し。★非有界/非 2-多様体はエラー (黙って 0 を返さない)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"nf/c++/nfMesh.h"
#include	"ts2/c++/stdString.h"
#include	<CGAL/Polygon_mesh_processing/measure.h>
#include	"_ts2/c++/nfaVolume_.h"

CLASS_TINYSTATE(nf/c++/nfaVolume,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	nfaVolume_(
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
class nfMesh;
TS_END_INTERFACE

#endif


nfaVolume_::nfaVolume_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
nfaVolume_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<nfMesh> in = ( na > 0 ) ? sPtr<nfMesh>::d_cast((*args)[0]) : sPtr<nfMesh>();
	if ( ! in.is_notNull() ) {
		result = thNEW(pigDataError,(thNEW(stdString,("volume: needs a Nef mesh"))));
		return;
	}
	nfMesh::Mesh m;
	if ( ! in->to_mesh(m) ) {
		result = thNEW(pigDataError,(thNEW(stdString,
		    ("volume: an unbounded or non-2-manifold Nef has no volume (e.g. the result of complement)"))));
		return;
	}
	result = thNEW(pigDataFloat,(CGAL::to_double(CGAL::Polygon_mesh_processing::volume(m))));
}
