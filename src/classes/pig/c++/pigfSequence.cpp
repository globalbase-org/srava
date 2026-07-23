/*
 * pigfSequence — 文の並び(statement list)を順に評価する tinyState helper(pigfFunction 派生)。
 * args = [stmt0, stmt1, ..., stmtN-1]。各文を順に compact(= 副作用を起こす。var 宣言/代入は
 * env への束縛、export は agent 起動)し、最後の文の値を結果として返す。
 *
 * 遅延の肝: pigfAssign は値を compact せず env に束縛する(遅延束縛)。よって途中の var 宣言を
 * compact しても重い計算は走らず、最後の式(例 export(a|||b))の評価時に参照された分だけ
 * 連鎖的に解決される(関数型の遅延評価)。env は実態親(caller)から継承する一つを共有するので、
 * 前の文の def_var を後の文の変数参照が見られる。
 */
#include	"pig/c++/pigfFunction.h"
#include	"pig/c++/pigData.h"
#include	"_ts2/c++/pigfSequence_.h"

CLASS_TINYSTATE(pig/c++/pigfSequence,pig/c++/pigfFunction)


#if 0

TS_BEGIN_IMPLEMENT


class TS_THISCLASS : public TS_BASECLASS {
public:
	pigfSequence_(
		sPtr<ptsObject> parent,
		sPtr<pigDataOperator> _front);

	sRptr<ptsObject,tinyState>		parent;
private:
protected:
	int		seqIdx;
	sPtr<pigData>	seqLast;
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


pigfSequence_::pigfSequence_(TS_ARGS0)
        : pigfFunction_(parent,_front),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
    seqIdx = 0;
}


/*******************************************
	STATE MACHINE
********************************************/

TS_STATE(INI_pigfFunction_START)
{
	seqIdx = 0;
	/* ブロックスコープ: sequence(= ブロック {..}・プログラム・lambda body・for desugar)は
	 * env を一段深くする。block 内の `var`(DEF)はこの子 env に閉じ、`SET` は上方探索で外側に
	 * 届く(ループ変数 i=i+1 等は外で定義された i を更新)。for-init の var もこの子 env に入り
	 * ループ終了で破棄される(囲みに漏れない)。CACHE_DIR 等は親チェーンで引ける。 */
	env = thNEW(pigEnvironment,(env));
	return rDO|ACT_START;
}

TS_STATE(ACT_START)   /* 文を 1 つずつ順に評価(async 文は yield → 本状態が再走。seqIdx で再開) */
{
	if ( seqIdx >= args.length() )
		return rDO|ACT_pigfSequence_DONE;
	/* is_error() が compact を兼ねる(遅延ノードは is_error()=compact()->is_error())。yield しうるが
	 * seqIdx 据え置きで再走→同じ文を再評価(解決済みは即返る)→ 前進。エラーなら即その値で打ち切り。 */
	seqLast = args[seqIdx];
	if ( seqLast->is_error() ) {
		front->set_result(seqLast);
		return rDO|FIN_START;
	}
	seqIdx++;
	return rDO|ACT_START;
}

TS_STATE(ACT_pigfSequence_DONE)
{
	/* 最後の文の値を結果に(空シーケンスは null)。 */
	if ( seqLast == thNULL )
		seqLast = thNEW(pigDataNull,());
	front->set_result(seqLast);
	return rDO|FIN_START;
}

TS_STATE(FIN_START)
{
	return rDO|FIN_pigfSequence_START;
}

TS_STATE(FIN_pigfSequence_START)
{
	return rDO|FIN_pigfFunction_START;
}
