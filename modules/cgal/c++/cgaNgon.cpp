/*
 * cgaNgon — ngon(n, r) の計算本体(ptsCalcBody 派生)= 正 n 角形(2D, 外接半径 r, 原点中心, CCW)。
 * 頂点は角度 2πk/n の cos/sin(double)を K::FT に格納(回転と同じく角度近似・座標は EPECK 有理数)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"cg/c++/cgMesh.h"
#include	<vector>
#include	"cg/c++/ptscgWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	"common/segs.h"   /* ★ #3530: segs / n の共通検査 */
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

/* 正 n 角形(CCW)の座標を返す共有ヘルパ(circle も使う)。
 * ★ #3545: 返りを **素の (x,y) 列**にした (旧: CGAL の Polygon_2)。
 *   CGAL へ積むのは cgMesh2D::add_region_ring = 幾何 lib 側。 */
std::vector<double>
cga_regular_polygon(int n, double r)
{
	if ( n < 3 ) n = 3;
	std::vector<double> xy;
	xy.reserve((size_t)n * 2);
	for ( int k = 0 ; k < n ; ++k ) {
		double a = 2.0 * M_PI * (double)k / (double)n;   /* CCW */
		xy.push_back(r * ::cos(a));
		xy.push_back(r * ::sin(a));
	}
	return xy;
}

void
cgaNgon_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	int    n = ( na > 0 ) ? (int)(*args)[0]->get_int() : 3;
	double r = ( na > 1 ) ? (*args)[1]->get_flt() : 1.0;
	/* ★ #3516 続き: 他の実装 (occt) が元から持っていた検査を揃えた。
	 *   ⚠ 無いと ngon(2,1) が「2 角形」として面積 1.299 を返し、ngon(6,-1) は
	 *   ngon(6,1) と同じ面積を返していた (符号が黙って落ちる)。 */
	if ( srava_geo::check_sides(n, &n) != srava_geo::SEGS_OK ) {   /* ★ #3530: n は形そのもの = 既定値なし */
		result = cga_err(thNEW(stdString,(srava_geo::sides_error("ngon").c_str()))); return; }
	if ( !(r > 0) )  { result = cga_err(thNEW(stdString,("ngon: radius must be > 0"))); return; }
	mesh = thNEW(cgMesh2D,());
	std::vector<double> xy = cga_regular_polygon(n, r);
	mesh->add_region_ring(&xy[0], n);
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
