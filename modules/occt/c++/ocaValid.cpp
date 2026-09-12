/*
 * ocaValid — valid(m) — 妥当なら 1・そうでなければ 0。
 *
 * ★ 値を返すだけの op (→value)。B-rep のまま測るので、解析曲面 (球・円柱・トーラス) では
 *   mesh 系の内接多面体と **構造的に違う値**が出る (体積と同じ事情)。検証は閉形式で行う。
 * ★ valid の定義は 7 カーネル共通で ① 空でない ∧ ② 閉じている ∧ ③ 自己交差が無い。
 *   occt では BRepAlgoAPI_Check が ②③ をまとめて答える (BRepCheck_Analyzer +
 *   BOPAlgo_CheckerSI)。定義を先に決めた経緯は src/h/common/meshprops.h の冒頭。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"oc/c++/ocShape.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ocaValid_.h"

CLASS_TINYSTATE(oc/c++/ocaValid,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ocaValid_(
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


ocaValid_::ocaValid_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
ocaValid_::compute()
{
	ocShape::ensure_init();
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<ocShape> in = ( na > 0 ) ? sPtr<ocShape>::d_cast((*args)[0]) : sPtr<ocShape>();
	if ( ! in.is_notNull() ) {
		result = oca_err(thNEW(stdString,("valid: needs an OCCT shape")));
		return;
	}
	result = thNEW(pigDataInteger,((INTEGER64)in->op_valid()));
}
