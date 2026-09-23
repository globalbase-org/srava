/*
 * nfaSphere — sphere(r,seg) の計算本体 (nef 版)。cgaSphere/mfaSphere と同一アルゴリズム (common/geodesic.h) なので頂点・面が一致する。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"nf/c++/nfMesh.h"
#include	"ts2/c++/stdString.h"
#include	"common/segs.h"   /* ★ #3530: segs / n の共通検査 */
#include	"common/geodesic.h"   /* seg_to_n / SEED_OCTAHEDRON (cgal/manifold と共通) */
#include	<vector>
#include	"_ts2/c++/nfaSphere_.h"

CLASS_TINYSTATE(nf/c++/nfaSphere,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	nfaSphere_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<nfNefMesh>	mesh;
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


nfaSphere_::nfaSphere_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
nfaSphere_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	double r   = ( na > 0 ) ? (*args)[0]->get_flt() : 1.0;
	int    seg_in = ( na > 1 ) ? (int)(*args)[1]->get_int() : 0;   /* 0 = 未指定 */
	int    seg = 0;
	/* ★★ #3530: segs の意味を全 op / 全カーネルで 1 本に揃えた (src/h/common/segs.h)。
	 *   0 or 省略 = 既定値 / 1,2 / 負 = 明示エラー / 3 以上 = その値。 */
	if ( srava_geo::check_segs(seg_in, 0, &seg) != srava_geo::SEGS_OK ) {
		result = nfa_err(thNEW(stdString,(srava_geo::segs_error("sphere").c_str()))); return;
	}
	int    n   = srava_geo::seg_to_n(seg);
	/* ★ #3516: 退化・負の半径を弾く (icosphere / cylinder / cone などは元から持っていた検査を
	 *   sphere にも揃えた)。⚠ 弾かないと半径 0 が「体積 0 の球」として黙って下流へ流れる。 */
	if ( !(r > 0) ) {
		result = nfa_err(thNEW(stdString,("sphere: radius must be > 0")));
		return;
	}

	/* ★ #3545: 測地球の受け皿 (Surface_mesh) は **幾何 lib 側**に置いた。
	 *   ここで持つと、この TU が CGAL を引き込んで可変大域のコピーができる。 */
	mesh = thNEW(NF_MESH,());
	mesh->build_geodesic((int)srava_geo::SEED_OCTAHEDRON, n, r);
}

/* この演算の結果。エラー時は compute() が result にエラー値を残して mesh 未設定で return するので
 * result 優先。保存 (Writer 起動) は agent が出力 pigDataCache の set_body 経由で行う。 */
sPtr<pigData>
nfaSphere_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
