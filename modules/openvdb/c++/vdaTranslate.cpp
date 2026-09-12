/*
 * vdaTranslate — translate の計算本体 (#3463)。
 * ★ 引数の解釈は manifold / occt と同一。変換の実体は vdGrid::op_affine に集約してあり、
 *   **格子は不変のままボクセルを動かす** (resampleToMatch)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"vd/c++/vdGrid.h"
#include	"common/affine.h"   /* アフィン変換の共通規約 (#3486) */
#include	"vd/c++/vdArena.h"   /* ★ #3441: op あたりの TBB 予算 */
#include	"ts2/c++/stdString.h"
#include	<string>
#include	"_ts2/c++/vdaTranslate_.h"

#include	<stdio.h>

CLASS_TINYSTATE(vd/c++/vdaTranslate,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	vdaTranslate_(
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


vdaTranslate_::vdaTranslate_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
vdaTranslate_::compute()
{
	std::string vdwhy;
	/* ★ #3474 続き: 例外境界。openvdb が投げると受け手が無く、ワーカースレッド
	 *   由来なら agent ごと死ぬ (vdArena.h の vd_arena_guard 参照)。 */
	if ( ! vd_arena_guard("translate", [&]{

	vdGrid::ensure_init();
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<vdGrid> in = ( na > 0 ) ? sPtr<vdGrid>::d_cast((*args)[0]) : sPtr<vdGrid>();
	if ( ! in.is_notNull() ) {
		result = vda_err(thNEW(stdString,("translate: needs an openvdb grid")));
		return;
	}
	sPtr<pigData> arg = ( na > 1 ) ? (*args)[1] : sPtr<pigData>();

	/* ★ 引数の解釈と行列の組み立ては **common/affine.h** (カーネル非依存・7 モジュール共通)。
	 *   受け付ける書き方だけでなく **拒否の理由** もそこに集約してある (#3486)。
	 *   理由の受け皿 buf は呼び手が持つ (モジュール側に static を置かない)。 */
	double e[12];
	const char *why = 0;
	char buf[256];
	if ( ! srava_affine::matrix_translate(arg, e, &why, buf, (int)sizeof buf) ) {
		result = vda_err(thNEW(stdString,(why)));
		return;
	}
	out = in->op_affine(e);
	if ( ! out.is_notNull() )
		result = vda_err(thNEW(stdString,(
		    "translate: openvdb resampleToMatch failed")));
	}, vdwhy) )
		result = vda_err(thNEW(stdString,(vdwhy.c_str())));
}

sPtr<pigData>
vdaTranslate_::get_result()
{
	return ( result != thNULL ) ? result : out;
}
