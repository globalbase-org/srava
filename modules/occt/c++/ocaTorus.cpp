/*
 * ocaTorus — torus(R,r) の計算本体 (#3437 P5)。R = 中心から管の中心までの距離、r = 管の半径。
 * ★ **厳密なトーラス面 1 枚**でできる (Face 数 1)。三角形近似が入らないので volume は
 *   2π²Rr² とちょうど一致し、**分割数という概念が無い**。
 * ★ トーラスは「メッシュ系では必ず近似になるが B-rep では厳密に持てる」形の代表例で、
 *   しかも fillet が作る曲面そのもの (稜の丸めは実質トーラス面) でもある。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"oc/c++/ocShape.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ocaTorus_.h"
#include	<BRepPrimAPI_MakeTorus.hxx>
#include	<gp_Ax2.hxx>
#include	<gp_Pnt.hxx>
#include	<gp_Dir.hxx>
#include	<TopoDS_Shape.hxx>

CLASS_TINYSTATE(oc/c++/ocaTorus,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ocaTorus_(
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


ocaTorus_::ocaTorus_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
ocaTorus_::compute()
{
	ocShape::ensure_init();   /* ★ OCCT の診断出力を stdout から外す (ocShape.h 参照) */
	int na = ( args != 0 ) ? args->length() : 0;
	double R = ( na > 0 ) ? (*args)[0]->get_flt() : 1.0;
	double r = ( na > 1 ) ? (*args)[1]->get_flt() : 0.25;
	if ( !(R > 0) || !(r > 0) ) {
		result = thNEW(pigDataError,(thNEW(stdString,("torus: R and r must be > 0"))));
		return;
	}
	if ( r >= R ) {
		/* r >= R は自己交差する (中央の穴が潰れる)。OCCT は作ってしまうことがあるので先に弾く。 */
		result = thNEW(pigDataError,(thNEW(stdString,("torus: r must be < R (self-intersecting otherwise)"))));
		return;
	}
	/* 原点中心・軸は +Z (穴が Z 方向に空く)。 */
	gp_Ax2 ax(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1));
	BRepPrimAPI_MakeTorus mk(ax, R, r);
	/* ★ プリミティブは遅延構築。IsDone() は立たないので IsNull() で見る (ocaBox と同じ罠)。 */
	TopoDS_Shape sh = mk.Shape();
	if ( sh.IsNull() ) {
		result = thNEW(pigDataError,(thNEW(stdString,("torus: OCCT produced a null shape"))));
		return;
	}
	out = thNEW(ocShape,());
	out->set_shape(sh);
}

sPtr<pigData>
ocaTorus_::get_result()
{
	return ( result != thNULL ) ? result : out;
}
