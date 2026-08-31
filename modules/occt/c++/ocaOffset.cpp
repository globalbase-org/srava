/*
 * ocaOffset — offset(s,d[,unused]) の計算本体。★**解析曲面を直接オフセット**する第 3 の原理 (#3437 P5)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"oc/c++/ocShape.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ocaOffset_.h"


CLASS_TINYSTATE(oc/c++/ocaOffset,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ocaOffset_(
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


ocaOffset_::ocaOffset_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/


void
ocaOffset_::compute()
{
	ocShape::ensure_init();   /* ★ OCCT の診断出力を stdout から外す (ocShape.h 参照) */
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<ocShape> in = ( na > 0 ) ? sPtr<ocShape>::d_cast((*args)[0]) : sPtr<ocShape>();
	if ( ! in.is_notNull() ) {
		result = thNEW(pigDataError,(thNEW(stdString,("offset: needs an OCCT shape"))));
		return;
	}
	double d = ( na > 1 ) ? (*args)[1]->get_flt() : 0.0;
	/* ★ 第 3 引数 (近似球の細分化) は **無視する**。nef の 3D offset が球との Minkowski 和で
	 *   実装されているためのパラメータで、OCCT は近似球を使わない (稜に円筒パッチ・頂点に
	 *   球パッチを解析的に生成する = Steiner の公式を構成的にやる)。パーサが offset を常に
	 *   3 引数へ正規化するので受け取りはするが、使わないのが正しい。 */
	out = in->op_offset(d);
	if ( ! out.is_notNull() )
		result = thNEW(pigDataError,(thNEW(stdString,("offset: OCCT MakeOffsetShape failed"))));
}

sPtr<pigData>
ocaOffset_::get_result()
{
	return ( result != thNULL ) ? result : out;
}
