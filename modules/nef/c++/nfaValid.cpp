/*
 * nfaValid — valid(m) — 妥当なら 1・そうでなければ 0。
 *
 * ★ 値を返すだけの op (→value)。2D 型を要さないので、2D 型を持たないこのカーネルでも置ける。
 *   無いと「値の素性を訊く」ためだけに別カーネルへ cast させることになり、cast が通らない
 *   値 (非有界・非多様体) では確認手段そのものが消える (#3478) — それが #3487 の動機。
 * ★ valid の定義は 7 カーネル共通で ① 空でない ∧ ② 閉じている ∧ ③ 自己交差が無い。
 *   定義を先に決めた経緯と根拠は src/h/common/meshprops.h の冒頭にある。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"nf/c++/nfMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/nfaValid_.h"

CLASS_TINYSTATE(nf/c++/nfaValid,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	nfaValid_(
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


nfaValid_::nfaValid_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
nfaValid_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<nfNefMesh> in = ( na > 0 ) ? sPtr<nfNefMesh>::d_cast((*args)[0]) : sPtr<nfNefMesh>();
	if ( ! in.is_notNull() ) {
		result = nfa_err(thNEW(stdString,("valid: needs a Nef mesh")));
		return;
	}
	result = thNEW(pigDataInteger,((INTEGER64)in->op_valid()));
}
