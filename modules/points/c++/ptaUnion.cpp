/*
 * ptaUnion — union(points, points) の計算本体 (#3578 ・ 2026-09-22)。点群版。
 *
 * ★★ 点群の和は **単純に混ぜる**だけ。重複は落とさない ⇒ 不変条件
 *       nverts(union(a,b)) == nverts(a) + nverts(b)
 *   メッシュの union (ブール和・重なりを解消する) とは別の計算だが、「和」という約束は同じ。
 *   ⚠ 落とすべき重複の定義 (座標が厳密に一致? 距離 eps 以内?) は点群には無い — 与えるなら
 *     それは別の op (間引き) であって和ではない。
 *
 * ★ 並びは **a のあと b**。索引 (vert) は「格納順 = 入力の順」と定義済み (#3527) なので、
 *   union(a,b) と union(b,a) は *同じ点集合で違う点群* になる。
 *   ⚠⚠ だから OPS 行に **可換の印を立てていない** (pttsAgent.cpp)。
 *
 * 中身は共通 (pt_cloud_union)。ここは引数を読むだけ。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"pt/c++/ptCloud.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ptaUnion_.h"

CLASS_TINYSTATE(pt/c++/ptaUnion,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ptaUnion_(
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


ptaUnion_::ptaUnion_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
ptaUnion_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	if ( na < 2 ) {
		result = pta_err(thNEW(stdString,("union: needs two point clouds")));
		return;
	}
	sPtr<ptCloud> a = sPtr<ptCloud>::d_cast((*args)[0]);
	sPtr<ptCloud> b = sPtr<ptCloud>::d_cast((*args)[1]);
	char err[256];
	err[0] = '\0';
	cloud = pt_cloud_union(a, b, "union", err, (int)sizeof err);
	if ( ! cloud.is_notNull() )
		result = pta_err(thNEW(stdString,(err)));
}

/* この演算の結果。エラー時は compute() が result にエラー値を残して本体未設定で return するので
 * result 優先 (ptaPoints3D と同じ)。 */
sPtr<pigData>
ptaUnion_::get_result()
{
	return ( result != thNULL ) ? result : sPtr<pigData>::d_cast(cloud);
}
