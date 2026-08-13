/*
 * pigfConst — 最小の非同期 helper(3-3a の async 機構検証用)。
 * ACT で front->set_result(定数 42)して終了するだけ。pigDataFunction<pigfConst> ノードを
 * compact すると、caller は preprocess で yield → pigfConst 終了(TSE_DESTROY)で再開 → 42 を得る。
 */
#include	"pig/c++/pigfFunction.h"
#include	"pig/c++/ptsApplication.h"   /* ptsApp 値メンバの完全型(ptsObject.h から移動・#3406 4.2) */
#include	"pig/c++/pigData.h"
#include	"_ts2/c++/pigfConst_.h"

CLASS_TINYSTATE(pig/c++/pigfConst,pig/c++/pigfFunction)


#if 0

TS_BEGIN_IMPLEMENT


class TS_THISCLASS : public TS_BASECLASS {
public:
	pigfConst_(
		sPtr<ptsObject> parent,
		sPtr<pigDataOperator> _front);

	sRptr<ptsObject,tinyState>		parent;
private:
protected:
	TS_DEFARGS
};

TS_END_IMPLEMENT

TS_BEGIN_INTERFACE
#include	"ts2/c++/sRptr.h"
class ptsObject;
class pigDataOperator;
TS_END_INTERFACE

#endif


pigfConst_::pigfConst_(TS_ARGS0)
        : pigfFunction_(parent,_front),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	STATE MACHINE
********************************************/

TS_STATE(INI_pigfFunction_START)   // pigfFunction の INI gate を上書き(初期化なし)
{
	return rDO|ACT_START;
}
TS_STATE(ACT_START)                // 結果を非同期にセット(定数 42)して終了
{
	front->set_result(thNEW(pigDataInteger,((INTEGER64)42)));
	return rDO|FIN_START;
}
TS_STATE(FIN_START)                // pigfFunction の FIN gate を上書き
{
	return rDO|FIN_pigfConst_START;
}
TS_STATE(FIN_pigfConst_START)
{
	return rDO|FIN_pigfFunction_START;
}
