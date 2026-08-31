/*
 * pigfIf — 条件分岐(if/else)の tinyState helper(pigfFunction 派生)。
 * args[0]=条件, args[1]=then 文, args[2]=else 文(省略可)。
 * 条件を get_bool()(compact を兼ねる)で評価し、取られた枝のノードを **compact せず** result に。
 * (枝の評価=副作用は、result が観測された時に遅延的に起きる。取られない枝は評価しない。)
 * 注: if は一度だけ評価されるので memo 化と衝突しない。while/for(再評価)は 2.4。
 */
#include	"pig/c++/pigfFunction.h"
#include	"pig/c++/osglue.h"   /* osglue_env_int (#3419 §17.2) */
#include	"pig/c++/ptsApplication.h"   /* ptsApp 値メンバの完全型(ptsObject.h から移動・#3406 4.2) */
#include	"pig/c++/pigData.h"
#include	"_ts2/c++/pigfIf_.h"

CLASS_TINYSTATE(pig/c++/pigfIf,pig/c++/pigfFunction)


#if 0

TS_BEGIN_IMPLEMENT


class TS_THISCLASS : public TS_BASECLASS {
public:
	pigfIf_(
		sPtr<ptsObject> parent,
		sPtr<pigDataOperator> _front);

	sRptr<ptsObject,tinyState>		parent;
	int		ifDestroyed;   /* 駆動中の枝へ destroy を転送済み(1 回だけ) */
private:
protected:
};

TS_END_IMPLEMENT

TS_BEGIN_INTERFACE
#include	"ts2/c++/sRptr.h"
class ptsObject;
class pigDataOperator;
TS_END_INTERFACE

#endif


pigfIf_::pigfIf_(TS_ARGS0)
        : pigfFunction_(parent,_front),
	  parent(tinyState_::parent)
{
    ifDestroyed = 0;
}


/*******************************************
	STATE MACHINE
********************************************/

TS_STATE(INI_pigfFunction_START)
{
	return rDO|ACT_START;
}

TS_STATE(ACT_START)
{
	/* ★ destroy の転送 (ひさ設計 2026-08-11)。cond / 取られた枝のどちらで待っているかは
	 * ここでは分からないが、**未起動ノードへの destroy は no-op** (helper も result も無い)ため
	 * 自分の args を一巡して送れば「走っている枝だけ」が畳まれる。1 度だけ。 */
	if ( is_destroyed() && ! ifDestroyed ) {
		ifDestroyed = 1;
		if ( osglue_env_int("PIG_DBG_TD", 0) ) ::fprintf(stderr, "[td] if: destroy 転送\n");
		for ( int di = 0 ; di < args.length() ; ++di )
			if ( args[di].is_notNull() ) args[di]->destroy();
	}
	if ( args.length() < 2 ) {           /* 文法上ありえないが安全に */
		_front->set_result(thNEW(pigDataNull,()));
		return rDO|FIN_START;
	}
	if ( args[0]->is_error() ) {         /* 条件評価エラー → 伝播 */
		_front->set_result(args[0]);
		return rDO|FIN_START;
	}
	/* 取られた枝を compact して値を返す(エラーでも実結果でも「その枝の値」= 同じものを返す)。
	 * 取られない枝には触れない。compact は async 枝で yield しうるが seqLast 同様再走で前進。 */
	if ( args[0]->get_bool() )
		_front->set_result(args[1]->compact());
	else
		_front->set_result( ( args.length() >= 3 ) ? args[2]->compact()
		                                          : sPtr<pigData>(thNEW(pigDataNull,())) );
	return rDO|FIN_START;
}

TS_STATE(FIN_START)
{
	return rDO|FIN_pigfIf_START;
}

TS_STATE(FIN_pigfIf_START)
{
	return rDO|FIN_pigfFunction_START;
}
