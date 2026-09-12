/*
 * ggaArea — area(m) — 表面積を返す。
 *
 * ★ 値を返すだけの op (→value)。2D 型を要さないので、2D 型を持たないこのカーネルでも置ける。
 *   無いと「値の素性を訊く」ためだけに別カーネルへ cast させることになり、cast が通らない
 *   値 (非有界・非多様体) では確認手段そのものが消える (#3478) — それが #3487 の動機。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"gg/c++/ggMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ggaArea_.h"

CLASS_TINYSTATE(gg/c++/ggaArea,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ggaArea_(
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


ggaArea_::ggaArea_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
ggaArea_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<ggMesh> in = ( na > 0 ) ? sPtr<ggMesh>::d_cast((*args)[0]) : sPtr<ggMesh>();
	if ( ! in.is_notNull() ) {
		result = gga_err(thNEW(stdString,("area: needs a geogram mesh")));
		return;
	}
	result = thNEW(pigDataFloat,(in->op_area()));
}
