/*
 * tsTeardownRepro — 「多数の ts2System 子プロセスを一括 teardown すると、ワーカースレッドでの
 *   pipe FIN/abort 処理が main の app teardown と競合して SEGV する」最小再現(ptsObject 派生)。
 *
 * 背景(cgal-processor で判明): srava を多数 agent(=ts2System 子)が走っている最中に Ctrl+C すると、
 *   agent は全滅するが、その後の終了処理中に **ワーカースレッド(tsThread)** が
 *     ts2IOdescriptor FIN → fwIO::abort(fd) → stdQueue::del → ~sPtr<fwIOdata>
 *       → stdObject::relref → refMtx lock で SEGV(use-after-free)
 *   を起こす。他の worker は appMtxLock 待ちで滞留。= main の fwIO/app teardown と、worker が
 *   まだ処理中の pipe FIN/abort イベントが競合している。
 *
 * 本ファイルは pig/wire を介さず **素の ts2System だけ** で同じ状況を作る:
 *   ① ts2System で子(stdout に出力し続ける reader pipe 付き)を N 個起動する。
 *   ② 少し待って子が走り出した頃に、**全子を一括 destroy() + 即 FIN** する
 *      (= Ctrl+C で set_agentError → 全 agent 撤収 → planner FIN と同じ一括 teardown)。
 *   ③ 正常なら静かに終了。レースを踏むと teardown 中に SEGV(core dumped)。
 *
 * env パラメタ:
 *   TSTD_N      (既定 48)  : 起動する子プロセス数
 *   TSTD_DELAY  (既定 300) : 起動から一括 teardown までの待ち(ミリ秒)
 *   TSTD_CMD    (既定 "while :; do echo xxxx; sleep 0.01; done")
 *                          : 各子の中身(pipe を活性に保つため出力し続ける)
 *
 * 使い方: `for i in $(seq 50); do ./repro_teardown || echo "SEGV at $i"; done`
 *   多数回まわすと SEGV(exit 139)が出る(レースなので確率的)。
 */
#include	"pig/c++/ptsObject.h"
#include	"ts2/c++/ts2System.h"
#include	"ts2/c++/ts2IO.h"
#include	"ts2/c++/stdEvent.h"
#include	"ts2/c++/stdInterval.h"
#include	"_ts2/c++/tsTeardownRepro_.h"

#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>

CLASS_TINYSTATE(pig/c++/tsTeardownRepro,pig/c++/ptsObject)

#if 0

TS_BEGIN_IMPLEMENT

#define TSTD_MAXN 256

class TS_THISCLASS : public TS_BASECLASS {
public:
	tsTeardownRepro_(
		sPtr<tinyState> parent);

	sRptr<tinyState,tinyState>		parent;
protected:
	sPtr<ts2System>		sys[TSTD_MAXN];
	sPtr<ts2IO>		rfd[TSTD_MAXN];
	sPtr<ts2IO>		wfd[TSTD_MAXN];
	int			n;
	int			delayMs;
	const char *		cmd;
	int			ret;
private:
	TS_DEFARGS
};

TS_END_IMPLEMENT

TS_BEGIN_INTERFACE
#include	"ts2/c++/sRptr.h"
class tinyState;
class ts2System;
class ts2IO;
TS_END_INTERFACE

#endif


tsTeardownRepro_::tsTeardownRepro_(TS_ARGS0)
        : ptsObject_(parent),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
    n       = ( ::getenv("TSTD_N")     != 0 ) ? ::atoi(::getenv("TSTD_N"))     : 48;
    delayMs = ( ::getenv("TSTD_DELAY") != 0 ) ? ::atoi(::getenv("TSTD_DELAY")) : 300;
    cmd     = ( ::getenv("TSTD_CMD")   != 0 ) ? ::getenv("TSTD_CMD")
                                              : "while :; do echo xxxx; sleep 0.01; done";
    if ( n < 1 ) n = 1;
    if ( n > 256 ) n = 256;
    ret = 0;
}


/*******************************************
	STATE MACHINE
********************************************/

TS_STATE(INI_ptsObject_START)
{
	return rDO|ACT_tsTeardownRepro_SPAWN;
}

TS_STATE(ACT_tsTeardownRepro_SPAWN)
{
	::printf("[repro] spawning %d children (cmd via sh -c), then mass-teardown after %dms\n",
	         n, delayMs);
	::fflush(stdout);
	for ( int i = 0 ; i < n ; ++i ) {
		ret = 0;
		sys[i] = thNEW(ts2System,(ifThis, &ret, cmd, &rfd[i], (sPtr<ts2IO>*)0, &wfd[i], 0));
		if ( ret < 0 )
			::printf("[repro] child %d launch failed\n", i);
	}
	/* 子が走り出す頃まで待ってから一括撤収する(レース窓を作る)。 */
	stdInterval::wait(ifThis, delayMs * 1000, TSE_TIMER);
	return ACT_tsTeardownRepro_WAIT;   /* タイマ待ち(rDO なし) */
}

TS_STATE(ACT_tsTeardownRepro_WAIT)
{
	if ( ev->type == TSE_TIMER )
		return rDO|ACT_tsTeardownRepro_TEARDOWN;
	return 0;
}

TS_STATE(ACT_tsTeardownRepro_TEARDOWN)
{
	::printf("[repro] mass teardown: destroy %d children + FIN now\n", n);
	::fflush(stdout);
	/* 全子を一括 destroy(SIGTERM。Ctrl+C 時の set_agentError→全 agent 撤収と同じ)。
	 * その直後に FIN して app teardown へ → worker の pipe FIN/abort 処理と競合させる。 */
	for ( int i = 0 ; i < n ; ++i )
		if ( sys[i] != thNULL )
			sys[i]->destroy();
	return rDO|FIN_START;
}

TS_STATE(FIN_START)
{
	return rDO|FIN_ptsObject_START;
}
