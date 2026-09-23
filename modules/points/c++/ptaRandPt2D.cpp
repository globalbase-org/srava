/*
 * ptaRandPt2D — rand(a, b, n, seed) の **軸 2 本** の行 (`rand#pt2d`) の計算本体。
 *   (2026-09-21 新設 ・ 2026-09-22 に #3572 で rand へ統合。行は第 1 引数の要素数が選ぶ。)
 *   軸ごとの区間 [a[k], b[k]] にランダムな点を n 個作り、pt-cloud2d で返す。
 *
 * ★★★ **シードは省略できない** — 同じ引数なら必ず同じ点群になる (ptaRand.cpp 冒頭)。
 *
 * ★★ 2D と 3D で **op 名が分かれている**理由は points2d / points3d と同じ: op は実行時に
 *   出力型を変えられず、引数は inline 値なので sig の照合に参加しない。⇒ planner には
 *   2D か 3D かを判別する材料が無い (ptaPoints2D.cpp 冒頭に詳細)。
 *   ⚠ a / b の要素数で決まりそうに見えるが、**それは値の中身**であって型ではない。値で
 *     行を選ぶ仕掛け (AK_MATCH) は用意されているが、ここで使うと「要素を 1 つ書き間違えると
 *     黙って別の型になる」ので採らない — 次元は名前が言う。
 *
 * 中身は共通 (pt_cloud_random)。ここは次元と名前を渡すだけ。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"pt/c++/ptCloud.h"
#include	"pt/c++/ptRandom.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ptaRandPt2D_.h"

CLASS_TINYSTATE(pt/c++/ptaRandPt2D,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ptaRandPt2D_(
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


ptaRandPt2D_::ptaRandPt2D_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
ptaRandPt2D_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	if ( na < 4 ) {
		result = pta_err(thNEW(stdString,("rand: needs (a, b, n, seed)")));
		return;
	}
	char err[256];
	err[0] = '\0';
	cloud = pt_cloud_random((*args)[0], (*args)[1], (*args)[2], (*args)[3],
	                        2, "rand", err, (int)sizeof err);
	if ( ! cloud.is_notNull() )
		result = pta_err(thNEW(stdString,(err)));
}

/* この演算の結果。エラー時は compute() が result にエラー値を残して本体未設定で return するので
 * result 優先 (ptaPoints2D と同じ)。 */
sPtr<pigData>
ptaRandPt2D_::get_result()
{
	return ( result != thNULL ) ? result : sPtr<pigData>::d_cast(cloud);
}
