/*
 * ptaRand — rand(a, b, n, seed) の **軸 1 本** の行 (`rand`) の計算本体。[a,b] の擬似乱数を
 *   n 個・配列で返す。(2026-09-21 新設 ・ 2026-09-22 に #3572 で 3 行の 1 本になった。)
 *
 * ★★★ **シードは省略できない**。この op の約束は「同じ引数なら必ず同じ結果」で、シードは
 *   その引数のひとつだから (ひさ 2026-09-21)。省略可能にすると *引数から結果が決まらない*
 *   op になり、srava が結果をキャッシュする前提そのものが崩れる
 *   (キャッシュの鍵は引数のハッシュ ⇒ 2 回目は必ず HIT ⇒ 「毎回違う」は最初から実現しない)。
 *   ⇒ 記述子は nin=4 / nreq=0 (= 4 個すべて必須) で、arity 検査が 3 個の呼び出しを弾く。
 *
 * ★ 整数か浮動小数点かは **書かれた a と b の種別**で決まる (pt/c++/ptRandom.h)。
 *     rand(0, 10, 5, 1)     → 整数  [0,10] 閉    → [3,7,0,10,4]
 *     rand(0.0, 10, 5, 1)   → 浮動  [0,10) 半開  → [3.14…, …]
 *   ⚠ 返す要素の **型も** それに従う (整数なら pigDataInteger)。ここで float に揃えると
 *     `rand(0,10,…)[0] == 3` が偽になり、配列を添字で使う経路が静かに壊れる。
 *
 * ⚠ 値 (AK_INLINE) を返す op なので、結果は木の上でテキスト化されて運ばれる。10^6 個を
 *   ここから返すのは形として正しくない (点が欲しいなら第 1 引数を配列で書く = 行 rand#pt3d
 *   が pt-cloud3d を返す)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"pt/c++/ptCloud.h"     /* pta_err (モジュール名つきのエラー) */
#include	"pt/c++/ptRandom.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ptaRand_.h"

CLASS_TINYSTATE(pt/c++/ptaRand,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ptaRand_(
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


ptaRand_::ptaRand_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
ptaRand_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	if ( na < 4 ) {
		result = pta_err(thNEW(stdString,("rand: needs (a, b, n, seed)")));
		return;
	}
	/* ★★ #3572: この行 (`rand`) は **軸 1 本** だが、マッチ関数が「2 でも 3 でもない」を
	 *   全部ここへ寄せている。⇒ 要素数 1 や 4 の配列が来るのは *利用者の書き間違い*で、
	 *   それを **形の話として名指しで**言うのがこの分岐。
	 *   ⚠ ここで引き取らずに行を外すと routing の「どの候補も受けない」に落ち、
	 *     形について何も言わない文言になる (pttsAgent.cpp の pt_match_rand_dim 参照)。 */
	{
		sPtr<pigDataArray> aa = (*args)[0].is_notNull() ? (*args)[0]->obt_array()
		                                                : sPtr<pigDataArray>(thNULL);
		if ( aa.is_notNull() ) {
			char m[256];
			::snprintf(m, sizeof m,
			    "rand: a must be a number (one axis) or an array of 2 or 3 numbers"
			    " (one interval per axis), but it is an array of %d", aa->length());
			result = pta_err(thNEW(stdString,(m)));
			return;
		}
	}
	ptRandSpec sp;
	char err[256];
	err[0] = '\0';
	if ( ! pt_rand_spec((*args)[0], (*args)[1], (*args)[2], (*args)[3], 1, "rand", sp, err, sizeof err) ) {
		result = pta_err(thNEW(stdString,(err)));
		return;
	}

	ptRandom rng(sp.seed);
	sPtr<pigDataArray> arr = thNEW(pigDataArray,());
	const ptRandAxis &ax = sp.ax[0];
	for ( INTEGER64 i = 0 ; i < sp.n ; ++i ) {
		if ( ax.is_int )
			arr->push(thNEW(pigDataInteger,(rng.next_int(ax.ia, ax.ib))));
		else
			arr->push(thNEW(pigDataFloat,(rng.next_flt(ax.fa, ax.fb))));
	}
	result = arr;   /* n == 0 は空配列。断らない — points3d([]) が空の点群を作れるのと同じ */
}
