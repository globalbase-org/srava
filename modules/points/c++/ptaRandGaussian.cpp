/*
 * ptaRandGaussian — rand_gaussian(center, sigma, n, seed) の **軸 1 本** の行の計算本体
 *   (#3576 ・ 2026-09-22)。平均 center ・ 標準偏差 sigma の正規乱数を n 個・配列で返す。
 *
 * ★★★ **シードは省略できない** — 同じ引数なら必ず同じ結果 (ptaRand.cpp 冒頭と同じ約束)。
 * ★★ この行は **「2 でも 3 でもない」を全部引き取る** (#3572 の rand と同じ形)。
 *   ⇒ 要素数 1 や 4 の配列が来るのは書き間違いなので、**形の話として名指しで**断る。
 * ⚠ 値 (AK_INLINE) を返す op なので木の上でテキスト化されて運ばれる。10^6 個をここから
 *   返すのは形として正しくない (点が欲しいなら中心を配列で書く = #pt2d / #pt3d の行)。
 * ⚠ 出る値は **常に浮動小数点**。一様版の rand と違い「整数か浮動か」の規則は無い
 *   (正規分布は連続分布なので、整数を返す意味が無い)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"pt/c++/ptCloud.h"     /* pta_err (モジュール名つきのエラー) */
#include	"pt/c++/ptRandom.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ptaRandGaussian_.h"

CLASS_TINYSTATE(pt/c++/ptaRandGaussian,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ptaRandGaussian_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

protected:
	virtual void	compute();
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
TS_END_INTERFACE

#endif


ptaRandGaussian_::ptaRandGaussian_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
ptaRandGaussian_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	if ( na < 4 ) {
		result = pta_err(thNEW(stdString,("rand_gaussian: needs (center, sigma, n, seed)")));
		return;
	}
	/* ★★ #3576: この行は **軸 1 本** だが、マッチ関数が「2 でも 3 でもない」を全部ここへ
	 *   寄せている (#3572 の rand と同じ)。⇒ 要素数 1 や 4 の配列が来るのは
	 *   *利用者の書き間違い* で、それを **形の話として名指しで**言うのがこの分岐。
	 *   ⚠ ここで引き取らずに行を外すと routing の「どの候補も受けない」に落ち、
	 *     形について何も言わない文言になる (pttsAgent.cpp の pt_match_rand_dim 参照)。 */
	{
		sPtr<pigDataArray> ca = (*args)[0].is_notNull() ? (*args)[0]->obt_array()
		                                                : sPtr<pigDataArray>(thNULL);
		if ( ca.is_notNull() ) {
			char m[256];
			::snprintf(m, sizeof m,
			    "rand_gaussian: the center must be a number (one axis) or an array of"
			    " 2 or 3 numbers (one per axis), but it is an array of %d", ca->length());
			result = pta_err(thNEW(stdString,(m)));
			return;
		}
	}
	ptGaussSpec sp;
	char err[256];
	err[0] = '\0';
	if ( ! pt_rand_gauss_spec((*args)[0], (*args)[1], (*args)[2], (*args)[3],
	                          1, "rand_gaussian", sp, err, sizeof err) ) {
		result = pta_err(thNEW(stdString,(err)));
		return;
	}

	/* ⚠ 返すのは **常に pigDataFloat**。一様版 rand の「a と b が両方整数なら整数」の規則は
	 *   ここには無い — 正規分布は連続分布なので、整数へ丸める意味が無い。 */
	ptRandom rng(sp.seed);
	sPtr<pigDataArray> arr = thNEW(pigDataArray,());
	for ( INTEGER64 i = 0 ; i < sp.n ; ++i )
		arr->push(thNEW(pigDataFloat,( sp.c[0] + sp.sigma * rng.next_gauss() )));
	result = arr;   /* n == 0 は空配列 */
}
