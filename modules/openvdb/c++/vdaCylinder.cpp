/*
 * vdaCylinder — cylinder(r, h, seg) — 円柱 (原点中心・軸 +Z) の計算本体 (openvdb 版・#3474)。
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
#include	"_ts2/c++/vdaCylinder_.h"

CLASS_TINYSTATE(vd/c++/vdaCylinder,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	vdaCylinder_(
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


vdaCylinder_::vdaCylinder_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
vdaCylinder_::compute()
{
	std::string vdwhy;
	/* ★ #3474 続き: 例外境界。openvdb が投げると受け手が無く、ワーカースレッド
	 *   由来なら agent ごと死ぬ (vdArena.h の vd_arena_guard 参照)。 */
	if ( ! vd_arena_guard("cylinder", [&]{

	vdGrid::ensure_init();
	int na = ( args != 0 ) ? args->length() : 0;
	double r   = ( na > 0 ) ? (*args)[0]->get_flt() : 1.0;
	double h   = ( na > 1 ) ? (*args)[1]->get_flt() : 1.0;
	int    seg_in = ( na > 2 ) ? (int)(*args)[2]->get_int() : 0;   /* 0 = 未指定 */
	int    seg = 0;
	/* ★★ #3530: segs の意味を全 op / 全カーネルで 1 本に揃えた (src/h/common/segs.h)。
	 *   0 or 省略 = 既定値 / 1,2 / 負 = 明示エラー / 3 以上 = その値。 */
	if ( srava_geo::check_segs(seg_in, 0, &seg) != srava_geo::SEGS_OK ) {
		result = vda_err(thNEW(stdString,(srava_geo::segs_error("cylinder").c_str()))); return;
	}
	double dx = ( na > 3 ) ? (*args)[3]->get_flt() : 0.0;
	if ( !(r > 0) ) { result = vda_err(thNEW(stdString,("cylinder: radius must be > 0"))); return; }
	if ( !(h > 0) ) { result = vda_err(thNEW(stdString,("cylinder: height must be > 0"))); return; }
	if ( !(dx > 0) ) { result = vda_err(thNEW(stdString,("cylinder: last argument is dx (voxel size in world units) and must be > 0"))); return; }

	vdTriSink sink;
	srava_geo::make_cylinder(r, h, seg, sink);
	out = sink.finish(dx);
	if ( ! out.is_notNull() )
		result = vda_err(thNEW(stdString,("cylinder: openvdb meshToLevelSet failed")));
	}, vdwhy) )
		result = vda_err(thNEW(stdString,(vdwhy.c_str())));
}

/* この演算の結果。エラー時は compute() が result にエラー値を残して本体未設定で return するので
 * result 優先。保存 (Writer 起動) は agent が出力 pigDataCache の set_body 経由で行う。 */
sPtr<pigData>
vdaCylinder_::get_result()
{
	return ( result != thNULL ) ? result : out;
}
