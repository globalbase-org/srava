/*
 * cgaDifference — 2 メッシュの差(corefinement difference)の計算本体(ptsCalcBody 派生)。
 * cgaUnion と同型(差は CGAL 関数のみ)。args=[meshA, meshB] は cgMesh。結果 = A - B(非可換: 順序重要)。
 * corefine_and_compute_difference(A, B, out) で A から B を引き、cgMesh に保持して get_result() で返す(#3406 2026-07-30)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"cg/c++/cgMesh.h"
#include	"cg/c++/cgaBoolError.h"
#include	"cg/c++/ptscgWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/cgaDifference_.h"

CLASS_TINYSTATE(cg/c++/cgaDifference,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	cgaDifference_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<cgMesh>	mesh;
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
class cgMesh;
TS_END_INTERFACE

#endif


cgaDifference_::cgaDifference_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
cgaDifference_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<cgMesh> ma = ( na > 0 ) ? sPtr<cgMesh>::d_cast((*args)[0]) : sPtr<cgMesh>();
	sPtr<cgMesh> mb = ( na > 1 ) ? sPtr<cgMesh>::d_cast((*args)[1]) : sPtr<cgMesh>();
	if ( ! ma.is_notNull() || ! mb.is_notNull() ) {
		result = thNEW(pigDataError,(cga_missing_operand_msg("difference",
		     (na>0)?(*args)[0]:sPtr<pigData>(), (na>1)?(*args)[1]:sPtr<pigData>(), na)));
		return;
	}
	mesh = ma->op_difference(mb);   /* 多態。非可換: this(A) から b(B) を引く */
	if ( ! mesh.is_notNull() ) {
		const char *m = ( ma->dim() != mb->dim() )
		    ? "difference: cannot mix 2D and 3D operands"
		    : "difference: boolean failed. Operands must be closed solids that do not touch tangentially or "
		      "share coplanar faces, and must not self-intersect (an earlier boolean may have made "
		      "invalid geometry — check valid()). Overlap operands slightly (e.g. by 0.01) instead of "
		      "making them exactly touch.";
		result = thNEW(pigDataError,(thNEW(stdString,(m))));
	}
}

/* この演算の結果 (#3406, 2026-07-30 メモ: get_body/get_result を統一)。エラー時は
 * compute() が result にエラー値を残して mesh を未設定のまま return するので result 優先。
 * 成功時は result=thNULL のままmeshを返す。保存(Writer 起動)は agent が出力 pigDataCache
 * の set_body 経由で行う。 */
sPtr<pigData>
cgaDifference_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
