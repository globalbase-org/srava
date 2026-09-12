/*
 * demaCompute — demo モジュールの計算本体 (ptsCalcBody 派生・値返し)。ppaCompute のミラー。
 *
 * ★★ #3417 (2026-09-06): **なぜ calc を別オブジェクトにするのか**
 *
 *   旧実装は dematsAgent の状態関数の中で demo_compute() を同期に呼んでいた
 *   (「EXEC_PROCESS 専用: 同期 compute でよい (ptsCalcBody 不要)」)。速い value op しか
 *   無い前提では正しかったが、**中断できる op を足した瞬間に破綻する**:
 *
 *     ・同期のままだと計算中イベントループが止まり、destroy も wire の EOF も受け取れない
 *     ・状態関数を TS_THREAD にするだけでは足りない。中断要求 (destroy) を撃つ相手が
 *       **計算しているオブジェクト自身**になり、要求を受けて動く者が居なくなる
 *
 *   正しい形は「実行体 (待ち状態でイベントを受け続ける) + calc (専用 thread で計算する)」の
 *   2 オブジェクトで、これは ptsGenericAgent / ptsCalcBody が実カーネル 11 本すべてに
 *   提供している形そのもの。pipe_proximity も同じ形を手書きしている。demo だけが例外だった。
 *
 *   ⇒ 中断は「実行体が calc->destroy() を撃つ」→「calc の compute() が is_destroyed() を見る」
 *     で届く。実行体は待ち状態のままなので、撃つ側は止まらない。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"ts2/c++/stdString.h"
#include	"demo_compute.h"
#include	"_ts2/c++/demaCompute_.h"

CLASS_TINYSTATE(demo/c++/demaCompute,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	demaCompute_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target,
		sPtr<stdString> _op);

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


demaCompute_::demaCompute_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

/* 基底 ptsCalcBody の TS_THREAD(ACT_START) から呼ばれる (専用 thread)。
 * ★ demo_spin は sCallSection::key->caller() でこの calc を引き、is_destroyed() を見て中断する
 *   (demo_compute.cpp を参照)。つまり中断の問い合わせ先は **この calc オブジェクト**であり、
 *   実行体 (dematsAgent) ではない。 */
void
demaCompute_::compute()
{
	const char *opn = ( _op.is_notNull() ) ? _op->get_str() : "demo_add";
	result = demo_compute(opn, *args);
}
