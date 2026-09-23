/*
 * cgaCircle — circle(r) の計算本体(ptsCalcBody 派生)= 円の多角形近似(2D, 半径 r, 既定 32 分割)。
 * 厳密円(円弧)は持たず正多角形で近似(回転・球と同じ近似方針)。細かさが要れば ngon(n, r) を使う。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"cg/c++/cgMesh.h"
#include	<vector>
#include	"cg/c++/ptscgWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	"common/segs.h"   /* ★ #3530: segs / n の共通検査 */
#include	"_ts2/c++/cgaCircle_.h"

/* cgaNgon.cpp 定義の共有ヘルパ(正 n 角形 CCW を返す)。 */
std::vector<double> cga_regular_polygon(int n, double r);   /* ★ #3545: 素の (x,y) 列 */

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
	int    segs_in = ( na > 1 ) ? (int)(*args)[1]->get_int() : 0;   /* 0 = 未指定 */
	int    segs = 0;
	/* ★★ #3530: segs の意味を全 op / 全カーネルで 1 本に揃えた (src/h/common/segs.h)。
	 *   0 or 省略 = 既定値 / 1,2 / 負 = 明示エラー / 3 以上 = その値。 */
	if ( srava_geo::check_segs(segs_in, 32, &segs) != srava_geo::SEGS_OK ) {
		result = cga_err(thNEW(stdString,(srava_geo::segs_error("circle").c_str()))); return;
	}
	/* ★ #3516 続き: 他の実装 (occt) が元から持っていた検査を揃えた。
	 *   ⚠ 無いと circle(0) が面積 0 を返し、circle(-1) は cgal=3.12 (≈π) /
	 *   manifold=0 と **カーネルごとに違う値**を黙って返していた。 */
	if ( !(r > 0) ) { result = cga_err(thNEW(stdString,("circle: radius must be > 0"))); return; }
	mesh = thNEW(cgMesh2D,());
	std::vector<double> xy = cga_regular_polygon(segs, r);
	mesh->add_region_ring(&xy[0], segs);
}

/* この演算の結果 (#3406, 2026-07-30 メモ: get_body/get_result を統一)。エラー時は
 * compute() が result にエラー値を残して mesh を未設定のまま return するので result 優先。
 * 成功時は result=thNULL のままmeshを返す。保存(Writer 起動)は agent が出力 pigDataCache
 * の set_body 経由で行う。 */
sPtr<pigData>
cgaCircle_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
