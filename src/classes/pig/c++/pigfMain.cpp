/*
 * pigfMain — 3-3b テストドライバ。tsApplication 直下、自前の root env を持つ ptsObject。
 * INI で「x = (2+3)」の Assign ノードと「x」読み出し Variable ノードを組む。
 * ACT で Assign を compact(初回 async yield)→ 再開後 Variable を compact。
 *   - 代入時に (2+3) は評価されない(遅延束縛)
 *   - x を参照(get_int)した時に初めて 2+3=5 が評価される
 */
#include	"ts2/c++/tinyState.h"
#include	"pig/c++/ptsApplication.h"   /* ptsApp 値メンバの完全型(ptsObject.h から移動・#3406 4.2) */
#include	"pig/c++/pigData.h"
#include	"pig/c++/pigfAssign.h"     // pigDataFunction<pigfAssign> のインスタンス化
#include	"_ts2/c++/pigfMain_.h"

CLASS_TINYSTATE(pig/c++/pigfMain,pig/c++/ptsObject)


#if 0

TS_BEGIN_IMPLEMENT


class TS_THISCLASS : public TS_BASECLASS {
public:
	pigfMain_(
		sPtr<tinyState> parent);

	sRptr<tinyState,tinyState>		parent;

	virtual sPtr<pigEnvironment>	get_env();   // root env(Assign が継承 / Variable が参照)
protected:
	sPtr<pigEnvironment>	env;
	sPtr<pigData>		assignNode;
	sPtr<pigData>		varNode;
private:
	TS_DEFARGS
};

TS_END_IMPLEMENT

TS_BEGIN_INTERFACE
#include	"ts2/c++/sRptr.h"
class tinyState;
class pigData;
class pigEnvironment;
TS_END_INTERFACE

#endif


pigfMain_::pigfMain_(TS_ARGS0)
        : ptsObject_(parent),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}

sPtr<pigEnvironment>
pigfMain_::get_env()
{
	return env;
}


/*******************************************
	STATE MACHINE
********************************************/

TS_STATE(INI_ptsObject_START)   // root env + DAG ノード構築
{
	env = thNEW(pigEnvironment,(thNULL));

	// (2+3): pigDataOperatorAdd(遅延)
	sPtr<pigDataOperatorAdd> add = thNEW(pigDataOperatorAdd,());
	add->pushArg(thNEW(pigDataInteger,((INTEGER64)2)));
	add->pushArg(thNEW(pigDataInteger,((INTEGER64)3)));

	// var x = (2+3)   → 定義(DEF)
	sPtr<pigDataFunction<pigfAssign> > a = thNEW(pigDataFunction<pigfAssign>,());
	a->pushArg(thNEW(pigDataString,("x")));
	a->pushArg(add);
	a->set_mode(PIG_ASSIGN_DEF);
	assignNode = a;

	// Variable("x")
	sPtr<pigDataOperatorVariable> v = thNEW(pigDataOperatorVariable,());
	v->pushArg(thNEW(pigDataString,("x")));
	varNode = v;

	return rDO|ACT_START;
}
TS_STATE(ACT_START)
{
	assignNode->compact();                  // 文の実行(Sequence の代わり)。初回 async yield、x を遅延束縛
	// varNode->get_int() の中で compact が走る(明示 compact は不要)。参照=評価 → 5
	::printf("[pigf] x = %lld (expect 5; assigned lazily, evaluated on read)\n",
	         (long long)varNode->get_int());
	return rDO|FIN_START;
}
TS_STATE(FIN_START)
{
	return rDO|FIN_ptsObject_START;
}
