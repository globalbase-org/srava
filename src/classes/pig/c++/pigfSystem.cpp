/*
 * pigfSystem — system(cmd) の tinyState helper(pigfFunction 派生)。
 * シェルコマンドを **ts2System で非同期実行**する。時間のかかるコマンドでもイベントループを塞がない
 * (同期 ::system だと planner が固まり、並行 agent も止まる)。
 *   - pipe は捕らえない → 子の stdout/stderr は親(planner)に継承 = ユーザに直接見える。
 *   - 完了は **ts2System の TSE_RETURN** で検出(stdout drain 不要・エレガント)。終了コードを返す。
 * 文として置けば pigfSequence の評価順で export 等より先に走る → 出力ディレクトリ作成(mkdir -p)等に。
 */
#include	"pig/c++/pigfFunction.h"
#include	"pig/c++/osglue.h"   /* osglue_env_int (#3419 §17.2) */
#include	"pig/c++/ptsApplication.h"   /* ptsApp 値メンバの完全型(ptsObject.h から移動・#3406 4.2) */
#include	"pig/c++/pigData.h"
#include	"ts2/c++/ts2System.h"
#include	"ts2/c++/stdEvent.h"
#include	"_ts2/c++/pigfSystem_.h"

CLASS_TINYSTATE(pig/c++/pigfSystem,pig/c++/pigfFunction)


#if 0

TS_BEGIN_IMPLEMENT


class TS_THISCLASS : public TS_BASECLASS {
public:
	pigfSystem_(
		sPtr<ptsObject> parent,
		sPtr<pigDataOperator> _front);

	sRptr<ptsObject,tinyState>		parent;
protected:
	sPtr<ts2System>		sys;
	int			retp;
private:
};

TS_END_IMPLEMENT

TS_BEGIN_INTERFACE
#include	"ts2/c++/sRptr.h"
class ptsObject;
class pigDataOperator;
class ts2System;
TS_END_INTERFACE

#endif


pigfSystem_::pigfSystem_(TS_ARGS0)
        : pigfFunction_(parent,_front),
	  parent(tinyState_::parent)
{
    retp = 0;
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
		_front->set_result(thNEW(pigDataInteger,((INTEGER64)-1)));
		return rDO|FIN_START;
	}
	if ( args[0]->is_error() ) {           /* コマンド文字列の評価エラー → 伝播 */
		_front->set_result(args[0]);
		return rDO|FIN_START;
	}
	sPtr<stdString> cmd = args[0]->get_str();   /* 遅延/変数なら compact ゲートで解決 */
	if ( ! cmd.is_notNull() ) {
		_front->set_result(thNEW(pigDataInteger,((INTEGER64)-1)));
		return rDO|FIN_START;
	}
	/* pipe を捕らえずに起動(stdout/stderr は親へ継承)。完了は ts2System の TSE_RETURN で検出。 */
	retp = 0;
	const char *runcmd = cmd->get_str();
#ifdef _WIN32
	/* MinGW の ts2System は '#' 直接 exec のみ対応(sh -c 非対応)。'#' を前置して直接起動する
	 * (単純コマンド向け。argv は空白区切り。cmd は MSYS の exe を PATH 探索で見つける)。 */
	char wbuf[4096];
	::snprintf(wbuf, sizeof wbuf, "#%s", cmd->get_str());
	runcmd = wbuf;
#endif
	sys = thNEW(ts2System,(ifThis, &retp, runcmd,
	                       (sPtr<ts2IO>*)0, (sPtr<ts2IO>*)0, (sPtr<ts2IO>*)0, 0));
	if ( retp < 0 ) {
		_front->set_result(thNEW(pigDataError,(thNEW(stdString,("system: failed to launch command")))));
		return rDO|FIN_START;
	}
	return ACT_pigfSystem_WAIT;   /* ts2System の TSE_RETURN 待ち → rDO なし */
}

TS_STATE(ACT_pigfSystem_WAIT)
{
	if ( ev->type == TSE_RETURN && ev->source == sys ) {
		/* 子プロセス終了。終了コード(相当)を結果に。 */
		_front->set_result(thNEW(pigDataInteger,((INTEGER64)ev->msg_int)));
		return rDO|FIN_START;
	}
	/* ★ destroy の作法 (ひさ指示 2026-08-06): 子へ destroy() を送り、TSE_RETURN が
	 * 戻るのを **待ち続ける**。即 FIN しない。destroy された側が自分の終了処理をするので、
	 * こちらは戻ってくる内容に関知しない。 */
	if ( is_destroyed() ) {
		/* sys を destroy して子プロセスの終了 (TSE_RETURN) を待つ。ここで即 FIN すると
		 * 子プロセスが孤児になる。 */
		if ( osglue_env_int("PIG_DBG_TD", 0) ) ::fprintf(stderr, "[td] system: 子プロセスへ destroy\n");
		if ( sys.is_notNull() ) { sys->destroy(); return 0; }
		return rDO|FIN_START;
	}
	return 0;
}

TS_STATE(FIN_START)
{
	return rDO|FIN_pigfSystem_START;
}

TS_STATE(FIN_pigfSystem_START)
{
	if ( sys.is_notNull() ) { sys->destroy(); sys = thNULL; }   /* §9: fd を持つ子を確実に手放す */
	return rDO|FIN_pigfFunction_START;
}
