/*
 * ptaPoints2D — points2d([[x,y],...]) の計算本体 (#3528)。値配列から 2D 点群を作る leaf op。
 *
 * ★★ 2D と 3D で **op 名が分かれている**理由: op は実行時に出力型を変えられない
 *   (sig_dispatch が sig 行の out をそのまま返し、planner が走る前に決める)。points は leaf で
 *   引数が inline 値 = sig の照合に参加しない (arg_type_set が "value" を除外) ので、planner には
 *   2D か 3D かを判別する材料が無い。⚠ 1 つの op に 2 行 ("->pt-cloud2d;->pt-cloud3d") と書いても、
 *   同優先度では `if (pr > bestPrio)` が狭義なので **先頭が黙って勝つ**。
 *   ⇒ 名前を分けるのが唯一の形 (rect/box ・ empty2d/empty3d と同じ流儀)。
 *
 * 中身は共通 (pt_cloud_from_value)。ここは次元を渡すだけ。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"pt/c++/ptCloud.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ptaPoints2D_.h"

CLASS_TINYSTATE(pt/c++/ptaPoints2D,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ptaPoints2D_(
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


ptaPoints2D_::ptaPoints2D_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
ptaPoints2D_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	char err[256];
	err[0] = '\0';
	cloud = pt_cloud_from_value(( na > 0 ) ? (*args)[0] : sPtr<pigData>(),
	                            2, err, (int)sizeof err);
	if ( ! cloud.is_notNull() )
		result = pta_err(thNEW(stdString,(err)));
}

/* この演算の結果。エラー時は compute() が result にエラー値を残して本体未設定で return するので
 * result 優先。保存 (Writer 起動) は agent が出力 pigDataCache の set_body 経由で行う。 */
sPtr<pigData>
ptaPoints2D_::get_result()
{
	return ( result != thNULL ) ? result : sPtr<pigData>::d_cast(cloud);
}
