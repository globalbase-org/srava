/*
 * nfaConvexDecomposition — convex_decomposition(mesh) — 凹形状を凸片へ分解する (#3441)。
 *
 *   ★Nef 固有。Minkowski 和が内部で使っているのと同じ分解で、offset が重いのはここのペア数が
 *   m×n で効くため。単独 op としても有用 (物理エンジンの凸コリジョン形状・3D プリントのサポート生成)。
 *
 *   ★**返し方**: 凸片は「複数の立体」なので本来は mesh の配列で返したいが、今の op 表は
 *   出力が単一 cache か値なので書けない。当面 **1 つの mesh の中に凸片を別の連結成分として**
 *   持たせる (仕切り面つきの Nef)。volume は片の合計 (= 分解前と同じ)・export は片が別成分で出る。
 *   個数だけ要るなら convex_pieces(m)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"nf/c++/nfMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/nfaConvexDecomposition_.h"

CLASS_TINYSTATE(nf/c++/nfaConvexDecomposition,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	nfaConvexDecomposition_(
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


nfaConvexDecomposition_::nfaConvexDecomposition_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
nfaConvexDecomposition_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<nfMesh> in = ( na > 0 ) ? sPtr<nfMesh>::d_cast((*args)[0]) : sPtr<nfMesh>();
	if ( ! in.is_notNull() ) {
		result = thNEW(pigDataError,(thNEW(stdString,("convex_decomposition: needs a Nef mesh"))));
		return;
	}
	/* ★非有界は凸分解できない (CGAL の前提)。先に弾いて明示エラーにする。 */
	if ( ! in->is_bounded() ) {
		result = thNEW(pigDataError,(thNEW(stdString,
		    ("convex_decomposition: an unbounded Nef cannot be decomposed (e.g. the result of complement)"))));
		return;
	}
	mesh = in->op_convex_decomposition();
	if ( ! mesh.is_notNull() )
		result = thNEW(pigDataError,(thNEW(stdString,("convex_decomposition: computation failed"))));
}

/* この演算の結果。エラー時は compute() が result にエラー値を残して mesh 未設定で return するので
 * result 優先。保存 (Writer 起動) は agent が出力 pigDataCache の set_body 経由で行う。 */
sPtr<pigData>
nfaConvexDecomposition_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
