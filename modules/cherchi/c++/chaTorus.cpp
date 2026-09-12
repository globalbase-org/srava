/*
 * chaTorus — torus(R, r, seg) — トーラス (原点中心・軸 +Z) の計算本体 (cherchi 版・#3474)。
 * ★ common/solids.h の共通生成器なので、他のメッシュ系カーネルと
 *   **頂点・面の並びが一致する** (geodesic.h の sphere と同じ方針)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"ch/c++/chMesh.h"
#include	"ch/c++/chTriSink.h"
#include	"common/solids.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/chaTorus_.h"

CLASS_TINYSTATE(ch/c++/chaTorus,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	chaTorus_(
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


chaTorus_::chaTorus_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
chaTorus_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	double R   = ( na > 0 ) ? (*args)[0]->get_flt() : 1.0;
	double r   = ( na > 1 ) ? (*args)[1]->get_flt() : 0.25;
	int    seg = ( na > 2 ) ? (int)(*args)[2]->get_int() : 0;   /* 大円・管断面ともこの分割数。0=既定 32 */
	if ( !(R > 0) ) { result = cha_err(thNEW(stdString,("torus: R (distance from the axis to the tube center) must be > 0"))); return; }
	if ( !(r > 0) ) { result = cha_err(thNEW(stdString,("torus: r (tube radius) must be > 0"))); return; }
	if ( !(r < R) ) { result = cha_err(thNEW(stdString,("torus: r must be < R (self-intersecting otherwise)"))); return; }

	chTriSink sink;
	srava_geo::make_torus(R, r, seg, sink);
	mesh = sink.finish();
}

/* この演算の結果。エラー時は compute() が result にエラー値を残して本体未設定で return するので
 * result 優先。保存 (Writer 起動) は agent が出力 pigDataCache の set_body 経由で行う。 */
sPtr<pigData>
chaTorus_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
