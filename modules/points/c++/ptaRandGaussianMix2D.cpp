/*
 * ptaRandGaussianMix2D — rand_gaussian(pt-cloud2d, sigma, n, seed) の計算本体 (#3577 ・ 2026-09-22)。
 *   入力点群の各点を中心とする等方ガウスを **均等に重ね合わせた分布**から n 点を引く。
 *
 * ★★★ 「各入力点に (中心版) を施したもの」とは **違う**。重ね合わせた分布からの標本なので、
 *   K が大きく n が小さいと **点を 1 つも貰わない中心**が出る (それが正しい)。
 *   出力の点数は **n** で、入力の K とは無関係。
 *
 * ★★ この行は **sig が選ぶ** (第 1 引数が幾何なので insets=[pt-cloud2d])。
 *   ⚠ 中心版の 3 行はマッチ関数が選ぶ — **同じ op 名で選ばれ方が 2 通り**ある。
 *     pt_match_rand_dim は「キャッシュ引数なら 1 を返して sig に譲る」と書いてある。
 *
 * 中身は共通 (pt_cloud_random_gauss_mix)。ここは名前を渡すだけ。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"pt/c++/ptCloud.h"
#include	"pt/c++/ptRandom.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ptaRandGaussianMix2D_.h"

CLASS_TINYSTATE(pt/c++/ptaRandGaussianMix2D,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ptaRandGaussianMix2D_(
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


ptaRandGaussianMix2D_::ptaRandGaussianMix2D_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
ptaRandGaussianMix2D_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	if ( na < 4 ) {
		result = pta_err(thNEW(stdString,("rand_gaussian: needs (points, sigma, n, seed)")));
		return;
	}
	sPtr<ptCloud> in = sPtr<ptCloud>::d_cast((*args)[0]);
	char err[256];
	err[0] = '\0';
	cloud = pt_cloud_random_gauss_mix(in, (*args)[1], (*args)[2], (*args)[3],
	                                  "rand_gaussian", err, (int)sizeof err);
	if ( ! cloud.is_notNull() )
		result = pta_err(thNEW(stdString,(err)));
}

/* この演算の結果。エラー時は compute() が result にエラー値を残して本体未設定で return するので
 * result 優先 (ptaPoints2D と同じ)。 */
sPtr<pigData>
ptaRandGaussianMix2D_::get_result()
{
	return ( result != thNULL ) ? result : sPtr<pigData>::d_cast(cloud);
}
