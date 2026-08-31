/*
 * nfaNparts — nparts(mesh) — **塊 (part) の数** を返す (#3441 追補・ひさ提案 2026-08-18)。
 *   塊 = SNC の marked volume。普通の立体は 1・離れた 2 立体は 2・空洞つき立体は 1
 *   (空洞は塊ではない)・凸分解の結果は片の数。
 *   ★「mesh の配列」を返す仕組みが無いので、**数を返す op (これ) と n 番目を返す op (part)**
 *     の 2 本で塊を扱う。旧 convex_pieces はこれに置き換えた (分解を cache に残せる分こちらが得:
 *     @nparts(convex_decomposition(m))@ と書けば、分解結果を part でそのまま使い回せる)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"nf/c++/nfMesh.h"
#include	"ts2/c++/stdString.h"
#include	<CGAL/Polygon_mesh_processing/measure.h>
#include	"_ts2/c++/nfaNparts_.h"

CLASS_TINYSTATE(nf/c++/nfaNparts,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	nfaNparts_(
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


nfaNparts_::nfaNparts_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
nfaNparts_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<nfMesh> in = ( na > 0 ) ? sPtr<nfMesh>::d_cast((*args)[0]) : sPtr<nfMesh>();
	if ( ! in.is_notNull() ) {
		result = thNEW(pigDataError,(thNEW(stdString,("nparts: needs a Nef mesh"))));
		return;
	}
	result = thNEW(pigDataInteger,((INTEGER64)in->op_nparts()));
}
