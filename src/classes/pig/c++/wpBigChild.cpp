/*
 * wpBigChild — wpBigParent の相手。srava agent と同じ配線で **ptsWirePipe の子側**を回す
 *   (ptsObject 派生・CGAL 非依存)。自 stdin(rio)/ stdout(wio)に ptsWirePipe を 1 本張り、
 *   親が送る 1 レコード(大 payload)を受け取り、受信バイト数を "COUNT=<n>" レコードで返す。
 *
 * 配線: 子として起動され、s2IOstd で自 stdin=rio / stdout=wio を ts2IO 化(MinGW 対応)、
 *   pipe = ptsWirePipe(self, rio, wio)。
 *
 * プロトコル:
 *   親→子 : C_OP(payload=大データ), W_END
 *   子→親 : C_OP(payload="COUNT=<受信バイト数>"), W_END
 *
 * 状態:
 *   INI  : s2IOstd::init → ptsWirePipe → WAIT
 *   WAIT : TSE_PACKET(大レコード)で受信長を記録し "COUNT=n" を返信 + wend。
 *          親の W_END で pipe FIN(TSE_RETURN)→ FIN。
 */
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/ptsApplication.h"   /* ptsApp 値メンバの完全型(ptsObject.h から移動・#3406 4.2) */
#include	"pig/c++/pigwire.h"
#include	"pig/c++/ptsWirePipe.h"
#include	"pig/c++/ptsWirePacket.h"
#include	"ts2/c++/s2IOstd.h"          /* 自 stdin/stdout を portable に ts2IO 化(MinGW 対応) */
#include	"ts2/c++/ts2IO.h"
#include	"ts2/c++/stdEvent.h"
#include	"_ts2/c++/wpBigChild_.h"

#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<unistd.h>   /* usleep: 重い実 agent の drain 遅延を模す(WP_CHILD_SPIN_MS) */

CLASS_TINYSTATE(pig/c++/wpBigChild,pig/c++/ptsObject)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	wpBigChild_(
		sPtr<tinyState> parent);

	sRptr<tinyState,tinyState>		parent;
protected:
	sPtr<ts2IO>		in;
	sPtr<ts2IO>		out;
	sPtr<ptsWirePipe>	pipe;
	int			got;
	int			replied;
	int			spinMs;
	int			spun;
	char			cbuf[64];
private:
	TS_DEFARGS
};

TS_END_IMPLEMENT

TS_BEGIN_INTERFACE
#include	"ts2/c++/sRptr.h"
class tinyState;
class ts2IO;
class ptsWirePipe;
TS_END_INTERFACE

#endif


wpBigChild_::wpBigChild_(TS_ARGS0)
        : ptsObject_(parent),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
    got = -1; replied = 0; spun = 0;
    const char *s = ::getenv("WP_CHILD_SPIN_MS");   /* >0 で C_OP 受信時に drain を止めてパイプを満杯化 */
    spinMs = s ? ::atoi(s) : 0;
}


/*******************************************
	STATE MACHINE
********************************************/

TS_STATE(INI_ptsObject_START)
{
	s2IOstd::init(ifThis, &in, &out, (sPtr<ts2IO>*)0);
	if ( in == thNULL || out == thNULL )
		return rDO|FIN_START;
	pipe = thNEW(ptsWirePipe,(ifThis, in, out));   /* rio=自stdin / wio=自stdout */
	return rDO|ACT_wpBigChild_WAIT;
}

TS_STATE(ACT_wpBigChild_WAIT)
{
	if ( ev->type == TSE_PACKET ) {
		/* 実配線の 4 レコード列(C_OP / C_ARG_INLINE 大 / C_ARG_INLINE 小 / C_ARG_END)を受ける。
		 * 受信 payload 長の最大(= 大 arg)を記録し、C_ARG_END(全引数完了)で "COUNT=n" を返信。
		 * 実 agent が C_ARG_END で計算開始→ A_SAVE を返すのと同じ位置(自分の read が FIN する前に書く)。 */
		sPtr<ptsWirePacket> pkt = sPtr<ptsWirePacket>::d_cast(ev->msg_obj);
		int n = pkt->payload.length();
		if ( n > got ) got = n;
		/* 重い実 agent(CGAL・値パース)が C_OP 処理中に stdin を drain しない状況を模す:
		 * このハンドラは read コルーチンの中から同期呼びされるので、ここでブロックすると
		 * 次レコード(大 arg)の read が止まりパイプが満杯化 → 親の大 write が yield して
		 * MinGW の write-resume race を突く。 */
		if ( pkt->type == C_OP && spinMs > 0 && spun == 0 ) {
			spun = 1;
			::usleep((useconds_t)spinMs * 1000);
		}
		if ( pkt->type == C_ARG_END && replied == 0 ) {
			replied = 1;
			::snprintf(cbuf, sizeof(cbuf), "COUNT=%d", got);
			return rDO|ACT_wpBigChild_REPLY;   /* 1 状態 = write_record 1 回に分割 */
		}
		return 0;
	}
	if ( ev->type == TSE_RETURN ) {
		/* 親が W_END を送り自分の read が閉じた → 終了 */
		if ( ev->source == pipe )
			return rDO|FIN_START;
		return 0;
	}
	return 0;
}

TS_STATE(ACT_wpBigChild_REPLY)   /* event 非依存: COUNT レコードを 1 回送る */
{
	pipe->write(C_OP, (const uint8_t *)cbuf, (int)::strlen(cbuf));
	return rDO|ACT_wpBigChild_REPLYEND;
}

TS_STATE(ACT_wpBigChild_REPLYEND)   /* event 非依存: W_END 番兵を 1 回送る → 親の wend を待つ(WAIT へ) */
{
	pipe->wend();
	return rDO|ACT_wpBigChild_WAIT;
}

TS_STATE(FIN_START)
{
	pipe = thNULL;
	return rDO|FIN_ptsObject_START;
}
