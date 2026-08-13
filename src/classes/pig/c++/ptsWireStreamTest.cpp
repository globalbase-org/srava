/*
 * ptsWireStreamTest — step4 往復テストのドライバ(ptsObject 派生)。
 * 同一プロセス内で WriterText → ReaderText の往復を 2 段階で検証する:
 *   フェーズ1(逐次)  : writer が W_END まで書き切ってから reader を起動。バイト一致の正当性。
 *   フェーズ2(並行)  : writer の TSE_ASSERT(header+meta 書込完了)で reader を起動し、
 *                       writer 本体書込と reader のポーリング読みを同時進行(同一プロセス)。
 * プロセス分離テストは pigfAgent 実装後(step5 以降)。
 *
 * 子の TSE_RETURN は ev->source で判別(rDO 遷移時の stale event を弾くため。tsCallList と同手法)。
 */
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/ptsApplication.h"   /* ptsApp 値メンバの完全型(ptsObject.h から移動・#3406 4.2) */
#include	"pig/c++/ptsWireCacheStreamWriterText.h"
#include	"pig/c++/ptsWireCacheStreamReaderText.h"
#include	"ts2/c++/stdEvent.h"
#include	"_ts2/c++/ptsWireStreamTest_.h"

#include	<stdio.h>
#include	<string.h>

CLASS_TINYSTATE(pig/c++/ptsWireStreamTest,pig/c++/ptsObject)

/* テスト終了コード。FAIL で 1。main() がアプリループ終了後に読んで返す。
 * NB: スケジューラ(ワーカスレッド)内から ::exit() を呼ぶと、他スレッドが
 *     グローバル static を使用中に静的デストラクタが走り race で落ちる。
 *     よって自爆せず FIN へ抜け、アプリのアイドル終了に任せる。 */
int ptsWireStreamTest_exitCode = 0;


#if 0

TS_BEGIN_IMPLEMENT


class TS_THISCLASS : public TS_BASECLASS {
public:
	ptsWireStreamTest_(
		sPtr<tinyState> parent);

	sRptr<tinyState,tinyState>		parent;
protected:
	sPtr<stdString>	payload;
	sPtr<stdString>	fnameSeq;
	sPtr<stdString>	fnameCc;
	sPtr<tinyState>	wH;        /* 現フェーズの writer ハンドル */
	sPtr<tinyState>	rH;        /* 現フェーズの reader ハンドル */
	sPtr<stdObject>	result;    /* reader の返した結果 */
	int		ccReturns; /* 並行フェーズの TSE_RETURN 受領数 */
private:
	int		check_result();
	TS_DEFARGS
};

TS_END_IMPLEMENT

TS_BEGIN_INTERFACE
#include	"ts2/c++/sRptr.h"
class tinyState;
class stdString;
TS_END_INTERFACE

#endif


ptsWireStreamTest_::ptsWireStreamTest_(TS_ARGS0)
        : ptsObject_(parent),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
    ccReturns = 0;
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

/* result(reader が返した stdString)が payload とバイト一致するか。1=一致 */
int
ptsWireStreamTest_::check_result()
{
	sPtr<stdString> rs = sPtr<stdString>::d_cast(result);
	if ( rs.is_notNull() == 0 ) return 0;             /* result が無い / stdString でない */
	return ::strcmp(rs->get_str(), payload->get_str()) == 0;
}


/*******************************************
	STATE MACHINE
********************************************/

TS_STATE(INI_ptsObject_START)
{
	payload  = thNEW(stdString,("hello pigwire cache streaming round-trip 0123456789\n"));
	fnameSeq = thNEW(stdString,("test_cachestream_seq.cache"));
	fnameCc  = thNEW(stdString,("test_cachestream_cc.cache"));

	sPtr<ptsObject> self = ifThis;   /* sWptr→sPtr アップキャストの暗黙変換(d_cast 不要) */
	wH = thNEW(ptsWireCacheStreamWriterText,(self, fnameSeq, payload));   /* sPtr<tinyState> へ暗黙アップキャスト */
	return rDO|ACT_SEQ_WAIT_WRITER;
}

/* ---- フェーズ1: 逐次 ---- */
TS_STATE(ACT_SEQ_WAIT_WRITER)
{
	if ( ev->type != TSE_RETURN ) return 0;           /* writer の TSE_ASSERT 等は無視 */
	if ( ev->source != wH ) return 0;
	if ( ev->msg_int != 0 ) {
		::printf("[streamtest] seq: writer error errCode=%lld\n", (long long)ev->msg_int);
		ptsWireStreamTest_exitCode = 1;
		return rDO|FIN_START;
	}
	return rDO|ACT_SEQ_SPAWN_READER;
}
TS_STATE(ACT_SEQ_SPAWN_READER)
{
	sPtr<ptsObject> self = ifThis;   /* sWptr→sPtr アップキャストの暗黙変換(d_cast 不要) */
	rH = thNEW(ptsWireCacheStreamReaderText,(self, fnameSeq));
	return rDO|ACT_SEQ_WAIT_READER;
}
TS_STATE(ACT_SEQ_WAIT_READER)
{
	if ( ev->type != TSE_RETURN ) return 0;
	if ( ev->source != rH ) return 0;                 /* stale な writer RETURN を弾く */
	result = ev->msg_obj;
	if ( check_result() )
		::printf("[streamtest] seq  round-trip: PASS\n");
	else {
		::printf("[streamtest] seq  round-trip: FAIL\n");
		ptsWireStreamTest_exitCode = 1;
		return rDO|FIN_START;
	}
	return rDO|ACT_CC_SPAWN_WRITER;
}

/* ---- フェーズ2: 並行(同一プロセス) ---- */
TS_STATE(ACT_CC_SPAWN_WRITER)
{
	ccReturns = 0; result = thNULL; rH = thNULL;
	sPtr<ptsObject> self = ifThis;   /* sWptr→sPtr アップキャストの暗黙変換(d_cast 不要) */
	wH = thNEW(ptsWireCacheStreamWriterText,(self, fnameCc, payload));
	return rDO|ACT_CC_WAIT;
}
TS_STATE(ACT_CC_WAIT)
{
	if ( ev->type == TSE_ASSERT ) {
		/* 最初の ASSERT は writer のもの(reader はまだ起動していない)→ reader 起動 */
		if ( ev->source == wH && rH == thNULL ) {
			if ( ev->msg_int != 0 ) {
				::printf("[streamtest] cc: writer error errCode=%lld\n",
				         (long long)ev->msg_int);
				ptsWireStreamTest_exitCode = 1;
				return rDO|FIN_START;
			}
			sPtr<ptsObject> self = ifThis;   /* sWptr→sPtr アップキャストの暗黙変換(d_cast 不要) */
			rH = sPtr<tinyState>::d_cast(
				thNEW(ptsWireCacheStreamReaderText,(self, fnameCc)));
		}
		return 0;
	}
	if ( ev->type == TSE_RETURN ) {
		if ( ev->source == rH ) result = ev->msg_obj;
		if ( ev->source == wH || ev->source == rH ) ccReturns++;
		if ( ccReturns >= 2 ) return rDO|ACT_CC_CHECK;
		return 0;
	}
	return 0;
}
TS_STATE(ACT_CC_CHECK)
{
	if ( check_result() )
		::printf("[streamtest] cc   round-trip: PASS\n");
	else {
		::printf("[streamtest] cc   round-trip: FAIL\n");
		ptsWireStreamTest_exitCode = 1;
	}
	return rDO|FIN_START;
}
TS_STATE(FIN_START)
{
	return rDO|FIN_ptsObject_START;
}
