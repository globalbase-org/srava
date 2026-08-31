/*
 * pigfArrayFold — `union(arr)` / `intersection(arr)` / `combine(arr)` の tinyState helper。
 * 単一引数が **mesh の配列** のとき、評価時(配列長が確定してから)に **n 項ノード**へ畳む。
 * ★ #3436 P4: 木への分解自体は pigfModuleAgent::try_decompose (dispatch 時) が行う。
 *
 * 狙い(ユーザ案 2026-06-10): `pat = pat ||| x` の直列 fold は各 ||| が前段に依存して **直列化**する。
 * `union(配列)` を均衡二分木にすると独立サブツリーが **並列**に走り、段数も N から log2 N へ減る。
 * パース時は配列長が動的で木を組めないが、**評価時**なら配列が具体値になっており木を作れる。
 *
 * 振る舞い(_front->get_op_name() = union/intersection/combine):
 *   - 引数が配列(n 要素): 均衡二分木 pigDataFunction<pigfModuleAgent>(op) を作り _front を木の根に解決。
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
}


/* ★ #3436 P4 (2026-08-25): ここで木を組むのをやめ、**n 項ノードを 1 つ**作る。
 * 分解 (何項ずつの木にするか) は pigfModuleAgent::try_decompose が dispatch 時に行う
 * (docs/sig_grammar_design.md §5.5)。
 * ★ 副産物: union([a,b,c]) と union(a,b,c) が **同じ n 項ノード**に正準化され、同じ木・同じ
 *   中間キャッシュを共有するようになった (旧実装はここが「ソートしない中央分割」で、
 *   パーサ側の「ソートして中央分割」と別の木を作っていた = §5.3 ②)。
 * info は union(配列) 呼び出しの位置を伝播 (agentwatch/ps で行番号が出るように)。 */
static sPtr<pigData>
build_nary(sPtr<stdString> op, sPtr<pigInfo> info, sArray<sPtr<pigData> >& e, int n)
{
	sPtr<pigDataFunction<pigfModuleAgent> > f = thNEW(pigDataFunction<pigfModuleAgent>,());
	for ( int i = 0 ; i < n ; ++i )
		f->pushArg(e[i]);
	f->set_op_name(op);
	f->set_out_cache(1);
	f->set_info(info);
	return f;
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
		_front->set_result(thNEW(pigDataNull,()));
		return rDO|FIN_START;
	}
	/* 単一引数を解決。配列なら木へ、mesh ならそのまま、空コレクションなら {}(単位元)。
	 * compact は async なら yield→ACT_START 再走(set_result はこの後でしか起きないので冪等)。 */
	sPtr<pigData> av = args[0]->compact();
	if ( av->is_error() ) { _front->set_result(av); return rDO|FIN_START; }

	sPtr<pigDataArray> arr = av->obt_array();
	if ( ! arr.is_notNull() ) {        /* 配列でない = 単一 mesh: union(m)=m */
		_front->set_result(args[0]);
		return rDO|FIN_START;
	}
	int n = arr->length();
	if ( n == 0 ) {                    /* 空コレクションの reduce = 単位元 {} */
		_front->set_result(thNEW(pigDataHash,()));
		return rDO|FIN_START;
	}
	sArray<sPtr<pigData> > e;
	e.length(n);
	for ( int i = 0 ; i < n ; ++i )
		e[i] = arr->get_ix(thNEW(pigDataInteger,((INTEGER64)i)));   /* 要素ノードはそのまま(遅延評価) */
	if ( n == 1 ) {                    /* 要素 1 個: そのまま */
		_front->set_result(e[0]);
		return rDO|FIN_START;
	}
	_front->set_result(build_nary(_front->get_op_name(), _front->get_info(), e, n));   /* n 項ノードに解決 → dispatch 時に分解 */
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
