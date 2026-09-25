/*
 * pigfApply — lambda 適用(apply)の tinyState helper(pigfFunction 派生)。
 * args[0]=被呼び出し式(評価すると pigDataLambda)、args[1..]=実引数式。
 *
 * clone/thunk 再評価モデル:
 *   1. args[0] を compact して lambda 値を得る。
 *   2. 実引数 args[1..] を **呼び出し側 env**(= 基底が継承した現 env)で compact(eager)。
 *      → 引数は定義側でなく呼び出し側スコープで評価される(正しいレキシカル意味)。
 *      agent 継続(("delayed".promise))も compact は非ブロッキングなのでそのまま束縛できる。
 *   3. 新 env(parent=lambda の captured env)に params を束縛。
 *   4. body は **テンプレ** なので body->clone() で新鮮ノードに(メモ衝突回避)。
 *   5. env を新 env に切替え、clone した body を compact → その値を返す。
 * compact は async で yield しうる(sException で本状態が再走)。prepared フラグで①〜④を一度だけ。
 */
#include	"pig/c++/pigfFunction.h"
#include	"pig/c++/osglue.h"   /* osglue_env_int (#3419 §17.2) */
#include	"pig/c++/ptsApplication.h"   /* ptsApp 値メンバの完全型(ptsObject.h から移動・#3406 4.2) */
#include	"pig/c++/pigData.h"
#include	"_ts2/c++/pigfApply_.h"

CLASS_TINYSTATE(pig/c++/pigfApply,pig/c++/pigfFunction)


#if 0

TS_BEGIN_IMPLEMENT


class TS_THISCLASS : public TS_BASECLASS {
public:
	pigfApply_(
		sPtr<ptsObject> parent,
		sPtr<pigDataOperator> _front);

	sRptr<ptsObject,tinyState>		parent;
private:
protected:
	sPtr<pigData>		bodyClone;    /* clone した body(新鮮ノード) */
	sPtr<pigEnvironment>	applyEnv;     /* params 束縛 + parent=captured env */
	int			bodyDestroyed;   /* bodyClone へ destroy を転送済み(1 回だけ) */
	int			argsDestroyed;   /* 実引数 args[1..] へ destroy を転送済み(1 回だけ) */
};

TS_END_IMPLEMENT

TS_BEGIN_INTERFACE
#include	"ts2/c++/sRptr.h"
class ptsObject;
class pigDataOperator;
class pigData;
class pigEnvironment;
TS_END_INTERFACE

#endif


pigfApply_::pigfApply_(TS_ARGS0)
        : pigfFunction_(parent,_front),
	  parent(tinyState_::parent)
{
    /* ★ TS_CPARGS0 は撤去 (ひさ 2026-08-22): codegen が保存する _front は基底へ転送するだけで
     * この派生では読まない。TS_DEFARGS ごと外して **ノードへの重複参照を作らない**。 */
    bodyDestroyed = 0;
    argsDestroyed = 0;
}


/*******************************************
	STATE MACHINE
********************************************/

TS_STATE(INI_pigfFunction_START)
{
	return rDO|ACT_START;
}

/* 準備: callee→lambda 値、新 env(params 遅延束縛)、body clone。
 * callee の compact が async で yield したら本状態が再走するが、compact はメモ化され、
 * ne/clone は callee 解決後にしか作られないので一度きり(prepared フラグ不要)。 */
TS_STATE(ACT_START)
{
	/* ★ destroy の転送 (ひさ指摘 2026-08-11)。実引数 args[1..] は **この状態の is_error()/compact が
	 * 評価を開始させている** (call-by-value・呼び出し側 env で eager 評価) = 駆動しているのは自分。
	 * よって撤収要求が来たらここから畳む (未起動なら no-op)。args[0] は callee で lambda 値に
	 * 解決されるだけなので触らない。1 度だけ送り通常経路へ落とす。 */
	if ( is_destroyed() && ! argsDestroyed ) {
		argsDestroyed = 1;
		if ( osglue_env_int("PIG_DBG_TD", 0) ) ::fprintf(stderr, "[td] apply args: destroy 転送\n");
		for ( int di = 1 ; di < args.length() ; ++di )
			if ( args[di].is_notNull() ) args[di]->destroy();
	}
	if ( args.length() < 1 ) {                 /* 文法上ありえないが安全に */
		_front->set_result(thNEW(pigDataError,("apply: no callee",thNULL,1)));
		return rDO|FIN_START;
	}
	sPtr<pigData> c = args[0];
	if ( c->is_error() ) {                     /* is_error() は compact ゲートウェイ(不動点解決) */
		_front->set_result(c->compact());
		return rDO|FIN_START;
	}
	/* lambda オブジェクト(params/body/env)が要るので compact して実体を取る。
	 * compact() は result->compact() で不動点まで(varref→束縛→…→lambda 値)解決する。 */
	sPtr<pigDataLambda> l = sPtr<pigDataLambda>::d_cast(c->compact());
	if ( ! l.is_notNull() ) {                  /* lambda でない */
		_front->set_result(thNEW(pigDataError,("apply: callee is not a function",thNULL,1)));
		return rDO|FIN_START;
	}
	if ( l->paramc() != args.length() - 1 ) {
		_front->set_result(thNEW(pigDataError,("apply: argument count mismatch",thNULL,1)));
		return rDO|FIN_START;
	}
	/* 実引数は **呼び出し側 env で評価**(call-by-value)してから束縛する。ここ(ACT_START)の
	 * env はまだ caller env(ACT_pigfApply_DO で applyEnv に切替える前)なので、下の is_error()
	 * = compact ゲートウェイが引数を **caller env で評価・メモ化**する。これで node のまま束縛しても
	 * 後で body が force するとメモ(= caller env での値)を返し、callee 同名 param への変数捕捉
	 * (例: f(n-1) の n が callee の n=その束縛自身を指し自己循環 → "delay not resolved")が起きない。
	 * mesh 引数の評価は継続を返すだけで非ブロッキング、if 分岐ガードで未使用枝の過剰評価もなし。
	 * async で yield したら本状態が再走するが、評価はメモ化・ne 再構築は冪等。 */
	sPtr<pigEnvironment> ne = thNEW(pigEnvironment,(l->env()));
	/* ★★ #3482: try の帰属は **動的** = 呼び出し元から引き継ぐ。親(= lambda の捕捉 env)からでは
	 * ない — ヘルパ lambda を try の外で定義して try の中で呼ぶのが最も普通の書き方で、
	 * レキシカル(定義地点の try)だとそこが効かないため(C++ の例外と同じ直感)。
	 * ★ ここ(ACT_START)の env はまだ **caller env** なので、その場で引ける。 */
	if ( env.is_notNull() ) ne->set_try( env->get_try() );
	/* ★★ #3595 の続き (ひさ 2026-09-24): **USE_MODULES も動的**に引き継ぐ (try と同じ扱い)。
	 *
	 *   ⚠ クロージャは @snapshot_into@ で **値捕捉** する (#3450) ので、捕捉 env には
	 *     *定義時点の* USE_MODULES の写しが入っている。⇒ 何もしないと、ヘルパ lambda は
	 *     **定義された場所の候補列**で解き、呼び出し側が `use` で敷いた列が届かない。
	 *     しかも「届くかどうか」は *利用者が include を use の前に書いたか後に書いたか* で
	 *     変わってしまう (定義が use より後なら捕捉に入る) = 書き順で答えが動く。
	 *   ★ 直感は try と同じ: ヘルパを外で定義して中で呼ぶのが普通の書き方なので、
	 *     「いまどのカーネルで解いているか」は **呼び出し元**から引き継ぐのが正しい。
	 *   ★ 引き継ぐのは **USE_MODULES ただ 1 つ**。他の変数はレキシカルのまま (動的スコープに
	 *     するわけではない) — 候補列は「その計算をどのカーネルで解くか」という *文脈* であって、
	 *     値ではないため。
	 *   ★ 関数が **自分で `use` を書けばそれが勝つ** (body の DEF がこの束縛を覆う)
	 *     ⇒ ライブラリ関数の宣言 (docs §lib-use-decl) はこの変更後も従来どおり効く。
	 *   ⚠ @get_var@ は未定義のときエラー値を返すので、**@has_var@ で束縛の有無を見てから**引く
	 *     (エラー値が入っている場合はそのまま引き継いで伝播させる)。 */
	if ( env.is_notNull() ) {
		sPtr<stdString> umn = thNEW(stdString,("USE_MODULES"));
		if ( env->has_var(umn) ) ne->def_var(umn, env->get_var(umn));
	}
	for ( int i = 0 ; i < l->paramc() ; ++i ) {
		sPtr<pigData> av = args[i+1];
		if ( av->is_error() ) {            /* caller env で評価(副作用でメモ化)+ エラーなら伝播 */
			_front->set_result(av);
			return rDO|FIN_START;
		}
		ne->def_var(l->param(i), av);
	}
	bodyClone = l->body()->clone();            /* body テンプレを新鮮ノードに */
	applyEnv  = ne;
	return rDO|ACT_pigfApply_DO;
}

/* body を callee スコープで評価して返す。compact が yield したら本状態が再走するが、
 * env/bodyClone はメンバなので保たれ、compact はメモ化で前進する(prepared 不要)。 */
TS_STATE(ACT_pigfApply_DO)
{
	/* ★ destroy の転送 (ひさ設計 2026-08-11)。`bodyClone` は **自分だけが持つ複製** なので、
	 * 呼び元の AST (planner の tree) からは辿れない = ここで明示的に送るしかない。
	 * 無条件巡回はしない (destroy の順序は所有者が知っている): args は _front の持ち物なので触らず、
	 * clone した body だけを畳む。★**1 度だけ**送って通常経路へ落とす — destroy された子は
	 * FIN_pigfFunction_START が _front をエラー解決するので、下の compact がそれを拾って
	 * この apply も畳まれる (毎回送って return 0 にすると永久に先へ進めない)。 */
	if ( is_destroyed() && ! bodyDestroyed ) {
		bodyDestroyed = 1;
		if ( osglue_env_int("PIG_DBG_TD", 0) ) ::fprintf(stderr, "[td] apply: destroy 転送\n");
		if ( bodyClone.is_notNull() )
			bodyClone->destroy();
	}
	env = applyEnv;
	sPtr<pigData> r = bodyClone->compact();
	int ck = r->control_kind();
	if ( ck == CTRL_RETURN )                    /* return 値を関数の返り値に剥がす */
		r = r->control_value();
	else if ( ck == CTRL_BREAK || ck == CTRL_CONTINUE )   /* ループ外の break/continue */
		r = thNEW(pigDataError,( ck == CTRL_BREAK ? "break outside loop"
		                                          : "continue outside loop", thNULL ));
	_front->set_result(r);
	return rDO|FIN_START;
}

TS_STATE(FIN_START)
{
	return rDO|FIN_pigfApply_START;
}

TS_STATE(FIN_pigfApply_START)
{
	return rDO|FIN_pigfFunction_START;
}
