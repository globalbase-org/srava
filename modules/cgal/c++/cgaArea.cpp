/*
 * cgaArea — area(mesh) の計算本体(ptsCalcBody 派生)= 面積を返す**値返し op**(多態 op_area に委譲)。
 *   2D=囲み面積(外周−穴) / 3D=表面積。√を含むので double → pigDataFloat で返す。
 * 値返し op の作法: compute() で result(pigData)を立てるだけ。get_result は基底の result のまま
 * (#3406 2026-07-30: get_body 統合) → cgatsAgent が WriterText で保存し、プランナが VALUE パースで構造化して観測。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"cg/c++/cgMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/cgaArea_.h"

CLASS_TINYSTATE(cg/c++/cgaArea,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	cgaArea_(
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


cgaArea_::cgaArea_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
cgaArea_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<cgMesh> in = ( na > 0 ) ? sPtr<cgMesh>::d_cast((*args)[0]) : sPtr<cgMesh>();
	if ( ! in.is_notNull() ) {
		result = thNEW(pigDataError,(thNEW(stdString,("area: needs a mesh"))));
		return;
	}
	result = thNEW(pigDataFloat,(in->op_area()));
}
