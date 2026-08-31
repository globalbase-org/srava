/*
 * ptsMediator — planner↔agent 通信の抽象基底 (#3406, docs/mediator_design.md §2)。
 *
 * process-per-op の通信 (ts2System/ts2IO/ptsWirePipe) と、将来の planner 内 thread 実行
 * (ptsMediatorInternal + ptsAgent, 段階 4.3) を同じ界面で扱うための抽象化。
 * ★ 界面の原則 (2026-08-02 メモ §2.1): **外 (pigfAgent / ptsAgent) へ ptsWirePipe のやり取りを
 *   見せない**。見せるのはオペレーションの意味だけ:
 *   - enable()          : 通信確立。確立完了は parent へ TSE_ASSERT (現行 HELLO と同じ見え方)。
 *   - pl_write_op(s)    : 演算子名。
 *   - pl_write_arg(i,d) : 引数 1 個 (PATH/INLINE の弁別は Mediator の中)。
 *   - pl_write_end(oc)  : 因数終わり = 計算開始 + 送信終端 (番兵まで Mediator が面倒を見る)。
 *   pigwire のレコード種別を引数に取る pl_write_str は **protected**。
 * agent→planner 向きの a_write/a_wend は廃止した — 実行体 (ptsAgent) は結果を set_result して
 * FIN で TSE_RETURN を 1 回返すだけになり (§5/§6)、ワイヤ列 (A_SAVE_BEGIN/DONE/BYE/番兵) の
 * 組み立ては受け手の親 (ptsAgentApplication / ptsMediatorInternal) が自前で行う。
 * 着信は parent への TSE_PACKET (External=ptsWirePacket / Internal=ptsMediatorPacket)。
 * 自分の終了は parent へ TSE_RETURN (必ず 1 回・2 回以上送らない)。
 *
 * NB: pl_write のオーバーロード統合 (2026-0727 メモ §1.4 の「pl_write_arg/str 統合?」) は
 *     tscpp2 がメソッドオーバーロードを扱えるか未確認のため、ptsWirePipe と同じ
 *     「別名で分ける」流儀 (write/write_str/write_arg/wend) に合わせた。
 */
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/ptsApplication.h"   /* ptsApp 値メンバの完全型(ptsObject.h から移動・#3406 4.2) */
#include	"pig/c++/pigData.h"
#include	"pig/c++/pigwire.h"   /* C_OP (pl_write_op) */
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ptsMediator_.h"

CLASS_TINYSTATE(pig/c++/ptsMediator,pig/c++/ptsObject)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	/* parent 型が sPtr<tinyState> なのは、agent 側 Mediator である ptsApplication の親が
	 * tsApplication (ptsObject ではない) だから (2026-0727 メモ §1.7)。派生は自分の
	 * 都合の型で parent メンバを宣言し直す (ptsMediatorExternal=ptsObject / ptsApplication=tsApplication)。 */
	ptsMediator_(
		sPtr<tinyState> parent);

	sRptr<tinyState,tinyState>		parent;

	/* 通信確立。0=成功 / 非0=起動失敗 (External: fork 失敗等。同期で返す)。
	 * ★ #3441 (ひさ設計 2026-08-26・旧 #3419 C_ENV の再構築): env = module(so,{opts}) の
	 *   ハッシュ (thNULL 可 = 何も渡さない)。**OS のプロセス環境変数は使わない** — in-proc の
	 *   実行体に届かないため (同じ仕組みで process / in-proc の両方へ届ける必要がある)。
	 *   ⚠ in-proc (Internal) は同一プロセス・同一 descriptor なので実は不要 — module() 実行時に
	 *   pigModuleRegistry が直接 configure() を呼んで完結する。env は **process (External) だけ**
	 *   が使う (起動直後に C_ENV で 1 回送る)。Internal 側は override してもパラメタを無視する。
	 *   default 引数は **ここ (base) にだけ**書く (override 側で重複定義しない — C++ の
	 *   既定引数は静的型で解決されるため、複数箇所に書くと base ポインタ越しの呼び出しで
	 *   食い違いかねない)。 */
	virtual int	enable(sPtr<pigData> env = thNULL);
	/* ★ #3419 T4-b: 別プロセスで走っているなら its pid、そうでなければ 0。
	 * C_MEM の集計に使う (§4.1)。in-proc は planner に含まれるので 0 でよい。 */
	virtual uint32_t	agent_pid();
	/* 起動失敗が非同期に確定したか (External の fork 失敗)。基底は常に 0。 */
	virtual int	launch_failed();
	/* 子プロセスが終了していれば waitpid の生 status、まだ/該当なしなら -1。基底は -1。 */
	virtual int	child_status();
	/* ★ このメディエータが担当しているモジュール名 (in-proc のみ・無ければ thNULL)。
	 * 「いま走っている op はどのモジュールのものか」を caller 鎖から引くのに使う
	 * (pig_current_module_id)。process 実行では agent プロセスに .so が 1 本しか無いので
	 * レジストリ側の「唯一のモジュール」解決で足りる。 */
	virtual sPtr<stdString>	module_name();
	/* ★ 演算子名の送信 (2026-08-02 メモ §2.1)。 */
	virtual int	pl_write_op(sPtr<stdString> s);
	/* planner→agent: 引数 1 個。PATH/INLINE の弁別 (is_cache) は Mediator 内で行う。 */
	virtual int	pl_write_arg(int idx, sPtr<pigData> d);
	/* planner→agent: C_ARG_END = 計算開始 + 送信終端 (2026-08-02 メモ §2.1)。
	 * 目標キャッシュパスは **outCache 自身が持っている**ので引数に取らない (冗長だった)。
	 * External は path だけ送り (ワイヤ互換) 続けて W_END 番兵まで出す。Internal は outCache
	 * オブジェクトを直渡し (agent が同一オブジェクトへ set_body する)。
	 * ★ 旧 pl_wend は廃止 — 番兵は「送信の終わり」であって呼び元が意識する情報ではないので、
	 *   ptsMediator の外 (pigfAgent) には見せない。 */
	virtual int	pl_write_end(sPtr<pigDataCache> outCache);
	/* ★ 終了要求は **destroy() に統一** した (§2.1/§3.2/§4.1・旧 shutdown() は廃止)。
	 * destroy を送る側は TSE_RETURN が戻るのを待つだけで中身に関知しない。Mediator は
	 * is_destroyed() を見て自分の破棄列 (teardown) を回し、TSE_RETURN を 1 回返して終わる。 */
protected:
	/* ★ pigwire のレコード種別を引数に取る = ワイヤを外へ見せてしまうので protected (§2.1)。
	 * 外向きの界面は pl_write_op / pl_write_arg / pl_write_end の 3 つだけ。 */
	virtual int	pl_write_str(int cmd, sPtr<stdString> s);
private:
	TS_DEFARGS
};

TS_END_IMPLEMENT

TS_BEGIN_INTERFACE
class pigDataHash;   /* ★ #3419: 環境制御変数 */
#include	"ts2/c++/sRptr.h"
class ptsObject;
class pigData;
class pigDataCache;
class stdString;
TS_END_INTERFACE

#endif


ptsMediator_::ptsMediator_(TS_ARGS0)
        : ptsObject_(parent),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

/* 基底は全て未実装エラー (-1)。派生 (External/Internal/agent 側) が override する。 */

int
ptsMediator_::launch_failed()
{
	return 0;
}

int
ptsMediator_::child_status()
{
	return -1;   /* 子プロセスを持たない実装 (in-proc thread 等) */
}

sPtr<stdString>
ptsMediator_::module_name()
{
	return sPtr<stdString>();   /* 基底は知らない (External は agent プロセス側で解決する) */
}

int
ptsMediator_::enable(sPtr<pigData> env)
{
	(void)env;
	return 0;
}


/* ★ #3419: 基底は「別プロセスではない」= 0。External だけが override する。 */
uint32_t
ptsMediator_::agent_pid()
{
	return 0;
}

int
ptsMediator_::pl_write_op(sPtr<stdString> s)
{
	return pl_write_str(C_OP, s);
}

int
ptsMediator_::pl_write_str(int cmd, sPtr<stdString> s)
{
	(void)cmd; (void)s;
	return -1;
}

int
ptsMediator_::pl_write_arg(int idx, sPtr<pigData> d)
{
	(void)idx; (void)d;
	return -1;
}

int
ptsMediator_::pl_write_end(sPtr<pigDataCache> outCache)
{
	(void)outCache;
	return -1;
}

