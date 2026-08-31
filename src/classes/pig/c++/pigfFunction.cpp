/*
 * pigfFunction — 状態機械を持つ関数の tinyState helper の基底。
 * pigDataFunction<T> ノードが compact 時に起動する。_front(= 起動元の pigDataFunction
 * ノード)から args をコピーし、最後に _front->set_result する。
 * 派生(pigfConst / pigfAssign / pigfSequence ...)が ACT/FIN を上書きして実体を与える。
 */
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/ptsApplication.h"   /* ptsApp 値メンバの完全型(ptsObject.h から移動・#3406 4.2) */
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
	/* ★ 2026-08-22 (ひさ): codegen が ctor 引数を保存する _front を **protected に置いて直接使う**。
	 * 以前は private に生成されるため触れず、_front メンバへコピーし直していたが、そうすると
	 * 同じノードへの参照が継承の段数ぶん増え、FIN で 1 本切っても環が残っていた。 */
	TS_DEFARGS
	sArray<sPtr<pigData> >	args;      // _front からコピーした引数
	sPtr<pigEnvironment>	env;       // 実行環境(変数束縛)
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
	if ( _front.is_notNull() ) {
		int n = _front->argc();
		args.length(n);
		for ( int k = 0 ; k < n ; ++k )
			args[k] = _front->arg(k);
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
	if ( _front.is_notNull() ) {
		_front->set_result(thNEW(pigDataError,("function returned no value",thNULL)),1);
		/* result 確定後、_front の args/helper を解放(巨大インライン引数=path 配列等をここで手放す)。
		 * result は保持されるので planner は観測で値を得られる。set_result の invoke_listen は既に
		 * 走っており(caller の再起動はスケジュール済)、clean は result を残すので安全。 */
		_front->clean();
		_front = thNULL;
	}
	/* ★ #3419 (2026-08-22): **自分がコピーした引数列も手放す**。
	 * _front 側は clean() が args を落とすが、pigfFunction が _front からコピーした this->args は
	 * これまで誰も切っていなかった。
	 * ⚠ pigfAgent は liveAgents (dedup 台帳・除去コード無し) に参照され続け **program 終了まで
	 *   生存する**ので、ここを切らないと **消費済みの入力ノード**が最後まで残る。in-proc では
	 *   その result がメッシュ実体を抱える pigDataCache なので、そのまま常駐量になる。
	 * ⇒ 切ると pigData ノードの解放が増える。
	 * ⚠ **これは残存の一部でしかない** — pigDataCache の生成/解放比は変わらず、常駐量も動かない。
	 *   本命 (参照カウントの循環) は別途調査中。 */
	args.length(0);
	/* ★ #3450 (ひさ指摘 2026-08-29): **env もここで手放す**。手放さないと、ZOM 済みノードの env
	 * メンバが子 env を生かし、子 env の parent がプログラム env を生かし、その values[] の束縛
	 * (変数に入った継続 pair) から中間結果の pigDataCache までがプログラム終了まで残る (実測 N+3 個)。
	 * FIN 以降にこのノードの get_env() を呼ぶ者はいない (評価は終わっている)。 */
	env = thNULL;
	return rDO|FIN_ptsObject_START;
}
