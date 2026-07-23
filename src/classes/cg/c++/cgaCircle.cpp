/*
 * cgaCircle — circle(r) の計算本体(ptsCalcBody 派生)= 円の多角形近似(2D, 半径 r, 既定 32 分割)。
 * 厳密円(円弧)は持たず正多角形で近似(回転・球と同じ近似方針)。細かさが要れば ngon(n, r) を使う。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"cg/c++/cgMesh.h"
#include	"cg/c++/ptscgWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/cgaCircle_.h"

/* cgaNgon.cpp 定義の共有ヘルパ(正 n 角形 CCW を返す)。 */
cgMesh2D::Polygon_2 cga_regular_polygon(int n, double r);

CLASS_TINYSTATE(cg/c++/cgaCircle,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	cgaCircle_(
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


cgaCircle_::cgaCircle_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
cgaCircle_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	double r    = ( na > 0 ) ? (*args)[0]->get_flt() : 1.0;
	int    segs = ( na > 1 ) ? (int)(*args)[1]->get_int() : 32;   /* 辺数(精度ピッチ)。既定 32 */
	if ( segs < 3 ) segs = 3;                                     /* 三角形未満は無意味 */
	mesh = thNEW(cgMesh2D,());
	mesh->regions().push_back(cgMesh2D::Pwh_2(cga_regular_polygon(segs, r)));
}

sPtr<ptsWireCacheStreamWriter>
cgaCircle_::get_writer()
{
	return thNEW(ptscgWireCacheStreamWriterMesh,(parent, target, mesh));
}
