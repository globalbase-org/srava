/*
 * ptsAgent — 演算を実行する実行体の基底 (#3406 段階 4.2, docs/mediator_design.md §2.5)。
 *
 * 用語 (2026-0727 メモ §1.2): 「agent」= オペレータを実際に実行するクラス。planner 内 thread でも
 * agent process 内でも動く。従来 planner と対比して「agent」と呼んでいたプロセスは「agent process」。
 *
 * 親オブジェクトは **ptsObject** (2026-08-02 メモ §1)。かつては ptsMediator 限定だったが、
 * それは実行体が `parent->a_write()` を直接叩いていたためで、§5/§6 で結果の返し方が
 * 「set_result してから FIN で TSE_RETURN を 1 回」に変わり、その制約は消えた。
 * 相手 (planner) への送信は自分で pipe を持たず、着信は parent からの TSE_PACKET として受ける。
 * これにより同じ派生クラス (cgatsAgent / mfatsAgent) が
 *   - agent process 内 : parent = ptsAgentApplication (pigwire レコードで stdin/stdout へ)
 *   - planner 内 thread: parent = ptsMediatorInternal (pigData を sPtr のまま直渡し・4.3)
 * のどちらでも無改造で動く。実行体はどちらの型も知らない (parent は ptsObject として持つだけ)。
 *
 * 派生用 gate (ptsApplication と同じ流儀):
 *   INI: INI_ptsObject_START → INI_ptsAgent_START (派生が上書きして配線) → ACT_START
 *   FIN: 派生は FIN_START → FIN_ptsAgent_START → FIN_ptsObject_START と畳む
 *
 * NB: ptsApplication ではなく ptsObject 派生であることが要点。ptsApplication は INI で
 *     ptsApp=自分 を立てる「プロセスの実態元祖」なので、planner 内 thread として起動すると
 *     planner の ptsApp を潰してしまう (4.3 が成立しない)。ptsAgent は ptsObject の
 *     INI_START で親から ptsApp を継承するだけ = プロセス内に何個居てもよい。
 */
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/ptsApplication.h"   /* ptsApp 値メンバの完全型(ptsObject.h から移動・#3406 4.2) */
#include	"ts2/c++/stdEvent.h"
#include	"_ts2/c++/ptsAgent_.h"

CLASS_TINYSTATE(pig/c++/ptsAgent,pig/c++/ptsObject)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ptsAgent_(
		sPtr<ptsObject> parent);

	sRptr<ptsObject,tinyState>		parent;

	/* ★ 実行結果 (2026-08-02 メモ §2.2.2)。派生は計算が終わったら set_result() して FIN へ抜けるだけ。
	 * pigwire (A_SAVE_BEGIN/DONE/BYE/番兵) の組み立ては親 (ptsAgentApplication / ptsMediatorInternal)
	 * の仕事になったので、実行体はワイヤを一切知らない。
	 *   セットするのは **C_ARG_END で渡された pigDataCache** か **pigDataError** のいずれかだけ。 */
	void	set_result(sPtr<pigData> r);
protected:
	sPtr<pigData>	agentResult;
	int		retSent;      /* TSE_RETURN を必ず 1 回・2 回以上送らない (§1) */

protected:
private:
	TS_DEFARGS
};

TS_END_IMPLEMENT

TS_BEGIN_INTERFACE
#include	"ts2/c++/sRptr.h"
class ptsObject;
TS_END_INTERFACE

#endif


ptsAgent_::ptsAgent_(TS_ARGS0)
        : ptsObject_(parent),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
    retSent = 0;
}


/*******************************************
	STATE MACHINE
********************************************/

TS_STATE(INI_ptsObject_START)   /* 派生用 gate を 1 段挟む (ptsApplication と同じ流儀) */
{
	return rDO|INI_ptsAgent_START;
}

TS_STATE(INI_ptsAgent_START)    /* 派生 (cgatsAgent 等) がここを上書きして配線する */
{
	return rDO|ACT_START;
}

void
ptsAgent_::set_result(sPtr<pigData> r)
{
	agentResult = r;
}

TS_STATE(FIN_ptsAgent_START)    /* 派生用 FIN gate (派生は FIN_START からここへ畳む) */
{
	/* 終了を parent(Mediator)へ通知する。agent process では root(ptsApplication)がこれを受けて
	 * 自分も畳む = プロセス終了(旧構成では実行体自身が root だったので FIN がそのまま終了だった)。
	 * 4.3 の ptsMediatorInternal でも「実行体が終わった」の受け口はここになる。
	 * 通知の流儀は ptsCalcBody::FIN_START / ptsWirePipe::FIN と同じ(parent へ TSE_RETURN)。 */
	/* ★ 結果 (pigDataCache / pigDataError) を載せて **必ず 1 回だけ** 返す (§1)。
	 * destroy 等どの理由で畳まれてもここを通るので、親は「TSE_RETURN が来ない」を心配しなくてよい。 */
	if ( ! retSent ) {
		retSent = 1;
		if ( parent.is_notNull() )
			parent->eventHandler(thNEW(stdEvent,(TSE_RETURN, ifThis, agentResult)));
	}
	agentResult = thNULL;   /* §9: 終了時点で手放す */
	return rDO|FIN_ptsObject_START;
}
