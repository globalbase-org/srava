/*
 * pigfTryCatch — try/catch 文の tinyState helper(pigfFunction 派生)。#3482 段 1。
 *   args[0] = statement1(try 本体・ブロック = pigfSequence)
 *   args[1] = statement2(catch 本体。_front->get_has_catch()==0 なら無い)
 *
 * ★★ この段で入るのは **直列系のエラーだけ**を捕まえる形:
 *   1.  statement1 を is_error()(= compact 兼ねる)で評価する
 *   1.1   コントロール系(break / continue / return / exit)は **捕まえずそのまま戻り値**にする。
 *         try が持つのは制御の分岐ではなく「{} で囲った範囲 = 待ちリストのスコープ」だから。
 *   1.2   実エラーなら try のノードへ積み(error() が読む)、2. へ
 *   1.3   エラーが無ければ statement1 の値が try の値(statement2 は呼ばれない)
 *   2.1 statement2 を実行する。★ **この時点では destroy を送らない** — 撤収するかどうかは
 *       catch の戻り値と destroy() が決める、というのが本チケットの眼目。
 *   2.2 catch が無ければ、発生したエラーをそのまま戻り値にする
 *       (⇒ `try { s }` は `try { s } catch { error() }` と同じ意味になる)
 *   2.3/2.4 statement2 の値(通常値・エラー・コントロール系)をそのまま try/catch の値にする
 *
 * ★★ 段 2 で入った待ち (§2 の 1.2 / 2.2〜2.4):
 *   ・この try のスコープで起動した agent は **ノード側の待ちリスト**に登録される
 *     (pigfAgent の INI/FIN から pigDataTryCatch::agent_enter/leave)
 *   ・**どの終わり方でも、待ちリストが空になるまで抜けない** —
 *     「子プロセスを回収し終える前に抜けると planner が先に exit する」(§4.4) はここにも効く
 *   ・**destroy を送るかどうかだけが分かれる**:
 *       statement1 が正常終了          … 送らない (1.3)
 *       catch がエラー/コントロール系  … 送る   (2.2 / 2.4)
 *       catch が通常値                 … 送らない = 生かして待つ (2.3)
 *       catch が無い                   … 送る   (2.2 と同じ経路)
 *     ★ 眼目は「撤収するかどうかを catch の中で選べる」こと。
 *   ・「エラーが出るまで / 全員終わるまで」待つのは **error()** の側 (§2-A)。ここは
 *     *最後に全員を見送る* 役。
 * ⚠ 待ちリストへ来るのは **継続を既に返した後**の agent エラー (§4.2 ①) だけ。②/③ は
 *   評価チェーンを直列に上がって statement1 のエラーになる (二重に積まない)。
 *
 * env: 自分の子 env を 1 つ作り、そこへ **自分(= _front のノード)への生ポインタ**を刺す。
 *   statement1 / statement2 のブロックはこの env を親にした子 env を作り、tryPtr を引き継ぐ
 *   (pigfSequence / pigfAsync / pigfApply の 3 箇所)。⇒ catch 内の error() が O(1) で辿り着く。
 */
#include	"pig/c++/pigfFunction.h"
#include	"pig/c++/osglue.h"   /* osglue_env_int (#3419 §17.2) */
#include	"pig/c++/ptsApplication.h"   /* ptsApp 値メンバの完全型(ptsObject.h から移動・#3406 4.2) */
#include	"pig/c++/pigData.h"
#include	"_ts2/c++/pigfTryCatch_.h"

CLASS_TINYSTATE(pig/c++/pigfTryCatch,pig/c++/pigfFunction)


#if 0

TS_BEGIN_IMPLEMENT


class TS_THISCLASS : public TS_BASECLASS {
public:
	pigfTryCatch_(
		sPtr<ptsObject> parent,
		sPtr<pigDataOperator> _front);

	sRptr<ptsObject,tinyState>		parent;
private:
protected:
	int		tcPhase;       /* 0=statement1 / 1=本体後の見張り / 2=statement2 / 3=最終の見送り */
	int		tcDestroyed;   /* 評価中の文へ destroy を転送済み(1 回だけ) */
	sPtr<pigData>	tcErr;         /* statement1 が返した実エラー */
	sPtr<pigData>	tcOut;         /* try/catch の戻り値(待ちが明けたら _front へ) */
};

TS_END_IMPLEMENT

TS_BEGIN_INTERFACE
#include	"ts2/c++/sRptr.h"
class ptsObject;
class pigDataOperator;
class pigData;
TS_END_INTERFACE

#endif


pigfTryCatch_::pigfTryCatch_(TS_ARGS0)
        : pigfFunction_(parent,_front),
	  parent(tinyState_::parent)
{
    tcPhase     = 0;
    tcDestroyed = 0;
}


/*******************************************
	STATE MACHINE
********************************************/

TS_STATE(INI_pigfFunction_START)
{
	tcPhase     = 0;
	tcDestroyed = 0;
	tcErr       = thNULL;
	tcOut       = thNULL;
	/* try のスコープ: 子 env を 1 段作り、そこに自分(try ノード)を刺す。
	 * ⚠ 親の env をそのまま書き換えてはいけない — env は親子で **共有**されるので、
	 *   try を抜けた後の兄弟文まで同じ try に属してしまう。 */
	env = thNEW(pigEnvironment,(env));
	env->set_try( sPtr<pigDataTryCatch>::d_cast(_front) );
	/* ★ 実験 (#3403 系・ひさ案 2026-09-20): ここで **一旦切る**。rDO で続けると s1 の評価が
	 *   thNEW(pigfTryCatch,…) の中で走り切り、呼び手の `helper = …` の代入が **間に合わない**
	 *   (flush() が try_helper() を引けず黙って素通りする)。gc に積み直して TSE_RETURN で入り直す。 */
	application->gc->exe(ifThis);
	return INI_RET;
}

TS_STATE(INI_RET)
{
	R_TEST
	return rDO|ACT_START;
}

/* statement1 の評価。 */
TS_STATE(ACT_START)
{
	/* ★ destroy の転送 (ひさ設計 2026-08-11)。pigfSequence と同型: **いま評価中の文だけ**を畳む。
	 * 1 度だけ送り通常経路へ落とす — destroy された子は _front をエラー解決するので
	 * 下の is_error() がそれを拾う。 */
	if ( is_destroyed() && ! tcDestroyed ) {
		tcDestroyed = 1;
		if ( osglue_env_int("PIG_DBG_TD", 0) ) ::fprintf(stderr, "[td] trycatch: destroy 転送(try)\n");
		if ( args.length() > 0 && args[0].is_notNull() ) args[0]->destroy();
	}
	if ( args.length() < 1 ) {                 /* 文法上ありえないが安全に */
		_front->set_result(thNEW(pigDataNull,()));
		return rDO|FIN_START;
	}
	/* ⚠⚠ **compact() で値まで解いてから** 判定する。遅延ノード (pigDataDelay) は
	 *   control_kind() を委譲しない (is_error / get_int 等のゲートウェイ一覧に無い) ので、
	 *   ノードのまま聞くと break/continue/return でも常に -1 が返り、**コントロール系が
	 *   実エラーとして catch に落ちる**。pigfWhile も同じ理由で compact してから ck を見ている。
	 *   ★ この取り違えは catch **無し**では症状が出ない (どちらの経路も同じ値を返す) —
	 *     負の対照で初めて出た (2026-09-18)。
	 * yield しうるが tcPhase 据え置きで再走 → 同じ文を再評価 (解決済みは即返る) → 前進。 */
	sPtr<pigData> s1 = args[0]->compact();
	if ( s1->is_error() ) {
		if ( s1->control_kind() >= 0 ) {
			/* 1.1 コントロール系はそのまま戻り値。★ ただし **待ちリストは見送る** —
			 * 「子プロセスを回収し終える前に抜けると planner が先に exit する」(§4.4) は
			 * この経路にも効く。destroy は送らない (畳む理由が無い = 2.3 と同じ扱い)。
			 * ⚠ §2 の 1. は「戻り値とする」としか書いていないので、待つ/送らないは
			 *   4.4 の制約と 2.3 から引いた判断。 */
			tcOut   = s1;
			tcPhase = 3;
			return rDO|ACT_pigfTryCatch_WAIT;
		}
		tcErr = s1;                        /* 1.1 実エラー → catch へ */
		sPtr<pigDataTryCatch> tn = sPtr<pigDataTryCatch>::d_cast(_front);
		if ( tn.is_notNull() ) tn->push_error(s1);   /* error() が読む列へ積む */
		tcPhase = 2;
		return rDO|ACT_pigfTryCatch_CATCH;
	}
	/* statement1 は値を返した。★ ただしまだ終わりではない — **待ちリストを見張りながら待つ**
	 * (§2 の 1.2)。待っている間にエラーが来たら catch へ回る。 */
	tcOut   = s1;
	tcPhase = 1;
	return rDO|ACT_pigfTryCatch_WAITBODY;
}

/* 1.2 / 1.3 — statement1 は値を返したが、待ちリストにまだ計算が残っている間の見張り。
 *   エラーが来た           → 2. へ (catch を走らせる)
 *   誰もエラーを出さずに空 → 正常終了 (statement2 は呼ばれない)
 * ★ ここが「async の失敗が catch で捕まる」経路。async は値を直列に観測する者が居ないので、
 *   **待ちリスト経由でしか** try に届かない。 */
TS_STATE(ACT_pigfTryCatch_WAITBODY)
{
	sPtr<pigDataTryCatch> tn = sPtr<pigDataTryCatch>::d_cast(_front);
	if ( is_destroyed() && tn.is_notNull() )
		tn->destroy_agents();          /* 外から畳まれたら待ちリストへも転送 */
	sPtr<pigData> pe = tn.is_notNull() ? tn->peek_error() : sPtr<pigData>(thNULL);
	if ( pe.is_notNull() ) {           /* 1.2 エラーが来た → 2. へ (消費はしない = error() が読む) */
		tcErr   = pe;
		tcPhase = 2;
		return rDO|ACT_pigfTryCatch_CATCH;
	}
	if ( tn.is_notNull() && tn->agent_live() > 0 )
		return 0;                      /* 起こされるまで待つ (通知駆動) */
	return rDO|ACT_pigfTryCatch_WAIT;  /* 1.3 正常終了 (tcOut は statement1 の値のまま) */
}

/* statement2(catch 本体)の評価。 */
TS_STATE(ACT_pigfTryCatch_CATCH)
{
	sPtr<pigDataTryCatch> tn = sPtr<pigDataTryCatch>::d_cast(_front);
	int hasCatch = tn.is_notNull() ? tn->get_has_catch() : 0;
	if ( ! hasCatch || args.length() < 2 ) {
		/* 2.2 catch 無し: 発生したエラーをそのまま戻り値にし、**待ちリストへ destroy を送って**
		 * 全終了を待つ。⇒ `try { s }` ≡ `try { s } catch { throw error(); }`。 */
		tcOut   = tcErr;
		tcPhase = 3;
		if ( tn.is_notNull() ) tn->destroy_agents();
		return rDO|ACT_pigfTryCatch_WAIT;
	}
	if ( is_destroyed() && ! tcDestroyed ) {
		tcDestroyed = 1;
		if ( osglue_env_int("PIG_DBG_TD", 0) ) ::fprintf(stderr, "[td] trycatch: destroy 転送(catch)\n");
		if ( args[1].is_notNull() ) args[1]->destroy();
	}
	/* 2.1 statement2 を実行する。★ ここで destroy を送らないのが新設計の眼目
	 * (撤収するかどうかは catch の戻り値と destroy() が決める)。 */
	sPtr<pigData> v = args[1]->compact();
	/* 2.2 / 2.3 / 2.4 — 値はどれもそのまま try/catch の値。**分かれるのは destroy を送るかだけ**:
	 *   エラー / コントロール系 (break・continue・return) → 送る (2.2 / 2.4)
	 *   通常値                                            → 送らない = 生かして待つ (2.3)
	 * ⇒ 「1 つ落ちても残りを走らせ切る」と「最初の 1 件で全部畳む」が **どちらも書ける**。 */
	tcOut   = v;
	tcPhase = 3;
	if ( v->is_error() && tn.is_notNull() )      /* コントロール系も is_error() が真 */
		tn->destroy_agents();
	return rDO|ACT_pigfTryCatch_WAIT;
}

/* 待ちリストが空になるまで見送る。★ **通知駆動** — agent_leave が wake_waiters 経由で
 * この helper を起こすので、ここでポーリングはしない。 */
TS_STATE(ACT_pigfTryCatch_WAIT)
{
	sPtr<pigDataTryCatch> tn = sPtr<pigDataTryCatch>::d_cast(_front);
	/* 外から畳まれたら待ちリストにも転送する (自分だけ抜けると agent が取り残される)。 */
	if ( is_destroyed() && tn.is_notNull() )
		tn->destroy_agents();
	if ( tn.is_notNull() && tn->agent_live() > 0 )
		return 0;                                /* 起こされるまで待つ */
	if ( tcOut == thNULL )
		tcOut = thNEW(pigDataNull,());
	_front->set_result(tcOut);
	return rDO|FIN_START;
}

TS_STATE(FIN_START)
{
	return rDO|FIN_pigfTryCatch_START;
}

TS_STATE(FIN_pigfTryCatch_START)
{
	/* ★★ #3564 (ひさ念押し 2026-09-20): **env が指す try を明示的に切る**。
	 * #3564 で env→try は生ポインタから sPtr (強参照) になった。環
	 *   try ノード (_front) → helper (この状態機械) → env → try ノード
	 * は基底の @FIN_pigfFunction_START@ が @env = thNULL@ で切るので残らないが、
	 * ⚠ **引き継ぎ先の env が残っている場合**は自分の env を捨てるだけでは切れない
	 *   (pigfSequence / pigfAsync / pigfApply は子 env へ同じ try を写している)。
	 *   ⇒ ここで辺そのものを落としておく。@env = thNULL@ と二重に見えるが、
	 *     二重なのは *自分の* env についてだけ。 */
	if ( env.is_notNull() )
		env->set_try(thNULL);
	return rDO|FIN_pigfFunction_START;
}
