/*
 * ptsAgentStub — srava-agent の control-plane + 最小 data-plane を話すスタブ(ptsObject 派生)。
 * step5 pigfAgent の pigDataPair/pigDataCache/継続テスト用。実幾何計算はしない。
 *
 * 配線: 子プロセスとして起動され、自 stdin(fd0)=rio / stdout(fd1)=wio に ptsWirePipe 1 本。
 *
 * プロトコル:
 *   plan→agent : C_OP(op 名), C_ARG_INLINE/C_ARG_PATH(各引数テキスト)*,
 *                C_ARG_END(payload=**目標キャッシュパス**), W_END
 *   agent→plan : A_SAVE_BEGIN(payload=データキャッシュ本文), A_SAVE_DONE, A_BYE, W_END
 *
 * 動作: 受信した引数テキストを連結して結果本文 "R(arg0,arg1,...)" を作り、
 *   - C_ARG_END で受けたパスへ WriterText で実データキャッシュ(D_TEXT)を書き出し、
 *   - 同じ本文を A_SAVE_BEGIN に相乗り(srava はファイルを読まず即値解決できる)。
 *   同一引数なら同一本文 → 同一キャッシュ。次回同一演算はキャッシュ HIT で agent 不起動。
 *
 * 状態:
 *   INI          : fd0/fd1 に ptsWirePipe → WAIT
 *   WAIT         : C_ARG_* を蓄積。C_ARG_END でパス確定 → WriterText 起動 → WRITING
 *   WRITING      : writer の TSE_RETURN → A_SAVE_BEGIN/DONE/BYE + wend → DONE
 *   DONE         : plan の wend(pipe TSE_RETURN)→ FIN
 */
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/pigwire.h"
#include	"pig/c++/ptsWirePipe.h"
#include	"pig/c++/ptsWirePacket.h"
#include	"pig/c++/ptsWireCacheStreamWriterText.h"
#include	"ts2/c++/s2IOstd.h"          /* 自 stdin/stdout を portable に ts2IO 化(MinGW 対応) */
#include	"ts2/c++/stdEvent.h"
#include	"_ts2/c++/ptsAgentStub_.h"

#include	<stdio.h>
#include	<string.h>

CLASS_TINYSTATE(pig/c++/ptsAgentStub,pig/c++/ptsObject)

/* inline 引数はプランナーが serialize() した値リテラル。実エージェント(cgatsAgent)は共有
 * VALUE パーサで構造値に復元するが、この stub は軽量テストダブルなので最小デコードに留める:
 * 先頭が " のときだけ pigDataString::serialize() の逆(unquote + unescape)を行い、数値/ident/
 * 構造リテラル([..]/{..})はそのままテキストとして扱う(stub は結果本文をテキスト連結するだけ)。 */
static sPtr<stdString>
decode_inline(sPtr<stdString> text)
{
	const char *s = text->get_str();
	if ( s[0] != '"' )
		return text;
	char buf[1024];
	int k = 0;
	const char *p = s + 1;
	while ( *p && *p != '"' && k < (int)sizeof(buf)-1 ) {
		char c = *p++;
		if ( c == '\\' && *p ) {
			char e = *p++;
			c = ( e=='n' ) ? '\n' : ( e=='t' ) ? '\t' : e;
		}
		buf[k++] = c;
	}
	buf[k] = 0;
	return thNEW(stdString,(buf));
}

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ptsAgentStub_(
		sPtr<tinyState> parent);

	sRptr<tinyState,tinyState>		parent;
protected:
	sPtr<ts2IO>		rio;
	sPtr<ts2IO>		wio;
	sPtr<ptsWirePipe>	pipe;
	sPtr<ptsWireCacheStreamWriterText>	writer;
	sArray<sPtr<stdString> >	argv;   /* arg_index で格納(順不同受信に対応) */
	sPtr<stdString>		cachePath;   /* C_ARG_END で受けた目標パス */
	sPtr<stdString>		resultBody;  /* "R(args)" */
private:
	TS_DEFARGS
};

TS_END_IMPLEMENT

TS_BEGIN_INTERFACE
#include	"ts2/c++/sRptr.h"
#include	"ts2/c++/sArray.h"
#include	"ts2/c++/stdString.h"
class tinyState;
class ts2IO;
class ptsWirePipe;
class ptsWireCacheStreamWriterText;
class stdString;
TS_END_INTERFACE

#endif


ptsAgentStub_::ptsAgentStub_(TS_ARGS0)
        : ptsObject_(parent),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	STATE MACHINE
********************************************/

TS_STATE(INI_ptsObject_START)
{
	sPtr<tinyState> self = ifThis;
	s2IOstd::init(self, &rio, &wio);   /* 自 stdin(読み)=rio / stdout(書き)=wio。生 fd を排し MinGW 対応 */
	wio->set_divisible();   /* 応答も分割書き込み(cgatsAgent と同様。>64KB の不可分書き込み停止/CPUループ回避) */
	pipe = thNEW(ptsWirePipe,(self, rio, wio));
	return ACT_ptsAgentStub_WAIT;   /* pipe イベント待ち → rDO なし */
}

TS_STATE(ACT_ptsAgentStub_WAIT)
{
	if ( ev->type == TSE_PACKET ) {
		sPtr<ptsWirePacket> pkt = sPtr<ptsWirePacket>::d_cast(ev->msg_obj);
		int n = pkt->payload.length();
		switch ( pkt->type ) {
		case C_OP:
			break;   /* 演算子名(skeleton では未使用) */
		case C_ARG_PATH:
		case C_ARG_INLINE: {
			/* payload = [arg_index(u32 LE)][text]。番号順に格納(順不同受信に対応)。 */
			if ( n < 4 )
				break;   /* 不正(index 欠落) */
			const uint8_t *p = &pkt->payload[0];
			uint32_t idx = (uint32_t)p[0] | ((uint32_t)p[1] << 8)
			             | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
			if ( (int)idx >= argv.length() )
				argv.length((int)idx + 1);   /* 穴は thNULL のまま */
			{
				sPtr<stdString> text = thNEW(stdString,((const char*)(p + 4), 0, n - 4));
				/* INLINE は serialize 値 → デコード。PATH は生のキャッシュパス → そのまま。 */
				argv[(int)idx] = ( pkt->type == C_ARG_INLINE ) ? decode_inline(text) : text;
			}
			break;
		}
		case C_ARG_END: {
			/* payload = 目標キャッシュパス。結果本文を **有効な VALUE リテラル** = クォート文字列
			 * "R(arg0,arg1,...)" として作る(プランナが value-parse して pigDataString に復元するため)。 */
			cachePath  = ( n > 0 )
			    ? sPtr<stdString>(thNEW(stdString,((const char*)&pkt->payload[0], 0, n)))
			    : sPtr<stdString>(thNEW(stdString,("/tmp/srava-agent-stub.cache")));
			resultBody = thNEW(stdString,("\"R("));
			for ( int k = 0 ; k < argv.length() ; k++ ) {
				if ( k > 0 )
					resultBody = resultBody->add(",");
				if ( argv[k] != thNULL )
					resultBody = resultBody->add(argv[k]);
			}
			resultBody = resultBody->add(")\"");
			writer = thNEW(ptsWireCacheStreamWriterText,(ifThis, cachePath, resultBody));
			return ACT_ptsAgentStub_WRITING;   /* writer の TSE_RETURN 待ち → rDO なし */
		}
		default:
			break;
		}
		return 0;
	}
	if ( ev->type == TSE_RETURN )
		return rDO|FIN_START;   /* plan が先に閉じた */
	return 0;
}

/* NB: write は event でガードした分岐の中で複数回呼ばない。write_c が yield(sException)すると
 * 状態が先頭再走し、(a)ev が writer TSE_RETURN でなくなって分岐が成立しない、(b)先頭の write が
 * 二重実行される。→ 検出は WRITING(遷移のみ)、実 write は event 非依存・1 状態 1 write_record で
 * 段階送信する(各 write_record の yield 再走は ps_write_record が安全に再開)。 */
TS_STATE(ACT_ptsAgentStub_WRITING)
{
	/* キャッシュ書込完了 → 結果一式の送信へ(write はしない) */
	if ( ev->type == TSE_RETURN && ev->source == writer )
		return rDO|ACT_ptsAgentStub_SAVEBEGIN;
	return 0;
}

TS_STATE(ACT_ptsAgentStub_SAVEBEGIN)   /* 本文を A_SAVE_BEGIN に相乗り(1 write) */
{
	const char *body = resultBody->get_str();
	pipe->write(A_SAVE_BEGIN, (const uint8_t*)body, (int)::strlen(body));
	return rDO|ACT_ptsAgentStub_SAVEDONE;
}

TS_STATE(ACT_ptsAgentStub_SAVEDONE)
{
	pipe->write(A_SAVE_DONE, 0, 0);
	return rDO|ACT_ptsAgentStub_SAVEBYE;
}

TS_STATE(ACT_ptsAgentStub_SAVEBYE)
{
	pipe->write(A_BYE, 0, 0);
	return rDO|ACT_ptsAgentStub_SAVEWEND;
}

TS_STATE(ACT_ptsAgentStub_SAVEWEND)
{
	pipe->wend();
	return ACT_ptsAgentStub_DONE;   /* pipe 閉じの TSE_RETURN 待ち → rDO なし */
}

TS_STATE(ACT_ptsAgentStub_DONE)
{
	if ( ev->type == TSE_RETURN && ev->source == pipe )
		return rDO|FIN_START;
	return 0;
}

TS_STATE(FIN_START)
{
	return rDO|FIN_ptsObject_START;
}
