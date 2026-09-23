/*
 * ocaNverts — nverts(shape) の計算本体 (#3461)。
 * ★ **Vertex (稜の端点) の数**であって三角形の頂点数ではない。球は 2 (極) など、
 *   mesh 系の nverts とは桁が違う。nfaces と同じ理由で、あえて同じ op 名で出す。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"oc/c++/ocShape.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ocaNverts_.h"


CLASS_TINYSTATE(oc/c++/ocaNverts,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ocaNverts_(
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


ocaNverts_::ocaNverts_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/


void
ocaNverts_::compute()
{
	ocShape::ensure_init();   /* ★ OCCT の診断出力を stdout から外す (ocShape.h 参照) */
	int na = ( args != 0 ) ? args->length() : 0;
	/* ★★ #3547 (2026-09-18): **2D も受ける**。それまで oc-face3d は routing で断られていた
	 *   (型の穴であって計算の穴ではない — 数え方は 3D と同じ)。
	 *   ⇒ 分岐の順は ocaArea と同じく 2D を先に見る (どちらも d_cast なので排他)。 */
	sPtr<ocFace2D> f2 = ( na > 0 ) ? sPtr<ocFace2D>::d_cast((*args)[0]) : sPtr<ocFace2D>();
	if ( f2.is_notNull() ) {
		result = thNEW(pigDataInteger,((INTEGER64)f2->nverts()));
		return;
	}
	sPtr<ocShape> in = ( na > 0 ) ? sPtr<ocShape>::d_cast((*args)[0]) : sPtr<ocShape>();
	if ( ! in.is_notNull() ) {
		result = oca_err(thNEW(stdString,(
		    "nverts: input must be a 3D shape (oc-brep3d) or a 2D region (oc-face3d)")));
		return;
	}
	result = thNEW(pigDataInteger,((INTEGER64)in->nverts()));
}

sPtr<pigData>
ocaNverts_::get_result()
{
	return ( result != thNULL ) ? result : out;
}
