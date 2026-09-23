/*
 * nfaNverts — nverts(mesh) — 境界表現に落としたときの頂点数 / 面数 (#3443)。
 *   ★ Nef は面の集まりでなく空間分割なので、数える前に境界へ落とす (volume と同じ経路)。
 *   非有界は境界を持たないので明示エラー。
 */
#include	<cstdio>
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"nf/c++/nfMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/nfaNverts_.h"

CLASS_TINYSTATE(nf/c++/nfaNverts,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	nfaNverts_(
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
class nfNefMesh;
TS_END_INTERFACE

#endif


nfaNverts_::nfaNverts_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
nfaNverts_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<nfNefMesh> in = ( na > 0 ) ? sPtr<nfNefMesh>::d_cast((*args)[0]) : sPtr<nfNefMesh>();
	if ( ! in.is_notNull() ) {
		result = nfa_err(thNEW(stdString,("nverts: needs a Nef mesh")));
		return;
	}
	/* ★ #3545: Mesh をここで持たない — 持つとこの TU が CGAL を引き込む。 */
	int nv = 0, nf = 0;
	if ( ! in->boundary_counts(&nv, &nf) ) {
		char b[256];
		::snprintf(b, sizeof b, "nverts: this Nef has no boundary mesh (%s)", nf_why(in));
		result = nfa_err(thNEW(stdString,(b)));
		return;
	}
	result = thNEW(pigDataInteger,((INTEGER64)nv));
}
