/*
 * tsSysRepro — ts2System SIGCHLD teardown バグの最小再現(ptsObject 派生)。
 *
 * 現象: 1 プロセスで ts2System を 2 回以上 launch→destroy すると、2 回目以降の
 *   teardown(destroy → ACT_FINISH が SIGCHLD reap 用に tsSignal を生成)で SEGV。
 *   tsSignalCore は INI で signal_list(生ポインタ単方向リスト)に ins_signal するが、
 *   解放時に del_signal されないため、最後の参照が消えて core が free されると
 *   signal_list に dangling が残り、次の tsSignal INI の search_signal が deref して落ちる。
 *
 * 再現手順(本ファイル): pipe も pigfAgent も介さず、単に
 *   「ts2System(/bin/true) を作って destroy()」を待ち時間をはさんで N 回繰り返すだけ。
 *   各 round 間に sleep を入れて前の teardown を完全に終わらせ(= core が free され dangling 化)、
 *   次の round の destroy で search_signal が dangling を踏むようにする。
 *
 * 期待: round #1 は素通り、#2(または #3)の destroy で SEGV。
 *   修正後は最後まで生き残り "[repro] SURVIVED N teardowns" を表示して終了する。
 *
 * パラメタは env で変えられる:
 *   TSREPRO_ROUNDS (既定 4)         : 繰り返し回数
 *   TSREPRO_GAP_MS (既定 300)       : round 間の待ち(ms)
 *   TSREPRO_CMD    (既定 /bin/true) : 起動する子コマンド
 */
#include	"pig/c++/ptsObject.h"
#include	"ts2/c++/ts2System.h"
#include	"ts2/c++/ts2IO.h"
#include	"ts2/c++/stdInterval.h"
#include	"ts2/c++/stdEvent.h"
#include	"_ts2/c++/tsSysRepro_.h"

#include	<stdio.h>
#include	<stdlib.h>

CLASS_TINYSTATE(pig/c++/tsSysRepro,pig/c++/ptsObject)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	tsSysRepro_(
		sPtr<tinyState> parent);

	sRptr<tinyState,tinyState>		parent;
protected:
	sPtr<ts2System>		sys;
	sPtr<ts2IO>		rfd;
	sPtr<ts2IO>		wfd;
	int			ret;
	int			step;
	int			rounds;
	int			gap_us;
	const char *		cmd;
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


tsSysRepro_::tsSysRepro_(TS_ARGS0)
        : ptsObject_(parent),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
    ret  = 0;
    step = 0;
    const char *r = ::getenv("TSREPRO_ROUNDS");
    const char *g = ::getenv("TSREPRO_GAP_MS");
    cmd = ::getenv("TSREPRO_CMD");
    rounds = r ? ::atoi(r) : 4;
    gap_us = (g ? ::atoi(g) : 300) * 1000;
    if ( cmd == 0 ) cmd = "/bin/true";
}


/*******************************************
	STATE MACHINE
********************************************/

TS_STATE(INI_ptsObject_START)
{
	step = 0;
	return rDO|ACT_tsSysRepro_SPAWN;
}

TS_STATE(ACT_tsSysRepro_SPAWN)
{
	step++;
	::printf("[repro] teardown #%d: ts2System(%s) launch + destroy\n", step, cmd);
	::fflush(stdout);

	ret = 0;
	sys = thNEW(ts2System,(ifThis, &ret, cmd, &rfd, (sPtr<ts2IO>*)0, &wfd, 0));
	sys->destroy();
	/* 自前の参照を落とす → teardown 完了後に ts2System(と最後の参照だった tsSignalCore)が
	 * free され、signal_list に dangling が残る。これが次 round の SEGV のタネ。 */
	sys = thNULL;
	rfd = thNULL;
	wfd = thNULL;

	stdInterval::wait(ifThis, (INTEGER64)gap_us, TSE_TIMER);
	return rDO|ACT_tsSysRepro_WAIT;
}

TS_STATE(ACT_tsSysRepro_WAIT)
{
	if ( ev->type != TSE_TIMER )
		return 0;
	if ( step >= rounds ) {
		::printf("[repro] SURVIVED %d teardowns (no crash) — bug appears fixed\n", step);
		::fflush(stdout);
		return rDO|FIN_START;
	}
	return rDO|ACT_tsSysRepro_SPAWN;
}

TS_STATE(FIN_START)
{
	return rDO|FIN_ptsObject_START;
}
