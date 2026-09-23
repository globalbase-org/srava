/*
 * cgaHull — hull(a[,b,…]) — 与えた形すべての**凸包**(#3511)。cgal 版。
 * 本体は cg_hull_from_args (cgHull.cpp) にあり、3D/2D の振り分けもそちらが持つ。
 *
 * ★ **情報を落とす op** — 入力は「点の集合」としてしか見られないので、穴も凹みも消える。
 * ★ hull(hull(a,b),c) = hull(a,b,c) なので sig は fold 形 (木に分解してよい)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"cg/c++/cgMesh.h"
#include	"cg/c++/cgaBoolError.h"
#include	"cg/c++/ptscgWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	<stdio.h>
#include	"_ts2/c++/cgaHull_.h"

CLASS_TINYSTATE(cg/c++/cgaHull,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	cgaHull_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<cgMesh>	mesh;     /* compute() の union 結果(get_result が agent へ返す) */
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


cgaHull_::cgaHull_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

/* args[0], args[1] の cgMesh(reader が decode 済)を corefinement union → 結果を cgMesh に。
 * シリアライズは writer 側。重い計算は ACT_START(スレッド)。引き渡しは get_result()(#3406 2026-07-30: get_body 統合)。 */
void
cgaHull_::compute()
{
	/* ★ #3511: 1 個以上を受け、全頂点を 1 つの点集合にして 1 回で凸包を取る。 */
	const char *msg = 0;
	mesh = cg_hull_from_args(args, &msg);
	if ( ! mesh.is_notNull() ) {
		char b[256];
		::snprintf(b, sizeof b, "hull: %s", msg ? msg : "convex hull failed");
		result = cga_err(thNEW(stdString,(b)));
	}
}

/* この演算の結果 (#3406, 2026-07-30 メモ: get_body/get_result を統一)。エラー時は
 * compute() が result にエラー値を残して mesh を未設定のまま return するので result 優先。
 * 成功時は result=thNULL のままmeshを返す。保存(Writer 起動)は agent が出力 pigDataCache
 * の set_body 経由で行う。 */
sPtr<pigData>
cgaHull_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
