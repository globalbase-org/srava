/*
 * pigfAssign — 代入(変数定義+束縛)の tinyState helper。
 * args[0] = 変数名(テキスト)。args[1] = 代入値。
 * 肝: 変数名 args[0] は is_error 判定で compact 解決するが、代入値 args[1] は
 *     compact しない(= 実際に参照されるまで遅延)。env->def_var に未 compact の
 *     ノードをそのまま束縛する。戻り値は args[0](変数名)── args[1] を返すと
 *     呼び元(pigfSequence 等)がエラー判定で compact してしまい遅延が縮退するため。
 */
#include	"pig/c++/pigfFunction.h"
#include	"pig/c++/pigData.h"
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


pigfAssign_::pigfAssign_(TS_ARGS0)
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
TS_STATE(ACT_START)
{
	if ( args[0]->is_error() )         // 変数名の評価でエラー → 伝播
		return rDO|FIN_START;
	sPtr<pigData> val = ( args.length() >= 2 ) ? args[1]
	                                           : sPtr<pigData>(thNEW(pigDataNull,()));
	// DEF(var あり)= def_var / SET(var なし)= set_var。front のモードで分岐。
	if ( sPtr<pigDataFunction_b>::d_cast(front)->get_mode() == PIG_ASSIGN_DEF ) {
		// DEF: 値を **定義地点(現 env)で compact** してから束縛 = レキシカルスコープ。
		// 遅延束縛だと自由変数が「使用地点(force 地点)の env」で解決され、内側スコープの同名
		// シャドウを誤って拾う(dynamic scope バグ)。定義時 compact で定義環境に固定する。
		// 安全性: 値は実値に、mesh は継続 pair("delayed".promise→pigDataCache=変数なし)に、
		// lambda は値(env は参照なので自己束縛も後から見える)に解決されるだけで型は変わらない
		// (再帰・クロージャ・while/for は検証で不変)。
		if ( args.length() >= 2 )
			val = val->compact();
		env->def_var(args[0]->get_str(), val);
	} else {
		// SET(再代入): 値を **先に compact**(= 旧束縛で評価。yield しうるが再走で前進)してから
		// 束縛する。これで `a = a ||| box` のような自己参照が「新束縛を指す」循環(無限再帰)を防ぐ。
		if ( args.length() >= 2 )
			val = val->compact();
		env->set_var(args[0]->get_str(), val);
	}
	return rDO|FIN_START;
}
TS_STATE(FIN_START)                // pigfFunction の FIN gate を上書き
{
	return rDO|FIN_pigfAssign_START;
}
TS_STATE(FIN_pigfAssign_START)
{
	front->set_result(args[0]);        // 変数名を返す(値の遅延を保つ)
	return rDO|FIN_pigfFunction_START;
}
