/*
 * pigfMap — map(array, fn) の tinyState helper(pigfFunction 派生)。
 * 配列の各要素に lambda を適用し、**結果の配列を返す**(要素ごとに新しい pigfApply ノードを作る)。
 * fn は 1 引数 \(elem){…} または 2 引数 \(elem, index){…}(index=0..n-1)。
 *
 * 要素適用ノードは遅延(observation で compact)なので、fn が mesh op を作る場合は要素どうしが
 * 独立 → 並列に走る(union(配列) の葉と同じ)。layout(row/grid 等)はこの上に lambda で書ける。
 *
 * ★ 2026-08-11: 要素の解決を **ts2Parallel の worker** で行うように変更した。
 *   理由 = `pigDataArray::push` がエラー検査版になり、**要素が未解決なら compact ゲートで
 *   yield(sException)** するため。素の for で積んでいた旧実装は、yield で ACT_START が冒頭から
 *   再走 → ローカルの out/app を作り直す → 前回の結果が着かない、で **収束しなかった**
 *   (値 op を返す lambda、例 `map([box…], \(m){ volume(m); })` が無限ループ)。
 *   worker なら yield してもその worker だけが巻き戻るので、解決は前進する。
 */
#include	"pig/c++/pigfFunction.h"
#include	"pig/c++/ptsApplication.h"   /* ptsApp 値メンバの完全型(ptsObject.h から移動・#3406 4.2) */
#include	"pig/c++/pigData.h"
#include	"pig/c++/pigfApply.h"
#include	"ts2/c++/ts2Parallel.h"
#include	"ts2/c++/sArray.h"
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
	/* ★ 要素解決は worker で行うので、状態(apps/vals)は **メンバ**でなければならない。
	 * ローカルに置くと worker の yield による状態再走で作り直され、前進しない。 */
	sPtr<ts2Parallel>	par;
	sArray<sPtr<pigData> >	apps;    /* 要素ごとの適用ノード(pigfApply) */
	sPtr<pigDataArray>	out;     /* 結果配列。worker が **自分の添字位置へ** 書き込む */
	int			mapIdx;  /* worker への担当 index 配り(共有カウンタ) */
private:
};

TS_END_IMPLEMENT

TS_BEGIN_INTERFACE
#include	"ts2/c++/sRptr.h"
#include	"ts2/c++/sArray.h"
#include	"pig/c++/pigData.h"   /* sArray<sPtr<pigData> > メンバの完全型 */
class ptsObject;
class pigDataOperator;
class ts2Parallel;
TS_END_INTERFACE

#endif


pigfMap_::pigfMap_(TS_ARGS0)
        : pigfFunction_(parent,_front),
	  parent(tinyState_::parent)
{
    mapIdx = 0;
}


TS_STATE(INI_pigfFunction_START)
{
	return rDO|ACT_START;
}

TS_STATE(ACT_START)
{
	if ( args.length() < 2 ) {
		_front->set_result(thNEW(pigDataError,("map: needs (array, function)", _front->get_info())));
		return rDO|FIN_START;
	}
	sPtr<pigData> av = args[0]->compact();          /* 配列(async なら yield → 再走) */
	if ( av->is_error() ) { _front->set_result(av); return rDO|FIN_START; }
	sPtr<pigDataArray> arr = av->obt_array();
	if ( ! arr.is_notNull() ) {
		_front->set_result(thNEW(pigDataError,("map: first argument must be an array", _front->get_info())));
		return rDO|FIN_START;
	}
	sPtr<pigData> lv = args[1]->compact();           /* lambda 値(arity を見る) */
	if ( lv->is_error() ) { _front->set_result(lv); return rDO|FIN_START; }
	sPtr<pigDataLambda> lam = sPtr<pigDataLambda>::d_cast(lv);
	if ( ! lam.is_notNull() ) {
		_front->set_result(thNEW(pigDataError,("map: second argument must be a function", _front->get_info())));
		return rDO|FIN_START;
	}
	int pc = lam->paramc();
	if ( pc != 1 && pc != 2 ) {
		_front->set_result(thNEW(pigDataError,("map: function must take 1 (elem) or 2 (elem, index) params", _front->get_info())));
		return rDO|FIN_START;
	}
	int n = arr->length();
	/* 適用ノードを作る。
	 * ⚠ かつてここで `app->trigger()` を撃っていた。理由は並列性ではなく **helper の生成を
	 *   worker (ts2Parallel コルーチン = ptsObject でない) でやると parent が null になって
	 *   落ちる**ことの回避だった (実測: 外すと srava_map が SEGFAULT)。
	 * ★ #3419 (ひさ指示 2026-08-24): `pigDataFunction::_start` が **親を辿って最初の ptsObject を
	 *   実態親にする**ようにしたので、worker の中で helper を作っても env が正しく引ける。
	 *   ⇒ 事前 trigger は不要になった (撤去後も 1 波 8/8・ctest 289/289 を確認)。
	 * 並列性は下の worker が phase0 で次の兄弟を spawn することで出ている。 */
	apps.length(n);
	out = thNEW(pigDataArray,());
	for ( int i = 0 ; i < n ; ++i ) {
		sPtr<pigDataFunction<pigfApply> > app = thNEW(pigDataFunction<pigfApply>,());
		app->pushArg(lv);                                              /* callee = lambda 値 */
		app->pushArg(arr->get_ix(thNEW(pigDataInteger,((INTEGER64)i))));   /* elem */
		if ( pc == 2 )
			app->pushArg(thNEW(pigDataInteger,((INTEGER64)i)));       /* index */
		apps[i] = app;
	}

	/* 要素を worker で解決する。各 worker は:
	 *   phase0: 担当 idx を確定 → 次の兄弟 worker を即 spawn(pigfAgent SENDOP と同じ fan-out)
	 *   phase1: 担当要素を compact(未解決なら yield。**その worker だけ**が巻き戻る)
	 * ts2Parallel は _fn を worker 毎に複製するので idx/phase は per-worker。sException で
	 * 巻き戻っても mutable キャプチャは保たれるため phase0(spawn)は再実行されない。
	 * ★エラー/制御値(exit 等)を見つけた worker は **その場で _front に結果を入れて cancel** する
	 *   (ひさ設計)。他 worker が重い計算を続けても意味がないので即座に畳む。
	 *   `set_result(…,1)` = 既に result があれば上書きしない → **最初に見つけたエラーが勝つ**。
	 * ★`push` でなく **`set_ix(idx,…)`** で書く: worker は完了順に走るので push だと
	 *   `map` の契約 (f(a0),f(a1),… の順) が壊れる。set_ix は穴を pigDataNull で埋めて
	 *   d[ix] に置くので **添字位置が保たれる**。 */
	mapIdx = 0;
	par = thNEW(ts2Parallel,(ifThis, 0,
		[this, idx=-1, phase=0](sPtr<ts2Parallel> me, sPtr<stdEvent> wev) mutable -> int {
			if ( phase == 0 ) {
				if ( mapIdx >= apps.length() )
					return 1;                    /* 解決する要素なし */
				idx = mapIdx++;
				if ( mapIdx < apps.length() )
					me->spawn();                 /* 次の要素 worker を先に起こす */
				phase = 1;
			}
			if ( me->is_destroyed() )                /* 他 worker のエラーで畳まれた後に起きた */
				return 1;
			if ( phase == 1 ) {
				if ( apps[idx]->is_error() ) {   /* 未解決なら compact ゲートで yield 再走 */
					_front->set_result(apps[idx], 1);   /* 最初のエラーが勝つ */
					me->cancel();                      /* ★他 worker を止める */
					/* ★ worker を畳むだけでは **上流(pigfApply とその子 agent)は走り続ける**。
					 * pigData::destroy() で要らなくなった枝を名指しで止める(ひさ設計)。
					 * set_agentError は pigfAgent の登録簿しか起こさないので、これが
					 * agent 以外の helper に届く唯一の経路。 */
					for ( int i = 0 ; i < apps.length() ; ++i )
						if ( i != idx )
							apps[i]->destroy();
					return 1;
				}
				out->set_ix(thNEW(pigDataInteger,((INTEGER64)idx)), apps[idx]);
				phase = 2;
			}
			return 1;
		}));
	return ACT_pigfMap_COLLECT;   /* par の TSE_RETURN 待ち → rDO なし */
}

/* 全要素の解決完了(または エラーで cancel)。エラー時は worker が既に _front を解決済みなので、
 * flag=1 の set_result は**上書きしない**(= エラーがそのまま map の結果として上方へ伝播する)。 */
TS_STATE(ACT_pigfMap_COLLECT)
{
	if ( ! (ev->type == TSE_RETURN && ev->source == par) ) {
		/* ★ destroy の作法 (pigfSystem ACT_pigfSystem_WAIT と同型・ひさ指示 2026-08-11):
		 * 自分が destroy されたら子 (par) へ destroy を送り、TSE_RETURN が戻るのを **待ち続ける**。
		 * 即 FIN すると worker が残る。 */
		if ( is_destroyed() ) {
			if ( par.is_notNull() ) { par->destroy(); return 0; }
			return rDO|FIN_START;
		}
		return 0;
	}
	_front->set_result(out, 1);
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
