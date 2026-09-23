/*
 * vdaPyramid — pyramid(n, h, r) — 正 n 角錐 (底面は z=0 の XY 平面・頂点は z=h) の計算本体 (openvdb 版・#3474)。
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
#include	"_ts2/c++/vdaPyramid_.h"

CLASS_TINYSTATE(vd/c++/vdaPyramid,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	vdaPyramid_(
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


vdaPyramid_::vdaPyramid_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
vdaPyramid_::compute()
{
	std::string vdwhy;
	/* ★ #3474 続き: 例外境界。openvdb が投げると受け手が無く、ワーカースレッド
	 *   由来なら agent ごと死ぬ (vdArena.h の vd_arena_guard 参照)。 */
	if ( ! vd_arena_guard("pyramid", [&]{

	vdGrid::ensure_init();
	int na = ( args != 0 ) ? args->length() : 0;
	int    n = ( na > 0 ) ? (int)(*args)[0]->get_int() : 3;
	double h = ( na > 1 ) ? (*args)[1]->get_flt() : 1.0;
	double r = ( na > 2 ) ? (*args)[2]->get_flt() : 1.0;
	double dx = ( na > 3 ) ? (*args)[3]->get_flt() : 0.0;
	if ( srava_geo::check_sides(n, &n) != srava_geo::SEGS_OK ) {   /* ★ #3530: n は形そのもの = 既定値なし */
		result = vda_err(thNEW(stdString,(srava_geo::sides_error("pyramid").c_str()))); return; }
	if ( !(h > 0) ) { result = vda_err(thNEW(stdString,("pyramid: height must be > 0"))); return; }
	if ( !(r > 0) ) { result = vda_err(thNEW(stdString,("pyramid: radius must be > 0"))); return; }
	if ( !(dx > 0) ) { result = vda_err(thNEW(stdString,("pyramid: last argument is dx (voxel size in world units) and must be > 0"))); return; }

	vdTriSink sink;
	srava_geo::make_pyramid(n, h, r, sink);
	out = sink.finish(dx);
	if ( ! out.is_notNull() )
		result = vda_err(thNEW(stdString,("pyramid: openvdb meshToLevelSet failed")));
	}, vdwhy) )
		result = vda_err(thNEW(stdString,(vdwhy.c_str())));
}

/* この演算の結果。エラー時は compute() が result にエラー値を残して本体未設定で return するので
 * result 優先。保存 (Writer 起動) は agent が出力 pigDataCache の set_body 経由で行う。 */
sPtr<pigData>
vdaPyramid_::get_result()
{
	return ( result != thNULL ) ? result : out;
}
