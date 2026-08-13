/*
 * pigfWhile — while ループの tinyState helper(pigfFunction 派生)。
 * args[0]=条件式(テンプレ)、args[1]=body 文(テンプレ)。
 *
 * clone/thunk 再評価モデル: 遅延ノードはメモ(result)を持つので毎周そのままでは再評価できない。
 * → 各周回で cond / body を clone() して新鮮ノードで評価する。env は囲みスコープを共有(継承)
 *   するので、body 内の代入(i = i+1 等)が次周の cond/body に効く。
 *
 * 状態: ACT_START(cond 評価。真なら BODY、偽なら DONE)→ ACT_pigfWhile_BODY(body 評価→次周)。
 * compact は async で yield しうるが、cur* メンバで「この周回の clone 済みノード」を保持するので
 * 再走しても再 clone せず前進する(= prepared フラグ相当を周回単位で持つ)。
 * 返り値: 最後の周回の body 値(0 周なら null)。
 */
#include	"pig/c++/pigfFunction.h"
#include	"pig/c++/ptsApplication.h"   /* ptsApp 値メンバの完全型(ptsObject.h から移動・#3406 4.2) */
#include	"pig/c++/pigData.h"
#include	"_ts2/c++/pigfWhile_.h"

CLASS_TINYSTATE(pig/c++/pigfWhile,pig/c++/pigfFunction)

/* 暴走 while の保険(無限ループでテスト/プロセスが hang しないように)。超過でエラー。 */
static const int PIGF_WHILE_MAX_ITER = 1000000;


#if 0

TS_BEGIN_IMPLEMENT


class TS_THISCLASS : public TS_BASECLASS {
public:
	pigfWhile_(
		sPtr<ptsObject> parent,
		sPtr<pigDataOperator> _front);

	sRptr<ptsObject,tinyState>		parent;
private:
protected:
	sPtr<pigData>	curCond;    /* この周回の clone 済み cond(thNULL=未 clone) */
	sPtr<pigData>	curBody;    /* この周回の clone 済み body(thNULL=未 clone) */
	sPtr<pigData>	last;       /* 最後の周回の body 値 */
	int		loopDestroyed;   /* clone した cond/body へ destroy を転送済み(1 回だけ) */
	int		iterCount;
	TS_DEFARGS
};

TS_END_IMPLEMENT

TS_BEGIN_INTERFACE
#include	"ts2/c++/sRptr.h"
class ptsObject;
class pigDataOperator;
class pigData;
TS_END_INTERFACE

#endif


pigfWhile_::pigfWhile_(TS_ARGS0)
        : pigfFunction_(parent,_front),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
    iterCount = 0;
    loopDestroyed = 0;
}


/*******************************************
	STATE MACHINE
********************************************/

TS_STATE(INI_pigfFunction_START)
{
	curCond   = thNULL;
	curBody   = thNULL;
	last      = thNULL;
	iterCount = 0;
	return rDO|ACT_START;
}

/* 条件評価。真なら BODY、偽なら DONE。 */
TS_STATE(ACT_START)
{
	/* ★ destroy の転送 (ひさ設計 2026-08-11)。curCond/curBody は **自分が clone した持ち物** なので
	 * 呼び元の AST からは辿れない = ここで明示的に送る。無条件巡回はしない (args は front の持ち物)。
	 * 1 度だけ送り通常経路へ落とす (destroy された子は front をエラー解決するので compact が拾う)。 */
	if ( is_destroyed() && ! loopDestroyed ) {
		loopDestroyed = 1;
		if ( ::getenv("PIG_DBG_TD") ) ::fprintf(stderr, "[td] while: destroy 転送\n");
		if ( curCond.is_notNull() ) curCond->destroy();
		if ( curBody.is_notNull() ) curBody->destroy();
	}
	if ( args.length() < 2 ) {                 /* 文法上ありえないが安全に */
		front->set_result(thNEW(pigDataNull,()));
		return rDO|FIN_START;
	}
	if ( curCond == thNULL )
		curCond = args[0]->clone();            /* 周回ごとに新鮮ノード */
	sPtr<pigData> cv = curCond->compact();     /* 不動点解決。async なら yield→再走(curCond 保持) */
	if ( cv->is_error() ) {
		front->set_result(cv);
		return rDO|FIN_START;
	}
	if ( cv->get_bool() )
		return rDO|ACT_pigfWhile_BODY;
	return rDO|ACT_pigfWhile_DONE;
}

/* body 評価 → 次周へ。 */
TS_STATE(ACT_pigfWhile_BODY)
{
	/* ★ destroy の転送 (ひさ設計 2026-08-11)。curCond/curBody は **自分が clone した持ち物** なので
	 * 呼び元の AST からは辿れない = ここで明示的に送る。無条件巡回はしない (args は front の持ち物)。
	 * 1 度だけ送り通常経路へ落とす (destroy された子は front をエラー解決するので compact が拾う)。 */
	if ( is_destroyed() && ! loopDestroyed ) {
		loopDestroyed = 1;
		if ( ::getenv("PIG_DBG_TD") ) ::fprintf(stderr, "[td] while: destroy 転送\n");
		if ( curCond.is_notNull() ) curCond->destroy();
		if ( curBody.is_notNull() ) curBody->destroy();
	}
	if ( curBody == thNULL ) {
		if ( ++iterCount > PIGF_WHILE_MAX_ITER ) {
			front->set_result(thNEW(pigDataError,("while: iteration limit exceeded",thNULL)));
			return rDO|FIN_START;
		}
		curBody = args[1]->clone();            /* 周回ごとに新鮮ノード */
	}
	sPtr<pigData> bv = curBody->compact();     /* body 評価(代入の副作用は共有 env に効く) */
	if ( bv->is_error() ) {
		int ck = bv->control_kind();
		if ( ck == CTRL_BREAK ) {              /* break: ループ終了(これまでの last を返す) */
			curCond = thNULL; curBody = thNULL;
			return rDO|ACT_pigfWhile_DONE;
		}
		if ( ck == CTRL_CONTINUE ) {           /* continue: 次周へ(last 据え置き) */
			curCond = thNULL; curBody = thNULL;
			return rDO|ACT_START;
		}
		front->set_result(bv);                 /* return 信号 or 実エラー → 上方へ伝播 */
		return rDO|FIN_START;
	}
	last    = bv;
	curCond = thNULL;                          /* 次周: cond/body を再 clone */
	curBody = thNULL;
	return rDO|ACT_START;
}

TS_STATE(ACT_pigfWhile_DONE)
{
	front->set_result( (last == thNULL) ? sPtr<pigData>(thNEW(pigDataNull,())) : last );
	return rDO|FIN_START;
}

TS_STATE(FIN_START)
{
	return rDO|FIN_pigfWhile_START;
}

TS_STATE(FIN_pigfWhile_START)
{
	return rDO|FIN_pigfFunction_START;
}
