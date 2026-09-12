/*
 * ppaCompute — pipe_proximity in-proc 実行体の計算本体 (ptsCalcBody 派生・値返し)。
 *   .so 化 Phase 5 (docs §5)。mfaVolume 等の値返し calc body のミラー。
 *
 * 1 つの calc body が pipe_proximity の 5 _op (検出/調整/scene 検出/scene 調整/サンプル) を捌くので、
 *   ptsCalcBody の (parent, args, target) に加えて **_op 名** を ctor で受け取り、TS_THREAD の
 *   compute() で pp_compute(_op, *args) を回す。pp_compute は純 pigData 境界 (process 版 serve と共有)。
 *   重い調整計算 (pipe_adjust の反復) は基底 ptsCalcBody が専用 thread で走らせるので、planner の
 *   協調スケジューラ (ptsMediatorInternal 経路) をブロックしない。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"ts2/c++/stdString.h"
#include	"pipe_proximity_compute.h"   /* pp_compute (計算本体・marshaling) */
#include	"_ts2/c++/ppaCompute_.h"

CLASS_TINYSTATE(pipe/c++/ppaCompute,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ppaCompute_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target,
		sPtr<stdString> _op);

	sRptr<ptsObject,tinyState>		parent;
protected:
	virtual void	compute();
private:
	TS_DEFARGS
};

TS_END_IMPLEMENT

TS_BEGIN_INTERFACE
#include	"ts2/c++/sRptr.h"
#include	"ts2/c++/sArray.h"
#include	"ts2/c++/stdString.h"
class ptsObject;
class pigData;
class stdString;
TS_END_INTERFACE

#endif


ppaCompute_::ppaCompute_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

/* 基底 ptsCalcBody の TS_THREAD(ACT_START) から呼ばれる (専用 thread)。args は親 ppatsAgent が
 * 所有・寿命中生存 (ポインタ渡し)。結果 pigData は基底 get_result() が返す (値返しは result のまま)。 */
void
ppaCompute_::compute()
{
	const char *opn = ( _op.is_notNull() ) ? _op->get_str() : "pipe_proximity";
	result = pp_compute(opn, *args);
}
