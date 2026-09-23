/*
 * cgaTorus — torus(R, r, seg) — トーラス (原点中心・軸 +Z) の計算本体 (cgal 版・#3474)。
 * ★ common/solids.h の共通生成器なので、他のメッシュ系カーネルと
 *   **頂点・面の並びが一致する** (geodesic.h の sphere と同じ方針)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"cg/c++/cgMesh.h"
#include	"cg/c++/cgTriSink.h"
#include	"common/solids.h"
#include	"ts2/c++/stdString.h"
#include	"common/segs.h"   /* ★ #3530: segs / n の共通検査 */
#include	"_ts2/c++/cgaTorus_.h"

CLASS_TINYSTATE(cg/c++/cgaTorus,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	cgaTorus_(
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
class cgMesh3D;
TS_END_INTERFACE

#endif


cgaTorus_::cgaTorus_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
cgaTorus_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	double R   = ( na > 0 ) ? (*args)[0]->get_flt() : 1.0;
	double r   = ( na > 1 ) ? (*args)[1]->get_flt() : 0.25;
	int    seg_in = ( na > 2 ) ? (int)(*args)[2]->get_int() : 0;   /* 0 = 未指定 */
	int    seg = 0;
	/* ★★ #3530: segs の意味を全 op / 全カーネルで 1 本に揃えた (src/h/common/segs.h)。
	 *   0 or 省略 = 既定値 / 1,2 / 負 = 明示エラー / 3 以上 = その値。 */
	if ( srava_geo::check_segs(seg_in, 0, &seg) != srava_geo::SEGS_OK ) {
		result = cga_err(thNEW(stdString,(srava_geo::segs_error("torus").c_str()))); return;
	}
	if ( !(R > 0) ) { result = cga_err(thNEW(stdString,("torus: R (distance from the axis to the tube center) must be > 0"))); return; }
	if ( !(r > 0) ) { result = cga_err(thNEW(stdString,("torus: r (tube radius) must be > 0"))); return; }
	if ( !(r < R) ) { result = cga_err(thNEW(stdString,("torus: r must be < R (self-intersecting otherwise)"))); return; }

	mesh = thNEW(cgMesh3D,());
	cgTriSink sink;
	srava_geo::make_torus(R, r, seg, sink);
	sink.flush_to(mesh);   /* ★ #3545: CGAL へ積むのは幾何 lib 側 */
}

/* この演算の結果。エラー時は compute() が result にエラー値を残して本体未設定で return するので
 * result 優先。保存 (Writer 起動) は agent が出力 pigDataCache の set_body 経由で行う。 */
sPtr<pigData>
cgaTorus_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
