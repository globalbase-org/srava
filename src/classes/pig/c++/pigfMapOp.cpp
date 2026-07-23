/*
 * pigfMapOp — 単項 mesh 変換演算子(translate/mirror/scale/rotate = `>>> <> *** @`)を **配列対応** にする
 *   tinyState helper(pigfFunction 派生)。args[0]=mesh(役割), args[1..]=パラメータ(役割)。
 *
 * 評価時に形を見て展開(深さ規則):
 *   - mesh 役(arg0): 配列 `[]` なら **コンテナ**(各要素に map)、単一 mesh なら leaf(共有)。
 *   - param 役(arg1..): **ネスト配列**(要素が配列/mesh)なら zip コンテナ、**flat 数値配列(=ベクトル)** や
 *     スカラ/文字列は leaf(broadcast で共有)。→ 角度配列での要素別回転などは曖昧なので map で明示。
 *   - コンテナが無ければ従来どおり単一 op。あれば各コンテナを index、leaf を共有して **配列**を返す。
 *
 *   meshArr >>> v          broadcast  → 配列
 *   mesh    >>> [[..],..]  instancing → 配列(1個を各位置へ複製)
 *   meshArr >>> [[..],..]  zip(長さ一致) → 配列
 * reduce しない(形を保つ)。まとめたいなら union(配列)。各要素は遅延=並列。
 */
#include	"pig/c++/pigfFunction.h"
#include	"pig/c++/pigData.h"
#include	"cg/c++/pigfSravaAgent.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/pigfMapOp_.h"

CLASS_TINYSTATE(pig/c++/pigfMapOp,pig/c++/pigfFunction)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	pigfMapOp_(
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


pigfMapOp_::pigfMapOp_(TS_ARGS0)
        : pigfFunction_(parent,_front),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}

/* 単一 op ノード(従来形)を作る: pigDataFunction<pigfSravaAgent>(op_name, args)。 */
static sPtr<pigData>
mk_single(sPtr<stdString> op, sPtr<pigInfo> info, sArray<sPtr<pigData> >& a, int n)
{
	sPtr<pigDataFunction<pigfSravaAgent> > f = thNEW(pigDataFunction<pigfSravaAgent>,());
	for ( int i = 0 ; i < n ; ++i )
		f->pushArg(a[i]);
	f->set_op_name(op);
	f->set_out_cache(1);
	f->set_info(info);
	return f;
}

TS_STATE(INI_pigfFunction_START)
{
	return rDO|ACT_START;
}

TS_STATE(ACT_START)
{
	int n = args.length();
	if ( n < 1 ) { front->set_result(thNEW(pigDataNull,())); return rDO|FIN_START; }
	sPtr<stdString> op = front->get_op_name();
	sPtr<pigInfo>   info = front->get_info();

	/* 各 arg を「コンテナ(配列)か leaf か」に分類。arg0=mesh 役(配列ならコンテナ)、
	 * arg1..=param 役(ネスト配列ならコンテナ / flat 数値配列やスカラは leaf)。 */
	sPtr<pigDataArray> cont[8];   /* コンテナなら配列実体、leaf なら null */
	int N = -1;
	for ( int i = 0 ; i < n && i < 8 ; ++i ) {
		sPtr<pigData> v = args[i]->compact();
		if ( v->is_error() ) { front->set_result(v); return rDO|FIN_START; }
		sPtr<pigDataArray> arr = sPtr<pigDataArray>::d_cast(v);
		int isCont = 0;
		if ( arr.is_notNull() ) {
			if ( i == 0 ) {
				isCont = 1;                         /* mesh 役の配列 = コンテナ */
			} else if ( arr->length() > 0 ) {
				/* param 役: 要素[0]が配列なら zip コンテナ(ネスト)、数値なら leaf(=ベクトル) */
				sPtr<pigData> e0 = arr->get_ix(thNEW(pigDataInteger,((INTEGER64)0)))->compact();
				if ( sPtr<pigDataArray>::d_cast(e0).is_notNull() )
					isCont = 1;
			}
		}
		if ( isCont ) {
			cont[i] = arr;
			if ( N < 0 ) N = arr->length();
			else if ( N != arr->length() ) {
				front->set_result(thNEW(pigDataError,("array op: length mismatch (zip)", info)));
				return rDO|FIN_START;
			}
		}
	}

	if ( N < 0 ) {                       /* コンテナなし = 従来どおり単一 op */
		front->set_result(mk_single(op, info, args, n));
		return rDO|FIN_START;
	}

	/* コンテナを index、leaf を共有して N 要素の配列を作る。 */
	sPtr<pigDataArray> out = thNEW(pigDataArray,());
	for ( int k = 0 ; k < N ; ++k ) {
		sArray<sPtr<pigData> > per;
		per.length(n);
		for ( int i = 0 ; i < n ; ++i ) {
			if ( i < 8 && cont[i].is_notNull() )
				per[i] = cont[i]->get_ix(thNEW(pigDataInteger,((INTEGER64)k)));
			else
				per[i] = args[i];
		}
		out->push(mk_single(op, info, per, n));
	}
	front->set_result(out);
	return rDO|FIN_START;
}

TS_STATE(FIN_START)
{
	return rDO|FIN_pigfMapOp_START;
}

TS_STATE(FIN_pigfMapOp_START)
{
	return rDO|FIN_pigfFunction_START;
}
