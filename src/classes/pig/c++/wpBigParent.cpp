/*
 * wpBigParent — tinyState #3393(MinGW の pipe write livelock + shutdown teardown hang)の
 *   CGAL 非依存 最小再現/回帰テスト用ドライバ(ptsObject 派生)。
 *   【結果 2026-07-19】真因は tinyState 側で根治(livelock=7625336 / shutdown+refio UAF=89ee2e6)。
 *   MinGW full ctest 174/174。切り分けの全記録は docs/wirepipe_big_repro.md。以下の本文コメントは
 *   調査当時の記述(当初「大レコード送信 deadlock」と誤解した経緯を含む)。
 *
 *   (調査当時の説明)srava #4「性能崖」の MinGW 大レコード送信 stall を、srava/CGAL 非依存で
 *   **ptsWirePipe の親側だけ**で再現するドライバ。
 *
 * これは bigrepro(素の ts2System + ts2IO::write_c だけ)が MinGW で **再現しなかった**ことを
 * 受けての切り分け。素の ts2IO が 128KB を完走するのに実機 srava_bigtube が固まる差は、
 * srava のレコード framing 層 = **ptsWirePipe** にある、という仮説を確かめる。ここでは
 * プランナ(pigfAgent.cpp:451)と同じ配線で ptsWirePipe を使う:
 *
 *   ① ts2System で子(wp_child)を起動し rfd(子 stdout)/ wfd(子 stdin)を得る。
 *   ② wfd->set_divisible()(プランナと同じ)。
 *   ③ pipe = ptsWirePipe(self, rfd, wfd)。handshake(streamhdr 交換)完了 = TSE_ASSERT。
 *   ④ TSE_ASSERT で 1 レコード(type=C_OP, payload=WP_BYTES バイト)を pipe->write() で送信し wend()。
 *      → write_record は hdr(write_c) + payload(write_c) の 2 段。>64KB で MinGW named pipe が満杯
 *        になると payload の write_c が EAGAIN yield する。この resume が(子 stdout を読みつつ)
 *        正しく駆動されないと**停止**する。
 *   ⑤ 子は record を受け取ると "COUNT=<n>" レコードを返す(TSE_PACKET)。一致すれば OK。
 *   ⑥ 子の wend で pipe FIN(TSE_RETURN)→ 終了。
 *
 * env:
 *   WP_CHILD : wp_child(.exe) のパス(必須)
 *   WP_BYTES : payload バイト数(既定 131072 = 128KB。>64KB で MinGW 再現を狙う)
 *
 * 期待: MinGW 実機で WP_BYTES>~64KB のとき deadlock(OK が出ずハング)。POSIX は即完走。
 *   → bigrepro(素 ts2IO)は完走・これ(ptsWirePipe)は停止、なら犯人は ptsWirePipe 層と確定。
 */
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/ptsApplication.h"   /* ptsApp 値メンバの完全型(ptsObject.h から移動・#3406 4.2) */
#include	"pig/c++/pigwire.h"
#include	"pig/c++/ptsWirePipe.h"
#include	"pig/c++/ptsWirePacket.h"
#include	"ts2/c++/ts2System.h"
#include	"ts2/c++/ts2Parallel.h"   /* WP_SEND=par: 大レコード書込を pigfAgent と同じく worker から出す */
#include	"ts2/c++/ts2IO.h"
#include	"ts2/c++/stdEvent.h"
#include	"ts2/c++/sException.h"    /* WP_CATCHDBG: pipe->write の yield が sException で来ているか計測 */
#include	"_ts2/c++/wpBigParent_.h"

#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>

CLASS_TINYSTATE(pig/c++/wpBigParent,pig/c++/ptsObject)

/* main がアプリループ終了後に返す終了コード(スケジューラ内 ::exit は使わない)。 */
int wpBigParent_exitCode = 0;

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	wpBigParent_(
		sPtr<tinyState> parent);

	sRptr<tinyState,tinyState>		parent;
protected:
	sPtr<ts2System>		sys;
	sPtr<ts2IO>		rfd;
	sPtr<ts2IO>		wfd;
	sPtr<ptsWirePipe>	pipe;
	sPtr<ts2Parallel>	par;
	int			retPid;
	int			N;
	int			sent;
	int			got;
	int			parMode;
	int			sendIdx;
	int			catchDbg;
	long			throwCount;
	uint8_t *		payload;
	char			cmd[1200];
private:
	TS_DEFARGS
};

TS_END_IMPLEMENT

TS_BEGIN_INTERFACE
#include	"ts2/c++/sRptr.h"
class tinyState;
class ts2System;
class ts2IO;
class ptsWirePipe;
class ts2Parallel;
TS_END_INTERFACE

#endif


wpBigParent_::wpBigParent_(TS_ARGS0)
        : ptsObject_(parent),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
    retPid = -1; N = 0; sent = 0; got = -1; parMode = 0; sendIdx = 0; payload = 0;
    catchDbg = 0; throwCount = 0;
    const char *b = ::getenv("WP_BYTES");
    N = b ? ::atoi(b) : (128 * 1024);
    const char *sm = ::getenv("WP_SEND");    /* "par" = pigfAgent と同じく ts2Parallel worker から書く */
    parMode = ( sm && ::strcmp(sm, "par") == 0 ) ? 1 : 0;
    catchDbg = ::getenv("WP_CATCHDBG") ? 1 : 0;
}


/*******************************************
	STATE MACHINE
********************************************/

TS_STATE(INI_ptsObject_START)
{
	const char *exe = ::getenv("WP_CHILD");
	if ( !exe ) {
		::printf("[wpParent] WP_CHILD not set\n"); ::fflush(stdout);
		wpBigParent_exitCode = 1;
		return rDO|FIN_START;
	}
	::snprintf(cmd, sizeof(cmd), "#%s", exe);   /* '#' = MinGW ts2System 直接 exec */
	retPid = -1;
	sys = thNEW(ts2System,(ifThis, &retPid, cmd, &rfd, (sPtr<ts2IO>*)0, &wfd, 0));
	if ( retPid < 0 || wfd == thNULL ) {
		::printf("[wpParent] spawn failed\n"); ::fflush(stdout);
		wpBigParent_exitCode = 1;
		return rDO|FIN_START;
	}
	if ( ! ::getenv("WP_NODIV") )               /* WP_NODIV=1 で set_divisible を外す(spin 診断) */
		wfd->set_divisible();               /* プランナ経路(pigfAgent.cpp:451)と同じ */

	payload = (uint8_t *)::malloc(N > 0 ? N : 1);
	for ( int i = 0; i < N; i++ ) payload[i] = (uint8_t)('A' + (i % 26));

	pipe = thNEW(ptsWirePipe,(ifThis, rfd, wfd));   /* rio=子stdout / wio=子stdin */
	::printf("[wpParent] sending %d payload bytes over ptsWirePipe ...\n", N); ::fflush(stdout);
	return rDO|ACT_wpBigParent_WAIT;
}

TS_STATE(ACT_wpBigParent_WAIT)
{
	/* handshake(streamhdr 交換)完了 = TSE_ASSERT → 送信シーケンスへ。
	 * ★ pigfAgent の実配線を忠実に再現する **4 レコード列**:
	 *     C_OP(小) → C_ARG_INLINE(大, idx0) + C_ARG_INLINE(小 "6", idx1) → C_ARG_END(小 path) → W_END
	 *   引数 2 本はプランナ同様 **ts2Parallel の pipeline fan-out**(worker0 が worker1 を先に spawn)。
	 *   write_record は wlock(count=1)で直列化されるので wire 上は混ざらないが、大 arg の write_c yield 中に
	 *   小 arg worker が wlock 待ちで積み上がる contention は実配線と同じ。1 状態 = write_record 1 回を厳守。 */
	if ( ev->type == TSE_ASSERT ) {
		if ( ev->source == pipe && sent == 0 ) {
			sent = 1;
			return rDO|ACT_wpBigParent_SENDOP;
		}
		return 0;
	}
	if ( ev->type == TSE_RETURN ) {   /* handshake 前の異常終了など */
		if ( ev->source == pipe )
			return rDO|ACT_wpBigParent_DONE;
		return 0;
	}
	return 0;
}

TS_STATE(ACT_wpBigParent_SENDOP)   /* event 非依存: C_OP(op 名)を 1 回(pigfAgent SENDOP 相当) */
{
	pipe->write(C_OP, (const uint8_t *)"tube", 4);
	if ( parMode ) {
		/* pigfAgent の SEND と同型: 引数を ts2Parallel の pipeline fan-out で送る。
		 * worker は ptsObject ではないコルーチン。write が満杯で yield(sException)すると
		 * worker body が巻き戻って再走する。この resume 駆動 + wlock contention が MinGW で怪しい経路。 */
		sendIdx = 0;
		par = thNEW(ts2Parallel,(ifThis, 0,
			[this, idx=-1, phase=0](sPtr<ts2Parallel> me, sPtr<stdEvent> wev) mutable -> int {
				if ( phase == 0 ) {
					if ( sendIdx >= 2 ) return 1;
					idx = sendIdx++;
					if ( sendIdx < 2 ) me->spawn();   /* 次の引数 worker を先に起こす */
					phase = 1;
				}
				if ( phase == 1 ) {
					/* ★ hang 中に write_c が「内部 spin」か「sException 大量 throw(throw-driven 再走)」かを判定。
					 *   catch が高頻度で回れば = pipe->write(→write_record→write_c)は throw して抜け worker が再走
					 *     している(= IOCP イベント大量発生/throw-driven)。
					 *   catch が全く回らず pipe->write から戻らない = write_c 内部で spin、と切り分けられる。
					 *   catch 後は bare throw で再送出し通常の yield を壊さない。 */
					try {
						if ( idx == 0 )
							pipe->write(C_ARG_INLINE, payload, N);            /* 大 arg(payload は安定メンバ) */
						else
							pipe->write(C_ARG_INLINE, (const uint8_t *)"6", 1); /* 小 arg */
					} catch ( sException & e ) {
						if ( catchDbg && idx == 0 ) {
							throwCount++;
							if ( throwCount == 1 || (throwCount % 1000) == 0 )
								::fprintf(stderr,"[wp] big pipe->write sException throw #%ld (type=%d) = throw-driven 再走\n",
								          throwCount, e.type);
						}
						throw;   /* 再送出(yield 成立) */
					}
					phase = 2;
				}
				return 1;
			}));
		return ACT_wpBigParent_SENDPAR;   /* par の TSE_RETURN 待ち(rDO なし) */
	}
	return rDO|ACT_wpBigParent_SENDARG0;
}

TS_STATE(ACT_wpBigParent_SENDPAR)   /* WP_SEND=par: 引数 worker 群の完了(TSE_RETURN)→ C_ARG_END へ */
{
	if ( ::getenv("WP_SENDDBG") )
		::fprintf(stderr,"[wp] SENDPAR ev.type=%d %s\n",(int)ev->type,
		          ev->source==par?"==par":(ev->source==pipe?"==pipe":"OTHER"));
	if ( ev->type == TSE_RETURN && ev->source == par )
		return rDO|ACT_wpBigParent_SENDEND;
	return 0;
}

TS_STATE(ACT_wpBigParent_SENDARG0)  /* direct: 大 arg を 1 回(>64KB で payload 側 write_c が yield → 再走で冪等) */
{
	pipe->write(C_ARG_INLINE, payload, N);
	return rDO|ACT_wpBigParent_SENDARG1;
}

TS_STATE(ACT_wpBigParent_SENDARG1)  /* direct: 小 arg を 1 回 */
{
	pipe->write(C_ARG_INLINE, (const uint8_t *)"6", 1);
	return rDO|ACT_wpBigParent_SENDEND;
}

TS_STATE(ACT_wpBigParent_SENDEND)   /* event 非依存: C_ARG_END(目標パス)を 1 回(pigfAgent SENDEND 相当) */
{
	pipe->write(C_ARG_END, (const uint8_t *)"C:/tmp/wp.cache", 15);
	return rDO|ACT_wpBigParent_SENDWEND;
}

TS_STATE(ACT_wpBigParent_SENDWEND)  /* event 非依存: W_END 番兵を 1 回(pigfAgent SENDWEND 相当) */
{
	pipe->wend();
	return rDO|ACT_wpBigParent_REPLY;
}

TS_STATE(ACT_wpBigParent_REPLY)
{
	if ( ev->type == TSE_PACKET ) {
		/* 子の返信 "COUNT=<n>" を検証 */
		sPtr<ptsWirePacket> pkt = sPtr<ptsWirePacket>::d_cast(ev->msg_obj);
		int n = pkt->payload.length();
		char rbuf[64];
		int k = ( n < (int)sizeof(rbuf)-1 ) ? n : (int)sizeof(rbuf)-1;
		for ( int i = 0; i < k; i++ ) rbuf[i] = (char)pkt->payload[i];
		rbuf[k] = 0;
		const char *p = ::strstr(rbuf, "COUNT=");
		got = p ? ::atoi(p + 6) : -1;
		return 0;
	}
	if ( ev->type == TSE_RETURN ) {
		if ( ev->source == pipe )
			return rDO|ACT_wpBigParent_DONE;
		return 0;
	}
	return 0;
}

TS_STATE(ACT_wpBigParent_DONE)
{
	if ( got == N )
		::printf("[wpParent] OK — child received all %d bytes\n", N);
	else {
		::printf("[wpParent] FAIL/DEADLOCK — child got %d / %d bytes\n", got, N);
		wpBigParent_exitCode = 1;
	}
	::fflush(stdout);
	return rDO|FIN_START;
}

TS_STATE(FIN_START)
{
	if ( ::getenv("WP_TDDBG") ) ::fprintf(stderr,"[wp] FIN_START enter (null sys/pipe/rfd)\n");
	if ( payload ) { ::free(payload); payload = 0; }
	par = thNULL;
	pipe = thNULL;
	rfd = thNULL;
	sys = thNULL;
	if ( ::getenv("WP_TDDBG") ) ::fprintf(stderr,"[wp] FIN_START done -> FIN_ptsObject_START\n");
	return rDO|FIN_ptsObject_START;
}
