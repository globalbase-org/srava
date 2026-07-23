/*
 * cgaLine — line([[x0,y0],[x1,y1],...]) の計算本体(ptsCalcBody 派生)= 2D ガイド(寸法線)。
 * 塗り領域(regions)ではなく開ポリライン 1 本を cgMesh2D の guides 層に作る。ブール演算の対象外で、
 * SVG ではストローク(細線)、DXF ではレイヤ GUIDES の開 LWPOLYLINE として描かれる。
 * 部品に重ねる用途: `export("p.svg", part +++ line([[0,-5],[260,-5]]), "mm")`。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"cg/c++/cgMesh.h"
#include	"cg/c++/ptscgWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/cgaLine_.h"

CLASS_TINYSTATE(cg/c++/cgaLine,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	cgaLine_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<ptsWireCacheStreamWriter>	get_writer();
protected:
	virtual void	compute();
	sPtr<cgMesh2D>	mesh;
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
class cgMesh2D;
class ptsWireCacheStreamWriter;
TS_END_INTERFACE

#endif


cgaLine_::cgaLine_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
cgaLine_::compute()
{
	typedef cgMesh::K K;
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<pigDataArray> pts = ( na > 0 ) ? sPtr<pigDataArray>::d_cast((*args)[0])
	                                    : sPtr<pigDataArray>();
	mesh = thNEW(cgMesh2D,());
	int np = pts.is_notNull() ? pts->length() : 0;
	if ( np < 2 ) {
		result = thNEW(pigDataError,(thNEW(stdString,(
		    "line: needs >= 2 points [[x,y],...]"))));
		return;
	}
	cgMesh2D::Guide g;
	for ( int i = 0 ; i < np ; ++i ) {
		sPtr<pigDataArray> xy = sPtr<pigDataArray>::d_cast(
		    pts->get_ix(thNEW(pigDataInteger,((INTEGER64)i))));
		if ( ! xy.is_notNull() || xy->length() < 2 ) {
			result = thNEW(pigDataError,(thNEW(stdString,(
			    "line: each point must be [x,y]"))));
			return;
		}
		double x = xy->get_ix(thNEW(pigDataInteger,((INTEGER64)0)))->get_flt();
		double y = xy->get_ix(thNEW(pigDataInteger,((INTEGER64)1)))->get_flt();
		g.push_back(K::Point_2(K::FT(x), K::FT(y)));
	}
	mesh->guides().push_back(g);   /* regions は空・guides 層のみ(面積 0 のガイド) */
}

sPtr<ptsWireCacheStreamWriter>
cgaLine_::get_writer()
{
	return thNEW(ptscgWireCacheStreamWriterMesh,(parent, target, mesh));
}
