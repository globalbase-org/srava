/*
 * pigfFunction — 状態機械を持つ関数の tinyState helper の基底。
 * pigDataFunction<T> ノードが compact 時に起動する。front(= 起動元の pigDataFunction
 * ノード)から args をコピーし、最後に front->set_result する。
 * 派生(pigfConst / pigfAssign / pigfSequence ...)が ACT/FIN を上書きして実体を与える。
 */
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/pigData.h"
#include	"_ts2/c++/pigfFunction_.h"

CLASS_TINYSTATE(pig/c++/pigfFunction,pig/c++/ptsObject)


#if 0

TS_BEGIN_IMPLEMENT


class TS_THISCLASS : public TS_BASECLASS {
public:
	pigfFunction_(
		sPtr<ptsObject> parent);
	pigfFunction_(
		sPtr<ptsObject> parent,
		sPtr<pigDataOperator> _front);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigEnvironment>	get_env();   // override: 自分の env を返す
protected:
	sPtr<pigDataOperator>	front;     // 起動元の pigDataFunction ノード(set_result 先)
	sArray<sPtr<pigData> >	args;      // front からコピーした引数
	sPtr<pigEnvironment>	env;       // 実行環境(変数束縛)
private:
	TS_DEFARGS
};

TS_END_IMPLEMENT

TS_BEGIN_INTERFACE
#include	"ts2/c++/sRptr.h"
class ptsObject;
class pigData;
class pigDataOperator;
class pigEnvironment;
TS_END_INTERFACE

#endif


pigfFunction_::pigfFunction_(TS_ARGS0)
        : ptsObject_(parent),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}

pigfFunction_::pigfFunction_(TS_ARGS1)
        : ptsObject_(parent),
	  parent(tinyState_::parent)
{
    TS_CPARGS1
    front = _front;
}

sPtr<pigEnvironment>
pigfFunction_::get_env()
{
	return env;
}


/*******************************************
	STATE MACHINE
********************************************/

TS_STATE(INI_ptsObject_START)   // ptsObject の INI gate を上書き: args コピー + env 設定
{
	if ( front.is_notNull() ) {
		int n = front->argc();
		args.length(n);
		for ( int k = 0 ; k < n ; ++k )
			args[k] = front->arg(k);
		env = parent->get_env();                  // 実態親の env を継承
	}
	else
		env = thNEW(pigEnvironment,(thNULL));      // ルート: 自前の env
	return rDO|INI_pigfFunction_START;
}
TS_STATE(INI_pigfFunction_START)   // 派生がここを上書きして初期化を挿入する
{
	return rDO|ACT_START;
}
TS_STATE(ACT_START)                // 基底はアイドル(派生が上書き)
{
	return 0;
}
TS_STATE(FIN_START)                // ptsObject の FIN gate を上書き
{
	return rDO|FIN_pigfFunction_START;
}
TS_STATE(FIN_pigfFunction_START)   // 後片付け(派生からもここへ)。結果未設定なら既定エラー
{
	if ( front.is_notNull() ) {
		front->set_result(thNEW(pigDataError,("function returned no value",thNULL)),1);
		/* result 確定後、front の args/helper を解放(巨大インライン引数=path 配列等をここで手放す)。
		 * result は保持されるので planner は観測で値を得られる。set_result の invoke_listen は既に
		 * 走っており(caller の再起動はスケジュール済)、clean は result を残すので安全。 */
		front->clean();
		front = thNULL;
	}
	return rDO|FIN_ptsObject_START;
}
