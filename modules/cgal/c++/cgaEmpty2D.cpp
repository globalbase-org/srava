/*
 * cgaEmpty2D — empty2d() の計算本体(ptsCalcBody 派生)。引数なしの leaf op で、
 * **値としての空集合**(領域ゼロの cgMesh2D)を返す。
 *
 * ★`{}`(空ハッシュ)との違い(2026-08-15 ひさ設計):
 *   `{}` は **fold の中立元**で「演算子を適用しない印」として働く(`a ||| {} = a`・
 *   `intersection(a,{}) = a` もそのため)。empty2d() は **空集合そのもの**なので
 *   `intersection(a, empty2d())` は正しく空になる。中立元と空集合を型のある別物として分けた。
 *   section() が返す 3 要素配列の「空要素」もこちら(空メッシュ)を使う。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"cg/c++/cgMesh.h"
#include	"cg/c++/ptscgWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/cgaEmpty2D_.h"

CLASS_TINYSTATE(cg/c++/cgaEmpty2D,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	cgaEmpty2D_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<cgMesh>	mesh;
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


cgaEmpty2D_::cgaEmpty2D_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
cgaEmpty2D_::compute()
{
	mesh = thNEW(cgMesh2D,());   /* 空のまま = 空集合 */
}

sPtr<pigData>
cgaEmpty2D_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
