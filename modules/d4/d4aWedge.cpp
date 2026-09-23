/*
 * d4aWedge — ★★ **わざと居座る in-proc の実行体** (#3503 のテスト用フック)。
 *
 * d4_wedge(sec) は sec 秒ぶん回るが、**中断要求を一切見ない**。demo_spin (#3417) の
 * 対極で、あちらが「中断できることの最小デモ」なのに対しこちらは
 * *「中断できないモジュールが in-proc に居るとどうなるか」の最小デモ*。
 *
 * ★ なぜ要るか — #3503 の in-proc panic は、これが無いと **end-to-end で検証できない**。
 *   実カーネルはどれも中断に応じるか (occt / manifold / openvdb / pipe_proximity)、
 *   process 専用か (cgal / nef / geogram / cherchi) のどちらかで、
 *   「in-proc なのに居座る」状況を意図的には作れない。
 *
 * ⚠ d4 を選んだのは **exec_default が既に EXEC_THREAD (in-proc)** のテスト専用モジュール
 *   だから。demo は「EXEC_PROCESS 専用」であること自体が設計の一部 (⑤ の境界規約回避) なので
 *   足せない。
 *
 * ⚠⚠ **このファイルの真似をしてはいけない**。is_destroyed() も brk_ も見ないのは
 *   *不具合の再現が目的*であって、書き方の手本ではない。実モジュールの作法は
 *   demo_compute.cpp (is_destroyed) と pigBreak.h (brk_) を見ること。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"d4/c++/d4Mesh.h"
#include	"ts2/c++/stdString.h"
#include	<time.h>
#include	"_ts2/c++/d4aWedge_.h"


CLASS_TINYSTATE(d4/c++/d4aWedge,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	d4aWedge_(
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


d4aWedge_::d4aWedge_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

/* 基底 ptsCalcBody の TS_THREAD(ACT_START) から呼ばれる (専用 thread)。
 * ★ **is_destroyed() も brk_ も見ない**。それがこの op の役目 (冒頭の但し書き参照)。 */
void
d4aWedge_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	double sec = ( na > 0 && (*args)[0] != thNULL ) ? (*args)[0]->get_flt() : 0.0;
	if ( sec < 0.0 ) sec = 0.0;
	/* ⚠ 上限を置く。テストが壊れても機械を占有し続けないため
	 *   (panic が効かなければ planner は永久に待つ = CI が止まる)。 */
	if ( sec > 120.0 ) sec = 120.0;

	const double SLICE = 0.02;
	const long slices = (long)(sec / SLICE + 0.5);
	for ( long i = 0 ; i < slices ; ++i ) {
		struct timespec ts;
		ts.tv_sec  = 0;
		ts.tv_nsec = (long)(SLICE * 1e9);
		::nanosleep(&ts, 0);
	}
	result = thNEW(pigDataFloat,((double)sec));
}
