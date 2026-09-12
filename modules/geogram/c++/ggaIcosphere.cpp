/*
 * ggaIcosphere — icosphere(r, subdiv) — 測地球 (正二十面体を細分) の計算本体 (geogram 版・#3474)。
 * ★ common/geodesic.h の共通生成器 (sphere と同じ) なので他カーネルと一致する。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"gg/c++/ggMesh.h"
#include	"gg/c++/ggTriSink.h"
#include	"common/geodesic.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ggaIcosphere_.h"

CLASS_TINYSTATE(gg/c++/ggaIcosphere,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ggaIcosphere_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<ggMesh>	mesh;
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
class ggMesh;
TS_END_INTERFACE

#endif


ggaIcosphere_::ggaIcosphere_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
ggaIcosphere_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	double r      = ( na > 0 ) ? (*args)[0]->get_flt() : 1.0;
	int    subdiv = ( na > 1 ) ? (int)(*args)[1]->get_int() : 0;   /* 細分回数。0=20 面 */
	int    n      = srava_geo::subdiv_to_n(subdiv);
	if ( !(r > 0) ) { result = gga_err(thNEW(stdString,("icosphere: radius must be > 0"))); return; }

	ggTriSink sink;
	srava_geo::make_geodesic(srava_geo::SEED_ICOSAHEDRON, n, r, sink);
	mesh = sink.finish();
}

/* この演算の結果。エラー時は compute() が result にエラー値を残して本体未設定で return するので
 * result 優先。保存 (Writer 起動) は agent が出力 pigDataCache の set_body 経由で行う。 */
sPtr<pigData>
ggaIcosphere_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
