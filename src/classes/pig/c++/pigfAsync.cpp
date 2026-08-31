/*
 * pigfAsync — `async { body...; sync: S }` 文の tinyState helper(pigfFunction 派生)。
 * pigfSequence(直列ブロック)+ pigfPrintAsync(発行順チェーン)を一般化したもの。
 *
 * args = [ prev, stmt0, stmt1, ..., stmtK ]
 *   prev      … 直前 async の done 信号(planner の syncTail。初回は解決済み null)。
 *   stmt0..   … ブロック内の文(env を 1 つ作って **同一スコープ**で順に評価)。
 *   _front の mode(get_mode())== 1 のとき hasSync: 末尾 stmtK が sync 文(最後に評価し整列対象)。
 *
 * 肝:
 *   - body 文は async 起動と同時に走り出す(各 async helper は別 trigger される)→ body 間は並列。
 *   - prev 待ちは body 完了の **後**。sync 文だけが全 async を跨いで発行順に整列する。
 *   - body も含め同じ子 env で評価するので、body の var を sync 文から参照できる(スコープ共有)。
 *   - エラーが出ても **中断せず**、err を _front の結果に載せて(continue-and-collect)チェーンを前進。
 *     どの経路でも ACT_DONE で set_result するので、次 async の prev 待ちは必ず解放される(no deadlock)。
 *
 *   INI       : seqIdx=1(args[0]=prev は飛ばす)。env を一段深くする(ブロックスコープ)。
 *   ACT_BODY  : body 文を順に compact(is_error 兼 compact)。エラーは errVal に捕捉し以降スキップ。
 *               hasSync の場合、末尾 1 文(sync)は body では評価しない。
 *   ACT_WAITPREV : prev を compact(直前 async の sync 完了を待つ。yield→再走)。
 *   ACT_SYNC  : hasSync かつ err 無しのとき sync 文を **同じ env** で compact(発行順に出力)。
 *   ACT_DONE  : _front->set_result(err があれば err / 無ければ null)→ 次 async の prev 待ちを解放。
 */
#include	"pig/c++/pigfFunction.h"
#include	"pig/c++/osglue.h"   /* osglue_env_int (#3419 §17.2) */
#include	"pig/c++/ptsApplication.h"   /* ptsApp 値メンバの完全型(ptsObject.h から移動・#3406 4.2) */
#include	"pig/c++/pigData.h"
#include	"_ts2/c++/pigfAsync_.h"

CLASS_TINYSTATE(pig/c++/pigfAsync,pig/c++/pigfFunction)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	pigfAsync_(
		sPtr<ptsObject> parent,
		sPtr<pigDataOperator> _front);

	sRptr<ptsObject,tinyState>		parent;
private:
protected:
	int		seqIdx;     /* body 文の評価カーソル(yield→再走で据え置き再開) */
	int		bodyEnd;    /* body の終端 index(hasSync なら args.length()-1、無ければ args.length()) */
	int		hasSync;    /* _front->get_mode():末尾文が sync 文か */
	sPtr<pigData>	errVal;     /* body/sync で捕捉したエラー(continue-and-collect) */
	int		asyDestroyed;   /* 評価中の body 文へ destroy を転送済み(1 回だけ) */
};

TS_END_IMPLEMENT

TS_BEGIN_INTERFACE
#include	"ts2/c++/sRptr.h"
#include	"pig/c++/pigData.h"
class ptsObject;
class pigDataOperator;
class pigData;
TS_END_INTERFACE

#endif


pigfAsync_::pigfAsync_(TS_ARGS0)
        : pigfFunction_(parent,_front),
	  parent(tinyState_::parent)
{
    seqIdx  = 1;
    asyDestroyed = 0;
    bodyEnd = 1;
    hasSync = 0;
}


/*******************************************
	STATE MACHINE
********************************************/

TS_STATE(INI_pigfFunction_START)
{
	seqIdx  = 1;                                /* args[0]=prev は文ではない */
	hasSync = sPtr<pigDataFunction_b>::d_cast(_front)->get_mode();
	bodyEnd = hasSync ? (args.length() - 1) : args.length();
	if ( bodyEnd < 1 ) bodyEnd = 1;             /* prev だけ(空 async)の保険 */
	errVal  = thNULL;
	/* ブロックスコープ: body 文と sync 文を同じ子 env で評価する(pigfSequence と同じ)。
	 * これで body の var(DEF)を sync 文が参照でき、CACHE_DIR 等は親チェーンで引ける。 */
	env = thNEW(pigEnvironment,(env));
	return rDO|ACT_START;
}

TS_STATE(ACT_START)   /* body 文を 1 つずつ順に評価(重い文は yield → 本状態が再走。seqIdx で再開) */
{
	/* ★ destroy の転送 (ひさ設計 2026-08-11)。pigfSequence と同型: **いま評価中の文だけ**を畳む。
	 * ACT_WAITPREV が待つ args[0] は「直前 async の完了」= 自分の持ち物ではないので触らない。 */
	if ( is_destroyed() && ! asyDestroyed ) {
		asyDestroyed = 1;
		if ( osglue_env_int("PIG_DBG_TD", 0) ) ::fprintf(stderr, "[td] async: destroy 転送\n");
		if ( seqIdx < args.length() && args[seqIdx].is_notNull() ) args[seqIdx]->destroy();
	}
	if ( seqIdx >= bodyEnd )
		return rDO|ACT_WAITPREV;
	sPtr<pigData> s = args[seqIdx];
	if ( s->is_error() ) {                       /* compact 兼ねる。エラーは捕捉し body 打ち切り */
		errVal = s;
		return rDO|ACT_WAITPREV;
	}
	seqIdx++;
	return rDO|ACT_START;
}

TS_STATE(ACT_WAITPREV)
{
	if ( args.length() >= 1 )
		(void) args[0]->compact();           /* 直前 async の sync 完了を待つ(発行順保持・yield→再走) */
	return rDO|ACT_SYNC;
}

TS_STATE(ACT_SYNC)
{
	if ( hasSync && errVal == thNULL ) {
		sPtr<pigData> s = args[args.length() - 1];   /* 末尾 = sync 文。同じ env で評価=発行順に出力 */
		if ( s->is_error() )
			errVal = s;
	}
	return rDO|ACT_DONE;
}

TS_STATE(ACT_DONE)
{
	/* err があれば結果に載せて drain が拾えるようにし、無ければ null。いずれにせよ set_result する
	 * ことで次 async の prev 待ち(ACT_WAITPREV)を必ず解放する(エラーでもチェーンが前進)。 */
	if ( errVal != thNULL )
		_front->set_result(errVal);
	else
		_front->set_result(thNEW(pigDataNull,()));
	return rDO|FIN_START;
}

TS_STATE(FIN_START)
{
	return rDO|FIN_pigfAsync_START;
}

TS_STATE(FIN_pigfAsync_START)
{
	return rDO|FIN_pigfFunction_START;
}
