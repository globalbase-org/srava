/*
 * vdaTorus — torus(R, r, seg) — トーラス (原点中心・軸 +Z) の計算本体 (openvdb 版・#3474)。
 * ★ 最終引数は **dx (ボクセルサイズ)**。ボクセル表現に分割数は意味を持たない
 *   (openvdb の box / sphere と同じ規約)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"vd/c++/vdGrid.h"
#include	"vd/c++/vdTriSink.h"
#include	"vd/c++/vdArena.h"   /* #3441: op あたりの TBB 予算 */
#include	"common/solids.h"
#include	"ts2/c++/stdString.h"
#include	"common/segs.h"   /* ★ #3530: segs / n の共通検査 */
#include	<string>
#include	"_ts2/c++/vdaTorus_.h"

CLASS_TINYSTATE(vd/c++/vdaTorus,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	vdaTorus_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<vdGrid>	out;
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
class vdGrid;
TS_END_INTERFACE

#endif


vdaTorus_::vdaTorus_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
vdaTorus_::compute()
{
	std::string vdwhy;
	/* ★ #3474 続き: 例外境界。openvdb が投げると受け手が無く、ワーカースレッド
	 *   由来なら agent ごと死ぬ (vdArena.h の vd_arena_guard 参照)。 */
	if ( ! vd_arena_guard("torus", [&]{

	vdGrid::ensure_init();
	int na = ( args != 0 ) ? args->length() : 0;
	double R   = ( na > 0 ) ? (*args)[0]->get_flt() : 1.0;
	double r   = ( na > 1 ) ? (*args)[1]->get_flt() : 0.25;
	int    seg_in = ( na > 2 ) ? (int)(*args)[2]->get_int() : 0;   /* 0 = 未指定 */
	int    seg = 0;
	/* ★★ #3530: segs の意味を全 op / 全カーネルで 1 本に揃えた (src/h/common/segs.h)。
	 *   0 or 省略 = 既定値 / 1,2 / 負 = 明示エラー / 3 以上 = その値。 */
	if ( srava_geo::check_segs(seg_in, 0, &seg) != srava_geo::SEGS_OK ) {
		result = vda_err(thNEW(stdString,(srava_geo::segs_error("torus").c_str()))); return;
	}
	double dx = ( na > 3 ) ? (*args)[3]->get_flt() : 0.0;
	if ( !(R > 0) ) { result = vda_err(thNEW(stdString,("torus: R (distance from the axis to the tube center) must be > 0"))); return; }
	if ( !(r > 0) ) { result = vda_err(thNEW(stdString,("torus: r (tube radius) must be > 0"))); return; }
	if ( !(r < R) ) { result = vda_err(thNEW(stdString,("torus: r must be < R (self-intersecting otherwise)"))); return; }
	if ( !(dx > 0) ) { result = vda_err(thNEW(stdString,("torus: last argument is dx (voxel size in world units) and must be > 0"))); return; }

	vdTriSink sink;
	srava_geo::make_torus(R, r, seg, sink);
	out = sink.finish(dx);
	if ( ! out.is_notNull() )
		result = vda_err(thNEW(stdString,("torus: openvdb meshToLevelSet failed")));
	}, vdwhy) )
		result = vda_err(thNEW(stdString,(vdwhy.c_str())));
}

/* この演算の結果。エラー時は compute() が result にエラー値を残して本体未設定で return するので
 * result 優先。保存 (Writer 起動) は agent が出力 pigDataCache の set_body 経由で行う。 */
sPtr<pigData>
vdaTorus_::get_result()
{
	return ( result != thNULL ) ? result : out;
}
