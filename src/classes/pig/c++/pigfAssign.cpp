/*
 * pigfAssign — 代入(変数定義+束縛)の tinyState helper。
 * args[0] = 変数名(テキスト)。args[1] = 代入値。
 * 肝: 変数名 args[0] も代入値 args[1] も compact してから束縛する。**両方 is_error を
 *     見る** (右辺のエラーは束縛せずその場で伝播・2026-09-04)。戻り値は args[0](変数名)
 *     ── args[1] を返すと呼び元(pigfSequence 等)が再度エラー判定で compact してしまうため。
 *     幾何は compact() で継続 pair(promise→pigDataCache) に解決されるので、右辺を見ても
 *     遅延は縮退しない。
 */
#include	"pig/c++/pigfFunction.h"
#include	"pig/c++/osglue.h"   /* osglue_env_int (#3419 §17.2) */
#include	"pig/c++/ptsApplication.h"   /* ptsApp 値メンバの完全型(ptsObject.h から移動・#3406 4.2) */
#include	"pig/c++/pigData.h"
#include	<stdio.h>
#include	"_ts2/c++/pigfAssign_.h"

CLASS_TINYSTATE(pig/c++/pigfAssign,pig/c++/pigfFunction)


#if 0

TS_BEGIN_IMPLEMENT


class TS_THISCLASS : public TS_BASECLASS {
public:
	pigfAssign_(
		sPtr<ptsObject> parent,
		sPtr<pigDataOperator> _front);

	sRptr<ptsObject,tinyState>		parent;
	int		asgDestroyed;   /* rhs へ destroy を転送済み(1 回だけ) */
	sPtr<pigData>	asgErr;         /* 分割代入のエラー(あれば FIN でこれを返す) */
private:
protected:
};

TS_END_IMPLEMENT

TS_BEGIN_INTERFACE
#include	"ts2/c++/sRptr.h"
class ptsObject;
class pigDataOperator;
TS_END_INTERFACE

#endif


pigfAssign_::pigfAssign_(TS_ARGS0)
        : pigfFunction_(parent,_front),
	  parent(tinyState_::parent)
{
    asgDestroyed = 0;
    asgErr = thNULL;
}


/*******************************************
	STATE MACHINE
********************************************/

TS_STATE(INI_pigfFunction_START)   // pigfFunction の INI gate を上書き(初期化なし)
{
	return rDO|ACT_START;
}
TS_STATE(ACT_START)
{
	/* ★ destroy の転送 (ひさ設計 2026-08-11)。rhs (args[1]) を compact しているのは自分なので、
	 * 撤収要求が来たらそこへ送る (未起動なら no-op)。1 度だけ。 */
	if ( is_destroyed() && ! asgDestroyed ) {
		asgDestroyed = 1;
		if ( osglue_env_int("PIG_DBG_TD", 0) ) ::fprintf(stderr, "[td] assign: destroy 転送\n");
		if ( args.length() >= 2 && args[1].is_notNull() ) args[1]->destroy();
	}
	if ( args[0]->is_error() )         // 変数名の評価でエラー → 伝播
		return rDO|FIN_START;
	/* ★ 分割代入 `var [a,b,c] = 式;` (mode=PIG_ASSIGN_DEF_LIST)。
	 * args[0] = 名前の配列 / args[1] = 右辺。右辺を **この地点で compact** して(DEF と同じ
	 * レキシカルスコープの理由)配列にし、要素 0,1,2,… を順に def_var する。
	 * 要素が足りなければエラー。余りは無視する(「N 個返す op の先頭 k 個を取る」用途)。 */
	if ( sPtr<pigDataFunction_b>::d_cast(_front)->get_mode() == PIG_ASSIGN_DEF_LIST ) {
		sPtr<pigDataArray> names = args[0]->obt_array();
		sPtr<pigData>      rv    = ( args.length() >= 2 ) ? args[1]->compact()
		                                                 : sPtr<pigData>(thNEW(pigDataNull,()));
		if ( rv->is_error() ) { asgErr = rv; return rDO|FIN_START; }
		sPtr<pigDataArray> vals = rv->obt_array();
		if ( ! names.is_notNull() ) {         /* 起こらない想定(文法が配列を作る) */
			asgErr = thNEW(pigDataError,("destructuring: bad name list", _front->get_info()));
			return rDO|FIN_START;
		}
		if ( ! vals.is_notNull() ) {
			asgErr = thNEW(pigDataError,("destructuring assignment needs an array on the right-hand side",
			                             _front->get_info()));
			return rDO|FIN_START;
		}
		if ( vals->length() < names->length() ) {
			char buf[128];
			::snprintf(buf, sizeof(buf), "destructuring: need %d element(s) but got %d",
			           names->length(), vals->length());
			asgErr = thNEW(pigDataError,(buf, _front->get_info()));
			return rDO|FIN_START;
		}
		for ( int i = 0 ; i < names->length() ; ++i ) {
			sPtr<pigData> nm = names->get_ix(thNEW(pigDataInteger,((INTEGER64)i)));
			env->def_var(nm->get_str(), vals->get_ix(thNEW(pigDataInteger,((INTEGER64)i))));
		}
		return rDO|FIN_START;
	}
	sPtr<pigData> val = ( args.length() >= 2 ) ? args[1]
	                                           : sPtr<pigData>(thNEW(pigDataNull,()));
	// 値を **代入地点で compact** してから束縛する。DEF/SET で理由が違う:
	//   DEF: 定義地点(現 env)で固定 = レキシカルスコープ。遅延束縛だと自由変数が
	//        「使用地点(force 地点)の env」で解決され、内側スコープの同名シャドウを誤って
	//        拾う(dynamic scope バグ)。
	//   SET: 旧束縛で評価してから束縛。`a = a ||| box` のような自己参照が「新束縛を指す」
	//        循環(無限再帰)になるのを防ぐ。
	// 安全性: 値は実値に、mesh は継続 pair("delayed".promise→pigDataCache=変数なし)に、
	// lambda は値(env は参照なので自己束縛も後から見える)に解決されるだけで型は変わらない
	// (再帰・クロージャ・while/for は検証で不変)。
	if ( args.length() >= 2 ) {
		val = val->compact();
		/* ★ 右辺がエラーならエラーを**束縛せず**その場で伝播する (2026-09-04 ひさ指示)。
		 * 分割代入 (PIG_ASSIGN_DEF_LIST・上) と添字代入 (pigDataOperatorSetIndex) は
		 * 以前から右辺を is_error 判定しており、**通常代入だけが見ていなかった**。
		 * 「遅延を保つため見ない」という旧来の理由は、キャッシュの遅延を継続 pair
		 * (car/cdr) で繋ぐ以前の残骸で、いまは compact() が幾何を promise に解決するので
		 * 遅延は損なわれない。エラーを束縛すると、使用地点まで発覚が遅れて位置情報が
		 * 代入式から離れてしまう。 */
		if ( val->is_error() ) { asgErr = val; return rDO|FIN_START; }
	}
	// DEF(var あり)= def_var / SET(var なし)= set_var。_front のモードで分岐。
	if ( sPtr<pigDataFunction_b>::d_cast(_front)->get_mode() == PIG_ASSIGN_DEF )
		env->def_var(args[0]->get_str(), val);
	else
		env->set_var(args[0]->get_str(), val);
	return rDO|FIN_START;
}
TS_STATE(FIN_START)                // pigfFunction の FIN gate を上書き
{
	return rDO|FIN_pigfAssign_START;
}
TS_STATE(FIN_pigfAssign_START)
{
	_front->set_result( asgErr.is_notNull() ? asgErr : args[0] );   // 変数名を返す(値の遅延を保つ)
	return rDO|FIN_pigfFunction_START;
}
