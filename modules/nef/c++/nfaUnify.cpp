/*
 * nfaUnify — unify(mesh) — 内壁除去 = 重なる/接する複数立体からソリッドを再構成する (#3442)。
 *
 *   ★**@repair@ とは別物**。repair は形を変えない (自己交差を幾何的に解消するだけ) が、
 *   こちらは **体積が変わる** — 重なりが 1 回だけ数えられ、内部の仕切り面が消える。
 *   同じ名前にすると「直すつもりが形が変わっていた」が起きるので名前を分ける (ひさ判断)。
 *   ★**自動ではやらない**: 黙って形を変えない / Nef 構築は重い / 部品が残っているなら union で書けばよい。
 *
 *   実体は「連結成分ごとの Nef の **和**」。暗黙の境界→Nef 変換 (set_from_mesh) は
 *   **XOR (even-odd)** なので、重なりのある入力では答えが違う = 明示 op でなければならない。
 *   有効な立体 (シェルが交差しない) では両者は一致するので、この op は重なりのある入力にだけ効く。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"nf/c++/nfMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/nfaUnify_.h"

CLASS_TINYSTATE(nf/c++/nfaUnify,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	nfaUnify_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<nfMesh>	mesh;
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
class nfMesh;
TS_END_INTERFACE

#endif


nfaUnify_::nfaUnify_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
nfaUnify_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<nfMesh> in = ( na > 0 ) ? sPtr<nfMesh>::d_cast((*args)[0]) : sPtr<nfMesh>();
	if ( ! in.is_notNull() ) {
		result = thNEW(pigDataError,(thNEW(stdString,("unify: needs a Nef mesh"))));
		return;
	}
	mesh = in->op_unify_shells();
	if ( ! mesh.is_notNull() )
		result = thNEW(pigDataError,(thNEW(stdString,
		    ("unify: cannot rebuild a solid (an unbounded Nef, or nothing to unify)"))));
}

/* この演算の結果。エラー時は compute() が result にエラー値を残して mesh 未設定で return するので
 * result 優先。保存 (Writer 起動) は agent が出力 pigDataCache の set_body 経由で行う。 */
sPtr<pigData>
nfaUnify_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
