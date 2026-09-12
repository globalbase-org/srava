/*
 * cgaPyramid — 正 n 角錐生成の計算本体(ptsCalcBody 派生)。args=[n, height, radius](INLINE)。
 * ★ #3474: CGAL::make_pyramid をやめ、**common/solids.h の共通生成器**へ寄せた。
 *   pyramid が cgal にしか無いあいだ `r ||| pyramid(...)` は cgal でしか実行できず、
 *   基本立体 1 個の欠落が式全体のカーネル選択を裏返していた。他カーネルへ配るにあたり、
 *   geodesic.h (sphere) と同じく **共通生成器 1 本**にして頂点・面の並びを一致させる。
 * ★ 位相は従来どおり (底面は中心頂点からの扇なので正 4 角錐は 6 頂点 8 面)。底面 z=0・
 *   頂点 z=h も従来どおり。⚠ 底面の頂点は ngon / prism と同じ「角度 2πk/n・+X 始点」に
 *   なるので、CGAL::make_pyramid が置いていた位置とは **回転位相が違いうる**
 *   (体積・面数は不変。prism(n,h,r) ≡ extrude(ngon(n,r),h) と揃う方が一貫する)。
 * ⚠ n < 3 は従来「黙って 3 に丸め」ていたが、明示エラーにした (他カーネルと同じ判定)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"cg/c++/cgMesh.h"
#include	"cg/c++/cgTriSink.h"
#include	"cg/c++/ptscgWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	"common/solids.h"
#include	"_ts2/c++/cgaPyramid_.h"

CLASS_TINYSTATE(cg/c++/cgaPyramid,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	cgaPyramid_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<cgMesh3D>	mesh;
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
class cgMesh;
TS_END_INTERFACE

#endif


cgaPyramid_::cgaPyramid_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
cgaPyramid_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	int    n = ( na > 0 ) ? (int)(*args)[0]->get_int() : 3;
	double h = ( na > 1 ) ? (*args)[1]->get_flt() : 1.0;
	double r = ( na > 2 ) ? (*args)[2]->get_flt() : 1.0;
	if ( !(n >= 3) ) { result = cga_err(thNEW(stdString,("pyramid: n must be >= 3"))); return; }
	if ( !(h > 0) ) { result = cga_err(thNEW(stdString,("pyramid: height must be > 0"))); return; }
	if ( !(r > 0) ) { result = cga_err(thNEW(stdString,("pyramid: radius must be > 0"))); return; }

	mesh = thNEW(cgMesh3D,());
	cgTriSink sink(mesh->mesh());
	srava_geo::make_pyramid(n, h, r, sink);
}

/* この演算の結果 (#3406, 2026-07-30 メモ: get_body/get_result を統一)。エラー時は
 * compute() が result にエラー値を残して mesh を未設定のまま return するので result 優先。
 * 成功時は result=thNULL のままmeshを返す。保存(Writer 起動)は agent が出力 pigDataCache
 * の set_body 経由で行う。 */
sPtr<pigData>
cgaPyramid_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
