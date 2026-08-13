/*
 * cgaNgon — ngon(n, r) の計算本体(ptsCalcBody 派生)= 正 n 角形(2D, 外接半径 r, 原点中心, CCW)。
 * 頂点は角度 2πk/n の cos/sin(double)を K::FT に格納(回転と同じく角度近似・座標は EPECK 有理数)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"cg/c++/cgMesh.h"
#include	"cg/c++/ptscgWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/cgaNgon_.h"

#include	<math.h>

CLASS_TINYSTATE(cg/c++/cgaNgon,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	cgaNgon_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

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
TS_END_INTERFACE

#endif


cgaNgon_::cgaNgon_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

/* 正 n 角形(CCW)を返す共有ヘルパ(circle も使う)。 */
cgMesh2D::Polygon_2
cga_regular_polygon(int n, double r)
{
	typedef cgMesh::K K;
	if ( n < 3 ) n = 3;
	cgMesh2D::Polygon_2 p;
	for ( int k = 0 ; k < n ; ++k ) {
		double a = 2.0 * M_PI * (double)k / (double)n;   /* CCW */
		p.push_back(K::Point_2(K::FT(r * ::cos(a)), K::FT(r * ::sin(a))));
	}
	return p;
}

void
cgaNgon_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	int    n = ( na > 0 ) ? (int)(*args)[0]->get_int() : 3;
	double r = ( na > 1 ) ? (*args)[1]->get_flt() : 1.0;
	mesh = thNEW(cgMesh2D,());
	mesh->regions().push_back(cgMesh2D::Pwh_2(cga_regular_polygon(n, r)));
}

/* この演算の結果 (#3406, 2026-07-30 メモ: get_body/get_result を統一)。エラー時は
 * compute() が result にエラー値を残して mesh を未設定のまま return するので result 優先。
 * 成功時は result=thNULL のままmeshを返す。保存(Writer 起動)は agent が出力 pigDataCache
 * の set_body 経由で行う。 */
sPtr<pigData>
cgaNgon_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
