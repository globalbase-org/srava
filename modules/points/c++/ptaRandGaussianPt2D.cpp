/*
 * ptaRandGaussianPt2D — rand_gaussian(center, sigma, n, seed) の **軸 2 本** の行
 *   (`rand_gaussian#pt2d`) の計算本体 (#3576 ・ 2026-09-22)。
 *   中心 c を **各軸の標準偏差 sigma** の等方ガウスで囲んだ点を n 個作り、pt-cloud2d で返す。
 *
 * ★★★ **シードは省略できない** — 同じ引数なら必ず同じ点群になる (ptaRand.cpp 冒頭)。
 * ★★ 行は **第 1 引数の要素数** (2 要素の配列) が選ぶ (#3572 の rand と同じ仕掛け)。
 * ★ sigma は **各軸の**標準偏差。⚠ 「距離の標準偏差」ではない (pt/c++/ptRandom.h)。
 *
 * 中身は共通 (pt_cloud_random_gauss)。ここは次元と名前を渡すだけ。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"pt/c++/ptCloud.h"
#include	"pt/c++/ptRandom.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ptaRandGaussianPt2D_.h"

CLASS_TINYSTATE(pt/c++/ptaRandGaussianPt2D,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ptaRandGaussianPt2D_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<ptCloud>	cloud;
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
class ptCloud;
TS_END_INTERFACE

#endif


ptaRandGaussianPt2D_::ptaRandGaussianPt2D_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
ptaRandGaussianPt2D_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	if ( na < 4 ) {
		result = pta_err(thNEW(stdString,("rand_gaussian: needs (center, sigma, n, seed)")));
		return;
	}
	char err[256];
	err[0] = '\0';
	cloud = pt_cloud_random_gauss((*args)[0], (*args)[1], (*args)[2], (*args)[3],
	                        2, "rand_gaussian", err, (int)sizeof err);
	if ( ! cloud.is_notNull() )
		result = pta_err(thNEW(stdString,(err)));
}

/* この演算の結果。エラー時は compute() が result にエラー値を残して本体未設定で return するので
 * result 優先 (ptaPoints2D と同じ)。 */
sPtr<pigData>
ptaRandGaussianPt2D_::get_result()
{
	return ( result != thNULL ) ? result : sPtr<pigData>::d_cast(cloud);
}
