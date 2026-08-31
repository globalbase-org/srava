/*
 * nfaMinkowski — minkowski(a,b) — ★Nef 固有の Minkowski 和 A ⊕ B (#3440)。
 *
 *   offset(A, r) = A ⊕ (半径 r の球) なので、**こちらがプリミティブ**で offset がその特殊形。
 *   cgal.so の 3D offset は中身が Nef + 凸分解であり、モジュール境界の約束①
 *   (他の幾何カーネルの機能を借りて自分の顔で出さない) に違反していた。まずここに
 *   プリミティブを置き、次に 3D offset を移設する (#3440 の順序 1 → 2 → 3)。
 *
 *   コストは *凸分解のペア数* で効く: A を m 個・B を n 個の凸片に分けて m×n の和を取り
 *   全部 union する (CGAL::minkowski_sum_3 が内部でやる)。最悪 O(n^3 m^3)。
 *
 * ★入力は**両方とも有界**でなければならない。非有界を渡すと CGAL は stderr に文言を出して
 *   片方をそのまま返す = **黙って嘘の答えになる**ので、ここで先に弾いて明示エラーにする。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"nf/c++/nfMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/nfaMinkowski_.h"

CLASS_TINYSTATE(nf/c++/nfaMinkowski,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	nfaMinkowski_(
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


nfaMinkowski_::nfaMinkowski_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
nfaMinkowski_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<nfMesh> a = ( na > 0 ) ? sPtr<nfMesh>::d_cast((*args)[0]) : sPtr<nfMesh>();
	sPtr<nfMesh> b = ( na > 1 ) ? sPtr<nfMesh>::d_cast((*args)[1]) : sPtr<nfMesh>();
	if ( ! a.is_notNull() || ! b.is_notNull() ) {
		result = thNEW(pigDataError,(thNEW(stdString,("minkowski: needs two Nef meshes"))));
		return;
	}
	/* ★非有界は CGAL に渡さない (黙って片方が返ってくる)。complement の結果などがここに来る。 */
	if ( ! a->is_bounded() || ! b->is_bounded() ) {
		result = thNEW(pigDataError,(thNEW(stdString,
		    ("minkowski: an unbounded Nef has no Minkowski sum (e.g. the result of complement)"))));
		return;
	}
	mesh = a->op_minkowski(b);
	if ( ! mesh.is_notNull() )
		result = thNEW(pigDataError,(thNEW(stdString,("minkowski: computation failed"))));
}

/* この演算の結果。エラー時は compute() が result にエラー値を残して mesh 未設定で return するので
 * result 優先。保存 (Writer 起動) は agent が出力 pigDataCache の set_body 経由で行う。 */
sPtr<pigData>
nfaMinkowski_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
