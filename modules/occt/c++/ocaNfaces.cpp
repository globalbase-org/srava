/*
 * ocaNfaces — nfaces(shape) の計算本体 (#3437)。
 * ★ **三角形数ではなく Face 数**を返す。円筒の側面は 1 面、トーラスは全体で 1 面なので、
 *   mesh 系の nfaces とは桁が違う値になる。**その違いこそがこの表現の要点**なので、
 *   あえて同じ op 名で出す (利用者が「同じ問いを両方の表現に投げて比べられる」ことを優先)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"oc/c++/ocShape.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ocaNfaces_.h"


CLASS_TINYSTATE(oc/c++/ocaNfaces,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ocaNfaces_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<ocShape>	out;
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
class ocShape;
TS_END_INTERFACE

#endif


ocaNfaces_::ocaNfaces_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/


void
ocaNfaces_::compute()
{
	ocShape::ensure_init();   /* ★ OCCT の診断出力を stdout から外す (ocShape.h 参照) */
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<ocShape> in = ( na > 0 ) ? sPtr<ocShape>::d_cast((*args)[0]) : sPtr<ocShape>();
	if ( ! in.is_notNull() ) {
		result = thNEW(pigDataError,(thNEW(stdString,("nfaces: needs an OCCT shape"))));
		return;
	}
	result = thNEW(pigDataInteger,((INTEGER64)in->nfaces()));
}

sPtr<pigData>
ocaNfaces_::get_result()
{
	return ( result != thNULL ) ? result : out;
}
