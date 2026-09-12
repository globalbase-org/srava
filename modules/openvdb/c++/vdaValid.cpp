/*
 * vdaValid — valid(m) — 妥当なら 1・そうでなければ 0。
 *
 * ★ 値を返すだけの op (→value)。**メッシュを作らずに**格子から出す (体積と同じ方針)。
 *   解像度に依存する近似値なので、メッシュ系との一致は相対誤差で見る。
 * ★ valid の共通定義 (① 空でない ∧ ② 閉じている ∧ ③ 自己交差が無い) のうち、
 *   **②③ は距離場では構造的に恒真** (境界も自己交差も表現できない) ので ① だけを見る。
 *   これは「別のことを答えている」のではなく「その表現では ②③ が恒真」という事実。
 *   定義を先に決めた経緯は src/h/common/meshprops.h の冒頭。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"vd/c++/vdGrid.h"
#include	"vd/c++/vdArena.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/vdaValid_.h"

#include	<string>

CLASS_TINYSTATE(vd/c++/vdaValid,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	vdaValid_(
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
TS_END_INTERFACE

#endif


vdaValid_::vdaValid_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
vdaValid_::compute()
{
	std::string vdwhy;
	/* ★ #3474 続き: 例外境界。openvdb が投げると受け手が無く、ワーカースレッド
	 *   由来なら agent ごと死ぬ (vdArena.h の vd_arena_guard 参照)。 */
	if ( ! vd_arena_guard("valid", [&]{

	vdGrid::ensure_init();
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<vdGrid> in = ( na > 0 ) ? sPtr<vdGrid>::d_cast((*args)[0]) : sPtr<vdGrid>();
	if ( ! in.is_notNull() ) {
		result = vda_err(thNEW(stdString,("valid: needs an openvdb grid")));
		return;
	}
	result = thNEW(pigDataInteger,((INTEGER64)in->op_valid()));
	}, vdwhy) )
		result = vda_err(thNEW(stdString,(vdwhy.c_str())));
}
