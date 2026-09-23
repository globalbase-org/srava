/*
 * guaPtIntersection — 点群を mesh で切る — mode で 3 つの片から選ぶ (#3579)。
 *
  * intersection(点群, mesh, mode) → 点群
 *     mode  0 … 境界ちょうど / -1 … 内側 (開) / +1 … 外側
 *   ★ mode を省くと **-1 (内側)**。⚠ パーサの糖衣 (3 要素配列) が入ったらそちらが先に
 *     展開するので、この既定に落ちるのは mode を省いて糖衣を通らない経路だけ。 *
 * ★★ 中身は **guPtSplit.h に 1 本**。intersection と difference が同じものを呼ぶ
 *   (写すと片方だけ直して黙ってずれる)。判定器そのものは共通ヘッダ
 *   (meshprops.h の巻き数 / ringprops.h の point-in-polygon) — 書き直さないこと。
 * ⚠ **可換ではない**。型が非対称なので可換フラグを立てると fold 分解が引数を組み替えて
 *   壊れる ⇒ OPS 行は commutative=0 ・ fold 形にしない。intersection(mesh, 点群) の順は受けない。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"gu/c++/guGeom.h"
#include	"gu/c++/guPtSplit.h"
#include	"pt/c++/ptCloud.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/guaPtIntersection_.h"
#include	<stdio.h>

CLASS_TINYSTATE(gu/c++/guaPtIntersection,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	guaPtIntersection_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	/* ⚠ get_result は **public 側**に置く (protected だと codegen が転送を作らず、
	 *   値は作れているのに format 'TEXT' で保存される)。 */
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
class guGeom;
class ptCloud;
TS_END_INTERFACE

#endif

guaPtIntersection_::guaPtIntersection_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
guaPtIntersection_::compute()
{
	const int na = ( args != 0 ) ? args->length() : 0;
	sPtr<ptCloud> in   = ( na > 0 ) ? sPtr<ptCloud>::d_cast((*args)[0]) : sPtr<ptCloud>();
	sPtr<guGeom>  geom = ( na > 1 ) ? sPtr<guGeom>::d_cast((*args)[1])  : sPtr<guGeom>();
	const int mode = ( na > 2 ) ? (int)(*args)[2]->get_int() : -1;   /* ★ 既定は内側 */

	const char *why = 0;
	sPtr<ptCloud> out = gu_pt_split(in, geom, mode, &why);
	if ( ! out.is_notNull() ) {
		char b[352];
		::snprintf(b, sizeof b, "intersection: %s", why ? why : "could not split the point cloud");
		result = gua_err(thNEW(stdString,(b)));
		return;
	}
	cloud = out;
}

/* エラー時は compute() が result にエラーを残すので result 優先。 */
sPtr<pigData>
guaPtIntersection_::get_result()
{
	return ( result != thNULL ) ? result : sPtr<pigData>::d_cast(cloud);
}
