/*
 * mfaSection — section(mesh, P, N) の計算本体(mf 版・cgaSection の Z 平面ケースのミラー)。
 * 3D(mfMesh)を平面で切った 2D 断面(mfCross)を返す。Manifold::Slice は **Z 法線の平面のみ**対応
 *  → N=[0,0,1](Z 法線)のときだけ Slice(P[2])。それ以外の任意平面は未対応(明示エラー)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"mf/c++/mfMesh.h"
#include	"mf/c++/ptsmfWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	<cmath>
#include	"_ts2/c++/mfaSection_.h"

CLASS_TINYSTATE(mf/c++/mfaSection,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	mfaSection_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<mfCross>	cross;
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
class mfCross;
TS_END_INTERFACE

#endif


mfaSection_::mfaSection_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

static void read3(sPtr<pigData> v, double out[3], double dflt) {
	out[0] = out[1] = 0.0; out[2] = dflt;
	sPtr<pigDataArray> a = v.is_notNull() ? v->obt_array() : sPtr<pigDataArray>();
	if ( a.is_notNull() ) {
		int n = a->length();
		if ( n > 0 ) out[0] = a->get_ix(thNEW(pigDataInteger,((INTEGER64)0)))->get_flt();
		if ( n > 1 ) out[1] = a->get_ix(thNEW(pigDataInteger,((INTEGER64)1)))->get_flt();
		if ( n > 2 ) out[2] = a->get_ix(thNEW(pigDataInteger,((INTEGER64)2)))->get_flt();
	}
}

void
mfaSection_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<mfMesh> in = ( na > 0 ) ? sPtr<mfMesh>::d_cast((*args)[0]) : sPtr<mfMesh>();
	if ( ! in.is_notNull() ) {
		result = thNEW(pigDataError,(thNEW(stdString,("section: needs a 3D mesh"))));
		return;
	}
	double P[3], N[3];
	read3( (na > 1) ? (*args)[1] : sPtr<pigData>(), P, 0.0 );
	read3( (na > 2) ? (*args)[2] : sPtr<pigData>(), N, 1.0 );
	/* Manifold::Slice は Z 法線の平面のみ。N が ±Z 以外なら未対応。 */
	double nlen = ::sqrt(N[0]*N[0] + N[1]*N[1] + N[2]*N[2]);
	int zaxis = ( nlen > 0.0 && ::fabs(N[0]/nlen) < 1e-9 && ::fabs(N[1]/nlen) < 1e-9 );
	if ( ! zaxis ) {
		result = thNEW(pigDataError,(thNEW(stdString,(
		    "section: Manifold kernel supports only Z-normal planes ([0,0,1]); use exact kernel for arbitrary planes"))));
		return;
	}
	manifold::Polygons ps = in->manifold().Slice(P[2]);
	cross = thNEW(mfCross,(manifold::CrossSection(ps, manifold::CrossSection::FillRule::NonZero)));
}

/* この演算の結果 (#3406, 2026-07-30 メモ: get_body/get_result を統一)。エラー時は
 * compute() が result にエラー値を残して cross を未設定のまま return するので result 優先。
 * 成功時は result=thNULL のままcrossを返す。保存(Writer 起動)は agent が出力 pigDataCache
 * の set_body 経由で行う。 */
sPtr<pigData>
mfaSection_::get_result()
{
	return ( result != thNULL ) ? result : cross;
}
