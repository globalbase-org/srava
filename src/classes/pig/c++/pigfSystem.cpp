/*
 * pigfSystem — system(cmd) の tinyState helper(pigfFunction 派生)。
 * シェルコマンドを **ts2System で非同期実行**する。時間のかかるコマンドでもイベントループを塞がない
 * (同期 ::system だと planner が固まり、並行 agent も止まる)。
 *   - ★ #3538: 子の stdout/stderr を **pipe で受けて親のそれへ中継**する。
 *     ⚠⚠ 旧版は「pipe を捕らえない → 親へ継承」と書いてあったが **そうなっていなかった**。
 *       rfd/efd/wfd を 3 つとも渡さないと `newProcess` が `wfd==0 && efd==0` で
 *       **暗黙に DM_TTY を立て**、`soOPENPTY` の pty に子の 0/1/2 を繋ぎ、親は master を
 *       `ts2IOdevNull` へ排水していた ⇒ **出力は画面に出ず黙って捨てられていた**
 *       (Linux / mac で実測。docs は「そのまま画面に出る」と約束している)。
 *     ⚠⚠ さらに **MinGW の ts2System は DM_TTY 非対応**なので、Windows では
 *       `system()` が **起動すらしなかった** ("failed to launch command")。
 *     ⇒ 実 pipe を渡せば暗黙の DM_TTY は立たず、pty を使わないので Windows も直る。
 *     ⚠ 受け取った以上 **排水は自分の責任** (読まないと 64KB で子が固まる) → 2 本の sink が回す。
 *   - 完了は **ts2System の TSE_RETURN** で検出。終了コードを返す。
 * 文として置けば pigfSequence の評価順で export 等より先に走る → 出力ディレクトリ作成(mkdir -p)等に。
 */
#include	"pig/c++/pigfFunction.h"
#include	"pig/c++/osglue.h"   /* osglue_env_int (#3419 §17.2) */
#include	"pig/c++/ptsApplication.h"   /* ptsApp 値メンバの完全型(ptsObject.h から移動・#3406 4.2) */
#include	"pig/c++/pigData.h"
#include	"ts2/c++/ts2System.h"
#include	"ts2/c++/ts2IO.h"
#include	"pig/c++/ptsErrSink.h"   /* ★ #3538: 子の出力を読んで親へ中継する */
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
	/* ★ #3538: 子の stdout / stderr を受ける ts2IO と、それを読んで親へ中継する sink。
	 * ⚠ **排水を止めると子が 64KB で固まる**ので、TSE_RETURN まで生かしておく。 */
	sPtr<ts2IO>		rfd;
	sPtr<ts2IO>		efd;
	sPtr<ptsErrSink>	osink;
	sPtr<ptsErrSink>	esink;
	int			retp;
	/* ★ #3541: TSE_RETURN の終了コード。後始末は park しうる = 状態が頭から
	 * 再実行されるので、ev をまたいで運べる場所に退避しておく。 */
	INTEGER64		exit_status;
private:
};

TS_END_IMPLEMENT

TS_BEGIN_INTERFACE
#include	"ts2/c++/sRptr.h"
class ptsObject;
class pigDataOperator;
class ts2System;
class ts2IO;
class ptsErrSink;
TS_END_INTERFACE

#endif


pigfSystem_::pigfSystem_(TS_ARGS0)
        : pigfFunction_(parent,_front),
	  parent(tinyState_::parent)
{
    retp = 0;
    exit_status = 0;
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
	/* ★ #3538: 子の stdout/stderr を **pipe で受けて**親のそれへ中継する。
	 * ⚠ rfd/efd を渡すこと自体が「暗黙の DM_TTY を立てさせない」ことでもある。 */
	retp = 0;
	const char *runcmd = cmd->get_str();
#ifdef _WIN32
	/* MinGW の ts2System は '#' 直接 exec のみ対応(sh -c 非対応)。'#' を前置して直接起動する
	 * (単純コマンド向け。argv は空白区切り。cmd は MSYS の exe を PATH 探索で見つける)。 */
	char wbuf[4096];
	::snprintf(wbuf, sizeof wbuf, "#%s", cmd->get_str());
	runcmd = wbuf;
#endif
	/* 引数の並びは (parent, retp, cmd, rfd, efd, wfd, dmode)。
	 * ⚠ wfd は渡さない = 子の stdin は即 EOF。`system()` は対話コマンド用ではないので
	 *   これでよい (旧 DM_TTY 経路では tty が繋がっていたが、docs はそれを約束していない)。 */
	sys = thNEW(ts2System,(ifThis, &retp, runcmd,
	                       &rfd, &efd, (sPtr<ts2IO>*)0, 0));
	if ( retp < 0 || rfd == thNULL || efd == thNULL ) {
		rfd = thNULL; efd = thNULL;
		_front->set_result(thNEW(pigDataError,(thNEW(stdString,("system: failed to launch command")))));
		return rDO|FIN_START;
	}
	/* ★ 排水 + 中継。1 = 親の stdout へ / 2 = 親の stderr へ。 */
	osink = thNEW(ptsErrSink,(ifThis, rfd)); osink->set_relay(1);
	esink = thNEW(ptsErrSink,(ifThis, efd)); esink->set_relay(2);
	return ACT_pigfSystem_WAIT;   /* ts2System の TSE_RETURN 待ち → rDO なし */
}

TS_STATE(ACT_pigfSystem_WAIT)
{
	if ( ev->type == TSE_RETURN && ev->source == sys ) {
		/* ★★ #3541: 後始末は **この状態の中でやらない**。drain_now() が呼ぶ
		 * ts2IO::read() は、MinGW ではデータが無いと **throw して park する**
		 * (POSIX の非ブロッキング生読みとは別物。ヘッダの「yield しない」は
		 *  POSIX 限定の記述)。park すると状態は **頭から再実行**され、
		 * **ev は保存されない** ⇒ 処理中の TSE_RETURN が失われ、二度と成立しない
		 * 分岐を待ち続けて撤収が止まる (planner が永久に残る)。
		 * ⇒ 値だけ退避して、park に耐える別状態へ渡す。 */
		exit_status = (INTEGER64)ev->msg_int;
		return rDO|ACT_pigfSystem_AFTER_RETURN;
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

/* ★ #3541: TSE_RETURN を受けた後の後始末。
 * ⚠ **park しうる状態**なので、再実行に耐える形で書くこと:
 *   - `ev` を見ない (退避した exit_status を使う)
 *   - drain_now() は読んだ分を keep へ積むだけなので、再入しても二重計上にならない
 * ★ #3538: 結果を置く **前に** パイプの残りを吐き切る。子の終了通知 (waitpid) と
 *   sink が最後の塊を読む event は順序が保証されないので、drain しないと
 *   **子の最後の行が出ないまま**次の文へ進む。 */
TS_STATE(ACT_pigfSystem_AFTER_RETURN)
{
	/* park 中に destroy された場合は drain を諦めて畳む (待ち続けない)。 */
	if ( ! is_destroyed() ) {
		if ( osink.is_notNull() ) osink->drain_now();
		if ( esink.is_notNull() ) esink->drain_now();
	}
	/* 子プロセス終了。終了コード(相当)を結果に。 */
	_front->set_result(thNEW(pigDataInteger,(exit_status)));
	return rDO|FIN_START;
}

TS_STATE(FIN_START)
{
	return rDO|FIN_pigfSystem_START;
}

TS_STATE(FIN_pigfSystem_START)
{
	if ( sys.is_notNull() ) { sys->destroy(); sys = thNULL; }   /* §9: fd を持つ子を確実に手放す */
	/* ★ #3538: sink は EOF で自分から FIN するが、異常経路では残りうるので明示的に畳む。
	 * sink が io (rfd/efd) を destroy するので、ここで fd を二重に閉じない。 */
	if ( osink.is_notNull() ) { osink->destroy(); osink = thNULL; }
	if ( esink.is_notNull() ) { esink->destroy(); esink = thNULL; }
	rfd = thNULL; efd = thNULL;
	return rDO|FIN_pigfFunction_START;
}
