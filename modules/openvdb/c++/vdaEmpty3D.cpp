/*
 * vdaEmpty3D — empty3d() — **値としての空集合** (openvdb 版・#3474)。
 * ★ `{}` (空ハッシュ) との違い: `{}` は **fold の中立元**で「演算子を適用しない印」。
 *   empty3d() は **空集合そのもの**なので intersection(a, empty3d()) は正しく空になる。
 * ★ openvdb だけ **dx (ボクセルサイズ) を取る**。ボリューム同士の合成は transform の
 *   一致を要求するので、空でも「どの格子の上の空か」を決めないと使えない。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"vd/c++/vdGrid.h"
#include	"vd/c++/vdArena.h"
#include	<openvdb/tools/LevelSetUtil.h>
#include	"ts2/c++/stdString.h"
#include	<string>
#include	"_ts2/c++/vdaEmpty3D_.h"

CLASS_TINYSTATE(vd/c++/vdaEmpty3D,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	vdaEmpty3D_(
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


vdaEmpty3D_::vdaEmpty3D_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
vdaEmpty3D_::compute()
{
	std::string vdwhy;
	/* ★ #3474 続き: 例外境界。openvdb が投げると受け手が無く、ワーカースレッド
	 *   由来なら agent ごと死ぬ (vdArena.h の vd_arena_guard 参照)。 */
	if ( ! vd_arena_guard("empty3d", [&]{

	vdGrid::ensure_init();
	int na = ( args != 0 ) ? args->length() : 0;
	double dx = ( na > 0 ) ? (*args)[0]->get_flt() : 0.0;
	if ( !(dx > 0) ) {
		result = vda_err(thNEW(stdString,(
		    "empty3d: argument is dx (voxel size in world units) and must be > 0")));
		return;
	}
	/* ★ 活性ボクセルが 1 つも無い level set = 空集合。★ dx を取るのは、ボリューム同士の
	 *   合成が transform 一致を要求するため — 空でも「どの格子の上の空か」が要る
	 *   (bool_from_args が transform 全体を突き合わせて明示エラーにする)。 */
	openvdb::FloatGrid::Ptr g = openvdb::createLevelSet<openvdb::FloatGrid>(dx);
	if ( ! g ) {
		result = vda_err(thNEW(stdString,("empty3d: openvdb could not create a level set")));
		return;
	}
	out = thNEW(vdGrid,());
	out->set_grid(g);
	}, vdwhy) )
		result = vda_err(thNEW(stdString,(vdwhy.c_str())));
}

sPtr<pigData>
vdaEmpty3D_::get_result()
{
	return ( result != thNULL ) ? result : out;
}
