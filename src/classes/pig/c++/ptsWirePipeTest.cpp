/*
 * ptsWirePipeTest — step5 ptsWirePipe の同一プロセス echo 往復テスト(ptsObject 派生)。
 *
 * socketpair で繋いだ 2 本の ptsWirePipe を 1 つのドライバが親として持ち、ev->source で
 * pipeA(プランナ側)/ pipeB(エージェント側)を判別する:
 *   - 両 pipe が handshake 完了 → TSE_ASSERT(ready)。pipeA ready で C_OP "ping" を送信。
 *   - pipeB が ping を TSE_PACKET 受信 → そのまま echo + 番兵(wend) を返す(エージェント役)。
 *   - pipeA が echo を TSE_PACKET 受信 → バイト一致を検証 + 番兵(wend)。
 *   - 双方が相手の番兵を読んで FIN → TSE_RETURN 2 本で完了判定。
 * 番兵で双方が self-terminate するので destroy() 不要。全 state machine 完了でアプリがアイドル終了。
 * プロセス分離テストは pigfAgent 実装後。
 */
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/pigwire.h"
#include	"pig/c++/ptsWirePipe.h"
#include	"pig/c++/ptsWirePacket.h"
#include	"ts2/c++/ts2IOdescriptor.h"
#include	"ts2/c++/stdEvent.h"
#include	"_ts2/c++/ptsWirePipeTest_.h"

#include	<sys/socket.h>
#include	<stdio.h>
#include	<string.h>

CLASS_TINYSTATE(pig/c++/ptsWirePipeTest,pig/c++/ptsObject)

/* テスト終了コード(main がアプリループ終了後に返す)。スケジューラ内 ::exit は使わない。 */
int ptsWirePipeTest_exitCode = 0;

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ptsWirePipeTest_(
		sPtr<tinyState> parent);

	sRptr<tinyState,tinyState>		parent;
protected:
	sPtr<ts2IO>		io0;
	sPtr<ts2IO>		io1;
	sPtr<ptsWirePipe>	pipeA;
	sPtr<ptsWirePipe>	pipeB;
	sPtr<stdString>		payload;
	int			pingSent;
	int			echoOK;
	int			returns;
private:
	TS_DEFARGS
};

TS_END_IMPLEMENT

TS_BEGIN_INTERFACE
#include	"ts2/c++/sRptr.h"
class tinyState;
class ts2IO;
class ptsWirePipe;
class stdString;
TS_END_INTERFACE

#endif


ptsWirePipeTest_::ptsWirePipeTest_(TS_ARGS0)
        : ptsObject_(parent),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
    pingSent = 0; echoOK = 0; returns = 0;
}


/*******************************************
	STATE MACHINE
********************************************/

TS_STATE(INI_ptsObject_START)
{
	int sv[2];
	if ( ::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0 ) {
		::printf("[wirepipe] socketpair failed\n");
		ptsWirePipeTest_exitCode = 1;
		return rDO|FIN_START;
	}
	sPtr<tinyState> self = ifThis;   /* sWptr→sPtr もアップキャストの暗黙変換で OK(d_cast 不要) */
	io0   = thNEW(ts2IOdescriptor,(self, sv[0]));   /* sPtr<ts2IO> ← sPtr<ts2IOdescriptor> 暗黙アップキャスト */
	io1   = thNEW(ts2IOdescriptor,(self, sv[1]));
	pipeA = thNEW(ptsWirePipe,(self, io0));
	pipeB = thNEW(ptsWirePipe,(self, io1));
	payload = thNEW(stdString,("ping over pigwire pipe 0123456789"));
	return rDO|ACT_WAIT;
}

TS_STATE(ACT_WAIT)
{
	if ( ev->type == TSE_ASSERT ) {
		/* pipeA の handshake 完了で ping を送る(pipeA が streamhdr を書いた後なので順序安全) */
		if ( ev->source == pipeA && pingSent == 0 ) {
			pingSent = 1;
			pipeA->write_str(C_OP, payload);
		}
		return 0;
	}
	if ( ev->type == TSE_PACKET ) {
		sPtr<ptsWirePacket> pkt = sPtr<ptsWirePacket>::d_cast(ev->msg_obj);
		if ( ev->source == pipeB ) {
			/* エージェント側: 受け取った record をそのまま echo + 番兵 */
			int n = pkt->payload.length();
			pipeB->write((int)pkt->type, n > 0 ? &pkt->payload[0] : (const uint8_t*)0, n);
			pipeB->wend();
		}
		else if ( ev->source == pipeA ) {
			/* プランナ側: echo を検証 + 番兵 */
			int n = pkt->payload.length();
			const char *exp = payload->get_str();
			int ok = ( pkt->type == C_OP ) && ( n == (int)::strlen(exp) );
			for ( int i = 0; i < n && ok; ++i )
				if ( pkt->payload[i] != (uint8_t)exp[i] ) ok = 0;
			echoOK = ok;
			pipeA->wend();
		}
		return 0;
	}
	if ( ev->type == TSE_RETURN ) {
		if ( ev->source == pipeA || ev->source == pipeB ) {
			returns++;
			if ( returns >= 2 ) return rDO|ACT_CHECK;
		}
		return 0;
	}
	return 0;
}

TS_STATE(ACT_CHECK)
{
	if ( echoOK )
		::printf("[wirepipe] echo round-trip: PASS\n");
	else {
		::printf("[wirepipe] echo round-trip: FAIL\n");
		ptsWirePipeTest_exitCode = 1;
	}
	return rDO|FIN_START;
}
TS_STATE(FIN_START)
{
	return rDO|FIN_ptsObject_START;
}
