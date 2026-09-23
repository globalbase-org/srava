/*
 * mfaBox — 直方体生成の計算本体(mf 版・cgaBox のミラー)。args=[w,h,d] or boxa([w,h,d])。
 * Manifold で原点隅の w×h×d 箱を作り mfMesh に保持。get_result() で agent へ返す(保存は set_body 経由・#3406 2026-07-30: get_body 統合)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"mf/c++/mfMesh.h"
#include	"mf/c++/ptsmfWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/mfaBox_.h"

CLASS_TINYSTATE(mf/c++/mfaBox,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	mfaBox_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<mfMesh>	mesh;
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
class mfMesh;
TS_END_INTERFACE

#endif


mfaBox_::mfaBox_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
mfaBox_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	double w = 1.0, h = 1.0, d = 1.0;
	sPtr<pigDataArray> dims = ( na == 1 ) ? (*args)[0]->obt_array()
	                                      : sPtr<pigDataArray>();
	if ( dims.is_notNull() ) {
		int nd = dims->length();
		if ( nd > 0 ) w = dims->get_ix(thNEW(pigDataInteger,((INTEGER64)0)))->get_flt();
		if ( nd > 1 ) h = dims->get_ix(thNEW(pigDataInteger,((INTEGER64)1)))->get_flt();
		if ( nd > 2 ) d = dims->get_ix(thNEW(pigDataInteger,((INTEGER64)2)))->get_flt();
	} else {
		if ( na > 0 ) w = (*args)[0]->get_flt();
		if ( na > 1 ) h = (*args)[1]->get_flt();
		if ( na > 2 ) d = (*args)[2]->get_flt();
	}
	/* ★ #3516: 退化・負の寸法を弾く (occt / openvdb は元から持っていた検査を揃えた)。
	 *   ⚠ 弾かないと「体積 0 の立体」や「負を正として扱った立体」が**黙って**下流へ流れる。
	 *     実測 (2026-09-12): 同じ box(-1,1,1) が nef=1 / cgal=-1 / manifold=0 / geogram=1 と
	 *     カーネルごとに違う値になり、nef は box(1,1,0) で **SIGSEGV** していた
	 *     (CGAL の SNC 構築が厚みゼロの面で落ちる。例外ではないので呼び手では受けられない)。
	 *   ★ boxa も同じクラスが受けるので、ここ 1 箇所で両方に効く。 */
	if ( !(w > 0) || !(h > 0) || !(d > 0) ) {
		result = mfa_err(thNEW(stdString,("box: sizes must be > 0")));
		return;
	}
	mesh = mfMesh::box(w, h, d);
}

/* この演算の結果 (#3406, 2026-07-30 メモ: get_body/get_result を統一)。エラー時は
 * compute() が result にエラー値を残して mesh を未設定のまま return するので result 優先。
 * 成功時は result=thNULL のままmeshを返す。保存(Writer 起動)は agent が出力 pigDataCache
 * の set_body 経由で行う。 */
sPtr<pigData>
mfaBox_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
