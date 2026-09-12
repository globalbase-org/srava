/*
 * nfaTube — tube(path[, segs]) — 折れ線まわりの掃引管 (nef 版・#3474)。
 * ★ 掃引そのものは **common/tube.h** が持つ (rotation-minimizing frame・断面リング・
 *   平キャップ)。CGAL にも Manifold にも依存しないので、どのカーネルからも同じ物が出る。
 * ★ このカーネルは **3D 型しか持たない**ので 2D パス ([x,y]) は明示エラーにする
 *   (黙って z=0 の平たい管にすると、利用者の意図と違う値を返してしまう)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"nf/c++/nfMesh.h"
#include	"nf/c++/nfTriSink.h"
#include	"ts2/c++/stdString.h"
#include	"common/tube.h"
#include	<vector>
#include	"_ts2/c++/nfaTube_.h"

CLASS_TINYSTATE(nf/c++/nfaTube,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	nfaTube_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<nfMesh>	mesh;
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


nfaTube_::nfaTube_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
nfaTube_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<pigDataArray> path = ( na > 0 ) ? (*args)[0]->obt_array()
	                                     : sPtr<pigDataArray>();
	int segs = ( na > 1 ) ? (int)(*args)[1]->get_int() : 32;   /* 円の辺数。既定 32 */
	if ( segs < 3 ) segs = 3;
	if ( segs > 4096 ) segs = 4096;
	int nraw = path.is_notNull() ? path->length() : 0;
	if ( nraw < 2 ) {
		result = nfa_err(thNEW(stdString,("tube: needs >= 2 path vertices ([[[x,y,z],r],...])")));
		return;
	}

	/* ---- パス読み取り: 各要素 [位置, r]。要素は obt_array() で取る
	 *   (配列は要素を eager 解決しないので、素の d_cast だと in-proc で null になる)。 ---- */
	std::vector<srava_geo::TubeV3> Praw((size_t)nraw);
	std::vector<double>            Rraw((size_t)nraw);
	for ( int i = 0 ; i < nraw ; ++i ) {
		sPtr<pigDataArray> pr = path->get_ix(thNEW(pigDataInteger,((INTEGER64)i)))->obt_array();
		if ( ! pr.is_notNull() || pr->length() < 2 ) {
			result = nfa_err(thNEW(stdString,("tube: each vertex must be [pos, r] (pos=[x,y,z])")));
			return;
		}
		sPtr<pigDataArray> pos = pr->get_ix(thNEW(pigDataInteger,((INTEGER64)0)))->obt_array();
		int pl = pos.is_notNull() ? pos->length() : 0;
		/* ★ この幾何カーネルは 2D 型を持たないので、2D パス ([x,y]) は受けられない。
		 *   黙って 3D として扱うと z=0 の平たい管になり、利用者の意図と違う値を返す。 */
		if ( pl < 3 ) {
			result = nfa_err(thNEW(stdString,("tube: this kernel has no 2D type, so the path must be 3D ([[x,y,z],r]); use \"cgal\"::tube or \"manifold\"::tube for 2D ribbons")));
			return;
		}
		Praw[(size_t)i] = srava_geo::TubeV3(
		    pos->get_ix(thNEW(pigDataInteger,((INTEGER64)0)))->get_flt(),
		    pos->get_ix(thNEW(pigDataInteger,((INTEGER64)1)))->get_flt(),
		    pos->get_ix(thNEW(pigDataInteger,((INTEGER64)2)))->get_flt());
		Rraw[(size_t)i] = pr->get_ix(thNEW(pigDataInteger,((INTEGER64)1)))->get_flt();
		if ( Rraw[(size_t)i] < 0.0 ) {
			result = nfa_err(thNEW(stdString,("tube: radius must be >= 0")));
			return;
		}
	}

	/* ---- 連続重複頂点は **弾かずに間引く** (接線が定義できないため)。 ---- */
	std::vector<srava_geo::TubeV3> P;
	std::vector<double> R;
	srava_geo::tube_dedup(Praw, Rraw, P, R);
	if ( P.size() < 2 ) {
		result = nfa_err(thNEW(stdString,("tube: needs >= 2 distinct path vertices (all given vertices coincide)")));
		return;
	}

	nfTriSink sink;
	int st = srava_geo::make_tube_3d(P, R, segs, sink);
	if ( st == srava_geo::TUBE_ERR_ZERO_SEGMENT ) {
		result = nfa_err(thNEW(stdString,("tube: two consecutive zero-radius vertices (degenerate segment)")));
		return;
	}
	if ( st != srava_geo::TUBE_OK ) {
		result = nfa_err(thNEW(stdString,("tube: duplicate consecutive path vertices")));
		return;
	}
	mesh = sink.finish();
}

sPtr<pigData>
nfaTube_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
