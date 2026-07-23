/*
 * pigfMap — map(array, fn) の tinyState helper(pigfFunction 派生)。
 * 配列の各要素に lambda を適用し、**結果の配列を返す**(要素ごとに新しい pigfApply ノードを作る)。
 * fn は 1 引数 \(elem){…} または 2 引数 \(elem, index){…}(index=0..n-1)。
 *
 * 要素適用ノードは遅延(observation で compact)なので、fn が mesh op を作る場合は要素どうしが
 * 独立 → 並列に走る(union(配列) の葉と同じ)。layout(row/grid 等)はこの上に lambda で書ける。
 */
#include	"pig/c++/pigfFunction.h"
#include	"pig/c++/pigData.h"
#include	"pig/c++/pigfApply.h"
#include	"_ts2/c++/pigfMap_.h"

CLASS_TINYSTATE(pig/c++/pigfMap,pig/c++/pigfFunction)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	pigfMap_(
		sPtr<ptsObject> parent,
		sPtr<pigDataOperator> _front);

	sRptr<ptsObject,tinyState>		parent;
private:
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


pigfMap_::pigfMap_(TS_ARGS0)
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
	if ( args.length() < 2 ) {
		front->set_result(thNEW(pigDataError,("map: needs (array, function)", front->get_info())));
		return rDO|FIN_START;
	}
	sPtr<pigData> av = args[0]->compact();          /* 配列(async なら yield → 再走) */
	if ( av->is_error() ) { front->set_result(av); return rDO|FIN_START; }
	sPtr<pigDataArray> arr = sPtr<pigDataArray>::d_cast(av);
	if ( ! arr.is_notNull() ) {
		front->set_result(thNEW(pigDataError,("map: first argument must be an array", front->get_info())));
		return rDO|FIN_START;
	}
	sPtr<pigData> lv = args[1]->compact();           /* lambda 値(arity を見る) */
	if ( lv->is_error() ) { front->set_result(lv); return rDO|FIN_START; }
	sPtr<pigDataLambda> lam = sPtr<pigDataLambda>::d_cast(lv);
	if ( ! lam.is_notNull() ) {
		front->set_result(thNEW(pigDataError,("map: second argument must be a function", front->get_info())));
		return rDO|FIN_START;
	}
	int pc = lam->paramc();
	if ( pc != 1 && pc != 2 ) {
		front->set_result(thNEW(pigDataError,("map: function must take 1 (elem) or 2 (elem, index) params", front->get_info())));
		return rDO|FIN_START;
	}
	int n = arr->length();
	sPtr<pigDataArray> out = thNEW(pigDataArray,());
	for ( int i = 0 ; i < n ; ++i ) {
		sPtr<pigDataFunction<pigfApply> > app = thNEW(pigDataFunction<pigfApply>,());
		app->pushArg(lv);                                              /* callee = lambda 値 */
		app->pushArg(arr->get_ix(thNEW(pigDataInteger,((INTEGER64)i))));   /* elem */
		if ( pc == 2 )
			app->pushArg(thNEW(pigDataInteger,((INTEGER64)i)));       /* index */
		out->push(app);
	}
	front->set_result(out);
	return rDO|FIN_START;
}

TS_STATE(FIN_START)
{
	return rDO|FIN_pigfMap_START;
}

TS_STATE(FIN_pigfMap_START)
{
	return rDO|FIN_pigfFunction_START;
}
