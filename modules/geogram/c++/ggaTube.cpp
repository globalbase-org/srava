/*
 * ggaTube — tube_ruled(path[, segs]) — 折れ線まわりの掃引管 (geogram 版・#3474)。
 * ★ 掃引そのものは **common/tube.h** が持つ (rotation-minimizing frame・断面リング・
 *   平キャップ)。CGAL にも Manifold にも依存しないので、どのカーネルからも同じ物が出る。
 * ★ このカーネルは **3D 型しか持たない**ので 2D パス ([x,y]) は明示エラーにする
 *   (黙って z=0 の平たい管にすると、利用者の意図と違う値を返してしまう)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"gg/c++/ggMesh.h"
#include	"gg/c++/ggTriSink.h"
#include	"ts2/c++/stdString.h"
#include	"common/segs.h"   /* ★ #3530: segs / n の共通検査 */
#include	"common/tube.h"
#include	<vector>
#include	"_ts2/c++/ggaTube_.h"

CLASS_TINYSTATE(gg/c++/ggaTube,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ggaTube_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<ggMesh>	mesh;
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
class ggMesh;
TS_END_INTERFACE

#endif


ggaTube_::ggaTube_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
ggaTube_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<pigDataArray> path = ( na > 0 ) ? (*args)[0]->obt_array()
	                                     : sPtr<pigDataArray>();
	int    segs_in = ( na > 1 ) ? (int)(*args)[1]->get_int() : 0;   /* 0 = 未指定 */
	int    segs = 0;
	/* ★★ #3530: segs の意味を全 op / 全カーネルで 1 本に揃えた (src/h/common/segs.h)。
	 *   0 or 省略 = 既定値 / 1,2 / 負 = 明示エラー / 3 以上 = その値。 */
	if ( srava_geo::check_segs(segs_in, 32, &segs) != srava_geo::SEGS_OK ) {
		result = gga_err(thNEW(stdString,(srava_geo::segs_error("tube_ruled").c_str()))); return;
	}
	int nraw = path.is_notNull() ? path->length() : 0;
	if ( nraw < 2 ) {
		result = gga_err(thNEW(stdString,("tube_ruled: needs >= 2 path vertices ([[[x,y,z],r],...])")));
		return;
	}

	/* ---- パス読み取り: 各要素 [位置, r]。要素は obt_array() で取る
	 *   (配列は要素を eager 解決しないので、素の d_cast だと in-proc で null になる)。 ---- */
	std::vector<srava_geo::TubeV3> Praw((size_t)nraw);
	std::vector<double>            Rraw((size_t)nraw);
	for ( int i = 0 ; i < nraw ; ++i ) {
		sPtr<pigDataArray> pr = path->get_ix(thNEW(pigDataInteger,((INTEGER64)i)))->obt_array();
		if ( ! pr.is_notNull() || pr->length() < 2 ) {
			result = gga_err(thNEW(stdString,("tube_ruled: each vertex must be [pos, r] (pos=[x,y,z])")));
			return;
		}
		sPtr<pigDataArray> pos = pr->get_ix(thNEW(pigDataInteger,((INTEGER64)0)))->obt_array();
		int pl = pos.is_notNull() ? pos->length() : 0;
		/* ★ この幾何カーネルは 2D 型を持たないので、2D パス ([x,y]) は受けられない。
		 *   黙って 3D として扱うと z=0 の平たい管になり、利用者の意図と違う値を返す。 */
		if ( pl < 3 ) {
			result = gga_err(thNEW(stdString,("tube_ruled: this kernel has no 2D type, so the path must be 3D ([[x,y,z],r]); use \"cgal\"::tube or \"manifold\"::tube for 2D ribbons")));
			return;
		}
		Praw[(size_t)i] = srava_geo::TubeV3(
		    pos->get_ix(thNEW(pigDataInteger,((INTEGER64)0)))->get_flt(),
		    pos->get_ix(thNEW(pigDataInteger,((INTEGER64)1)))->get_flt(),
		    pos->get_ix(thNEW(pigDataInteger,((INTEGER64)2)))->get_flt());
		Rraw[(size_t)i] = pr->get_ix(thNEW(pigDataInteger,((INTEGER64)1)))->get_flt();
		if ( Rraw[(size_t)i] < 0.0 ) {
			result = gga_err(thNEW(stdString,("tube_ruled: radius must be >= 0")));
			return;
		}
	}

	/* ---- 連続重複頂点は **弾かずに間引く** (接線が定義できないため)。 ---- */
	std::vector<srava_geo::TubeV3> P;
	std::vector<double> R;
	srava_geo::tube_dedup(Praw, Rraw, P, R);
	if ( P.size() < 2 ) {
		result = gga_err(thNEW(stdString,("tube_ruled: needs >= 2 distinct path vertices (all given vertices coincide)")));
		return;
	}

	ggTriSink sink;
	int st = srava_geo::make_tube_3d(P, R, segs, sink);
	if ( st == srava_geo::TUBE_ERR_ZERO_SEGMENT ) {
		result = gga_err(thNEW(stdString,("tube_ruled: two consecutive zero-radius vertices (degenerate segment)")));
		return;
	}
	if ( st != srava_geo::TUBE_OK ) {
		result = gga_err(thNEW(stdString,("tube_ruled: duplicate consecutive path vertices")));
		return;
	}
	mesh = sink.finish();
}

sPtr<pigData>
ggaTube_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
