/*
 * pigfArrayFold — `union(arr)` / `intersection(arr)` / `combine(arr)` の tinyState helper。
 * 単一引数が **mesh の配列** のとき、評価時(配列長が確定してから)に均衡二分木へ分解して一気に畳む。
 *
 * 狙い(ユーザ案 2026-06-10): `pat = pat ||| x` の直列 fold は各 ||| が前段に依存して **直列化**し遅い
 * (実測 36 円で 12.6s)。`union(配列)` を均衡二分木にすると独立サブツリーが **並列**に走る(同 1.2s ≈ 10x)。
 * パース時は配列長が動的で木を組めないが、**評価時**なら配列が具体値になっており木を作れる。
 *
 * 振る舞い(front->get_op_name() = union/intersection/combine):
 *   - 引数が配列(n 要素): 均衡二分木 pigDataFunction<pigfModuleAgent>(op) を作り front を木の根に解決。
 *     根を compact すると葉(独立な二項 op)が並列に走る(mesh-DAG 継続のパイプライン)。
 *   - 引数が単一 mesh: union(m)=m。そのまま返す。
 *   - 空コレクション []: {}(fold 単位元)を返す。
 * 木の葉/中間は通常の mesh op なので、dedup・キャッシュ・[] 単位元短絡も従来どおり効く。
 */
#include	"pig/c++/pigfFunction.h"
#include	"pig/c++/ptsApplication.h"   /* ptsApp 値メンバの完全型(ptsObject.h から移動・#3406 4.2) */
#include	"pig/c++/pigData.h"
#include	"pig/c++/pigfModuleAgent.h"   /* 木の節点 = pigDataFunction<pigfModuleAgent> */
#include	"ts2/c++/stdString.h"
#include	"ts2/c++/sArray.h"
#include	"_ts2/c++/pigfArrayFold_.h"

CLASS_TINYSTATE(pig/c++/pigfArrayFold,pig/c++/pigfFunction)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	pigfArrayFold_(
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


pigfArrayFold_::pigfArrayFold_(TS_ARGS0)
        : pigfFunction_(parent,_front),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/* 均衡二分木(可換 op): elems[lo,hi) を二項 mesh op の木へ。葉は配列要素ノードそのまま。
 * info は union(配列) 呼び出しの位置を全ノードに伝播(agentwatch/ps で行番号が出るように)。 */
static sPtr<pigData>
build_tree(sPtr<stdString> op, sPtr<pigInfo> info, sArray<sPtr<pigData> >& e, int lo, int hi)
{
	if ( hi - lo == 1 )
		return e[lo];
	int mid = (lo + hi) / 2;
	sPtr<pigDataFunction<pigfModuleAgent> > n = thNEW(pigDataFunction<pigfModuleAgent>,());
	n->pushArg(build_tree(op, info, e, lo, mid));
	n->pushArg(build_tree(op, info, e, mid, hi));
	n->set_op_name(op);
	n->set_out_cache(1);
	n->set_info(info);
	return n;
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
	if ( args.length() < 1 ) {
		front->set_result(thNEW(pigDataNull,()));
		return rDO|FIN_START;
	}
	/* 単一引数を解決。配列なら木へ、mesh ならそのまま、空コレクションなら {}(単位元)。
	 * compact は async なら yield→ACT_START 再走(set_result はこの後でしか起きないので冪等)。 */
	sPtr<pigData> av = args[0]->compact();
	if ( av->is_error() ) { front->set_result(av); return rDO|FIN_START; }

	sPtr<pigDataArray> arr = av->obt_array();
	if ( ! arr.is_notNull() ) {        /* 配列でない = 単一 mesh: union(m)=m */
		front->set_result(args[0]);
		return rDO|FIN_START;
	}
	int n = arr->length();
	if ( n == 0 ) {                    /* 空コレクションの reduce = 単位元 {} */
		front->set_result(thNEW(pigDataHash,()));
		return rDO|FIN_START;
	}
	sArray<sPtr<pigData> > e;
	e.length(n);
	for ( int i = 0 ; i < n ; ++i )
		e[i] = arr->get_ix(thNEW(pigDataInteger,((INTEGER64)i)));   /* 要素ノードはそのまま(遅延評価) */
	if ( n == 1 ) {                    /* 要素 1 個: そのまま */
		front->set_result(e[0]);
		return rDO|FIN_START;
	}
	front->set_result(build_tree(front->get_op_name(), front->get_info(), e, 0, n));   /* 木の根に解決 → compact で並列起動 */
	return rDO|FIN_START;
}

TS_STATE(FIN_START)
{
	return rDO|FIN_pigfArrayFold_START;
}

TS_STATE(FIN_pigfArrayFold_START)
{
	return rDO|FIN_pigfFunction_START;
}
