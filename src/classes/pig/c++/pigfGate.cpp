/*
 * pigfGate — gate(inp1, inp2) の tinyState helper(pigfFunction 派生)。
 * inp1 を **そのまま front の値にして即返し**(計算は変えない pass-through)、その後 helper が
 * 生き続けて inp1 の **実際の計算完了**(継続 cdr->cdr の解決)を待ち、完了時に inp2 を trigger する。
 * 「ある計算の完了フック/通知」用:  union(gate(box1, print("done")), box2) → box1 完了時に表示。
 * pigfAgent と違いエージェント・promise・admission gate を持たない薄いヘルパ。鍵は inp1 の
 * 「起動(compact ok)」と「完了(cdr->cdr ok)」の時間ギャップを mid-life 継続で跨ぐこと。
 *   ACT_START : v=inp1.compact() → front.set_result(v)(mid-life) → ACT_WAIT
 *   ACT_WAIT  : v が "delayed" 継続なら v.cdr().cdr().compact() で完了待ち → ACT_FIRE
 *   ACT_FIRE  : inp2.trigger()(側効果のみ・値は捨てる) → FIN
 */
#include	"pig/c++/pigfFunction.h"
#include	"pig/c++/ptsApplication.h"   /* ptsApp 値メンバの完全型(ptsObject.h から移動・#3406 4.2) */
#include	"pig/c++/pigData.h"
#include	"_ts2/c++/pigfGate_.h"

CLASS_TINYSTATE(pig/c++/pigfGate,pig/c++/pigfFunction)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	pigfGate_(
		sPtr<ptsObject> parent,
		sPtr<pigDataOperator> _front);

	sRptr<ptsObject,tinyState>		parent;
	sPtr<pigData>				gateVal;   /* inp1 の継続(set_result 済み)。完了待ちに使う */
private:
	TS_DEFARGS
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


pigfGate_::pigfGate_(TS_ARGS0)
        : pigfFunction_(parent,_front),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


TS_STATE(INI_pigfFunction_START)
{
	return rDO|ACT_START;
}

TS_STATE(ACT_START)
{
	if ( args.length() < 1 ) {
		front->set_result(thNEW(pigDataNull,()));
		return rDO|FIN_START;
	}
	sPtr<pigData> v = args[0]->compact();   /* inp1 → 継続(未解決なら yield→再走) */
	front->set_result(v, 1);                /* inp1 を即返す(mid-life・以後 helper は生き続けて完了を待つ) */
	gateVal = v;
	return rDO|ACT_WAIT;
}

TS_STATE(ACT_WAIT)
{
	/* inp1 が mesh 継続("delayed")なら実完了(cdr->cdr=A_SAVE_BEGIN で解決)まで待つ。
	 * 値返し op やエラーは即完了扱い。helper 自身が promise に listen して yield→再走で待つ。 */
	if ( ! gateVal->is_error() && pig_is_delayed(gateVal) )
		gateVal->cdr()->cdr()->compact();
	return rDO|ACT_FIRE;
}

TS_STATE(ACT_FIRE)
{
	if ( args.length() >= 2 )
		args[1]->trigger();                 /* 完了時に inp2 を起動(側効果・値は捨てる) */
	return rDO|FIN_START;
}

TS_STATE(FIN_START)
{
	return rDO|FIN_pigfGate_START;
}

TS_STATE(FIN_pigfGate_START)
{
	return rDO|FIN_pigfFunction_START;
}
