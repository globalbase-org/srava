/*
 * chaPrism — prism(n, h, r) — 正 n 角柱 (底面 z=0・天面 z=h) の計算本体 (cherchi 版・#3474)。
 * ★ common/solids.h の共通生成器なので他カーネルと頂点・面の並びが一致する。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"ch/c++/chMesh.h"
#include	"ch/c++/chTriSink.h"
#include	"common/solids.h"
#include	"ts2/c++/stdString.h"
#include	"common/segs.h"   /* ★ #3530: segs / n の共通検査 */
#include	"_ts2/c++/chaPrism_.h"

CLASS_TINYSTATE(ch/c++/chaPrism,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	chaPrism_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<chMesh>	mesh;
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
class chMesh;
TS_END_INTERFACE

#endif


chaPrism_::chaPrism_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
chaPrism_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	int    n = ( na > 0 ) ? (int)(*args)[0]->get_int() : 3;
	double h = ( na > 1 ) ? (*args)[1]->get_flt() : 1.0;
	double r = ( na > 2 ) ? (*args)[2]->get_flt() : 1.0;
	if ( srava_geo::check_sides(n, &n) != srava_geo::SEGS_OK ) {   /* ★ #3530: n は形そのもの = 既定値なし */
		result = cha_err(thNEW(stdString,(srava_geo::sides_error("prism").c_str()))); return; }
	if ( !(h > 0) ) { result = cha_err(thNEW(stdString,("prism: height must be > 0"))); return; }
	if ( !(r > 0) ) { result = cha_err(thNEW(stdString,("prism: radius must be > 0"))); return; }

	chTriSink sink;
	srava_geo::make_prism(n, h, r, sink);
	mesh = sink.finish();
}

/* この演算の結果。エラー時は compute() が result にエラー値を残して本体未設定で return するので
 * result 優先。保存 (Writer 起動) は agent が出力 pigDataCache の set_body 経由で行う。 */
sPtr<pigData>
chaPrism_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
