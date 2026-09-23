/*
 * ocaPtIntersection — intersection(点群, 形, ...) — 点群を B-rep で切る (#3581)。
 *
  * intersection(点群, 形, mode) -> 点群 ・ mode: 0=境界 / -1=内側 / 1=外側 *
 * ★★ 中身は **oc/c++/ocPtSplit.h に 1 本** (geomutils / openvdb と同じ形)。
 *   判定は BRepClass3d_SolidClassifier (3D) / 面までの距離 (2D)。
 * ★ 3D と 2D で **クラスが違う** (ocShape / ocFace2D) ので d_cast を 2 通り試す。
 *   ⚠ どちらでもなければ断る (黙って片方に倒さない)。
 * ⚠ **可換ではない**。型が非対称なので可換フラグを立てると fold 分解が引数を組み替えて壊れる。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"oc/c++/ocShape.h"
#include	"oc/c++/ocPtSplit.h"
#include	"pt/c++/ptCloud.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ocaPtIntersection_.h"
#include	<stdio.h>

CLASS_TINYSTATE(oc/c++/ocaPtIntersection,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ocaPtIntersection_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	/* ⚠ get_result は **public 側**に置く (protected だと codegen が転送を作らない)。 */
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
class ocShape;
class ocFace2D;
class ptCloud;
TS_END_INTERFACE

#endif

ocaPtIntersection_::ocaPtIntersection_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
ocaPtIntersection_::compute()
{
	const int na = ( args != 0 ) ? args->length() : 0;
	sPtr<ptCloud> in = ( na > 0 ) ? sPtr<ptCloud>::d_cast((*args)[0]) : sPtr<ptCloud>();
	const int mode = ( na > 2 ) ? (int)(*args)[2]->get_int() : -1;   /* ★ 既定は内側 */

	const char *why = 0;
	char wbuf[256];   /* ⚠ 理由の文字列はここへ (static 禁止) */
	sPtr<ptCloud> out;
	sPtr<pigData> g = ( na > 1 ) ? (*args)[1] : sPtr<pigData>();
	sPtr<ocShape>  s3 = sPtr<ocShape>::d_cast(g);
	if ( s3.is_notNull() )
		out = oc_pt_split(in, s3, mode, &why, wbuf, (int)sizeof wbuf, &brk_);
	else {
		sPtr<ocFace2D> s2 = sPtr<ocFace2D>::d_cast(g);
		if ( s2.is_notNull() )
			out = oc_pt_split(in, s2, mode, &why, wbuf, (int)sizeof wbuf, &brk_);
		else
			why = "the second argument must be a 3D shape or a 2D region";
	}
	if ( ! out.is_notNull() ) {
		char b[352];
		::snprintf(b, sizeof b, "intersection: %s", why ? why : "could not split the point cloud");
		result = oca_err(thNEW(stdString,(b)));
		return;
	}
	cloud = out;
}

/* エラー時は compute() が result にエラーを残すので result 優先。 */
sPtr<pigData>
ocaPtIntersection_::get_result()
{
	return ( result != thNULL ) ? result : sPtr<pigData>::d_cast(cloud);
}
