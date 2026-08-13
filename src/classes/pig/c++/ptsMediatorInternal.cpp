/*
 * ptsMediatorInternal — planner 内 thread で演算する Mediator (#3406 段階 4.3,
 * docs/mediator_design.md §2.5)。
 *
 * External (ts2System/pipe/pigwire) と同じ界面で、実行体 (ptsAgent 派生) を **同一プロセス内に
 * thNEW** し、pigData を **文字列化せず sPtr のまま** ptsMediatorPacket で直渡しする
 * (2026-0727 メモ §1.6。ここが本移行の狙い: encode/decode/Manifold 再構築税と fork/pipe が消える)。
 *
 *   enable()    : pigAgentRegistry から kernel 名で実行体を thNEW → parent へ TSE_ASSERT
 *                 (External の handshake 完了と同じ見え方。state_lock 中なのでキュー経由)。
 *   pl_write_*  : ptsMediatorPacket を実行体へ投函。pl_write_arg は d->compact() してから
 *                 (promise の可能性。compact ゲートの yield は呼び元の再走で再入 = 投函は
 *                 compact 成功後の 1 回だけなので二重投函しない)。
 *   teardown()  : 自分の FIN からだけ呼ぶ破棄列 (実行体へ destroy)。外からの終了要求は destroy()。
 *                 以後 実行体からの TSE_RETURN (FIN 通知) は agent=thNULL なので無視される。
 *
 * ★ 保存の見届け (2026-08-02 メモ §4.2 *5)。§5/§6 で実行体は「計算して set_result して FIN」だけに
 *   なり、ワイヤ列 (A_SAVE_BEGIN/DONE) の組み立ては **親 Mediator の仕事**になった。External 側は
 *   ptsAgentApplication が担うので、Internal 側は自分が同じ列を組む:
 *     実行体の TSE_RETURN (結果 = pigDataCache | pigDataError)
 *       → pigDataCache : is_valid で A_SAVE_BEGIN / is_complete で A_SAVE_DONE → parent へ TSE_RETURN
 *       → pigDataError : A_ERROR → parent へ TSE_RETURN
 *   is_complete まで待つのは External と同じ理由 = キャッシュファイルを書き切る前に pigfAgent を
 *   畳むと、planner 終了時に中途半端なキャッシュが残るため。
 */
#include	"pig/c++/ptsMediator.h"
#include	"pig/c++/ptsApplication.h"   /* ptsApp 値メンバの完全型(ptsObject.h から移動・#3406 4.2) */
#include	"pig/c++/pigCacheCodec.h"    /* is_stream_body: A_SAVE_BEGIN の payload 判定 */
#include	"pig/c++/pigwire.h"
#include	"pig/c++/ptsMediatorPacket.h"
#include	"pig/c++/ptsAgent.h"
#include	"pig/c++/pigAgentRegistry.h"
#include	"pig/c++/pigModuleRegistry.h"   /* ★ #3427 ③: app 所有レジストリ (agents/codecs) */
#include	"pig/c++/pigData.h"
#include	"ts2/c++/stdEvent.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ptsMediatorInternal_.h"

CLASS_TINYSTATE(pig/c++/ptsMediatorInternal,pig/c++/ptsMediator)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	/* moduleName = pigAgentRegistry のキー (例 "manifold")。codegen が同名メンバへ自動代入。 */
	ptsMediatorInternal_(
		sPtr<ptsObject> parent,
		sPtr<stdString> moduleName);

	sRptr<ptsObject,tinyState>		parent;

	virtual int	enable();
	virtual int	pl_write_arg(int idx, sPtr<pigData> d);
	virtual int	pl_write_end(sPtr<pigDataCache> outCache);
protected:
	/* 破棄列。**界面ではない** (§2.1/§4.1: 外からの終了要求は destroy() に統一) — 自分の
	 * FIN からだけ呼ぶ。 */
	void		teardown();
protected:
	virtual int	pl_write_str(int cmd, sPtr<stdString> s);
	sPtr<ptsAgent>		agent;    /* 実行体 (同一プロセス内)。enable() で thNEW */
	/* ★ 実行体から TSE_RETURN で受け取った結果 (§4.2 *5)。ptsAgentApplication の同名メンバと対。 */
	sPtr<pigData>		agentResult;
	sPtr<pigDataCache>	outCache;
	int			retSent;   /* parent への TSE_RETURN を必ず 1 回・2 回以上送らない (§2.2.1) */
private:
	int		a_write(int cmd, sPtr<pigData> d);   /* 結果を parent へ投函 (private・§2.1) */
	TS_DEFARGS
};

TS_END_IMPLEMENT

TS_BEGIN_INTERFACE
#include	"ts2/c++/sRptr.h"
#include	"pig/c++/ptsAgent.h"   /* sPtr<ptsAgent> agent 値メンバの完全型 */
class ptsObject;
class pigData;
class pigDataCache;
class stdString;
TS_END_INTERFACE

#endif


ptsMediatorInternal_::ptsMediatorInternal_(TS_ARGS0)
        : ptsMediator_(parent),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
    retSent = 0;
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

int
ptsMediatorInternal_::enable()
{
	if ( moduleName == thNULL )
		return -1;
	/* ★ #3427 ③: app 所有レジストリから引く。 */
	pigAgentFactory f = ( ptsApp != thNULL && ptsApp->module_registry != thNULL )
	    ? ptsApp->module_registry->agents.lookup(moduleName->get_str()) : 0;
	if ( f == 0 )
		return -1;   /* 実行体未リンク (呼び元は External へフォールバックせずエラー。選択は LAUNCH 側) */
	agent = f(ifThis);
	if ( agent == thNULL )
		return -1;
	/* handshake: External は pipe の streamhdr 交換後に TSE_ASSERT を転送する。Internal は確立に
	 * 待ちが無いので即 parent へ。呼び元 (pigfAgent LAUNCH) は state_lock 中なのでキューに積まれ、
	 * HELLO 状態が受ける (見え方は External と同一)。 */
	parent->eventHandler(thNEW(stdEvent,(TSE_ASSERT, ifThis, (INTEGER64)0)));
	return 0;
}

int
ptsMediatorInternal_::pl_write_str(int cmd, sPtr<stdString> s)
{
	if ( agent == thNULL )
		return -1;
	agent->eventHandler(thNEW(stdEvent,(TSE_PACKET, ifThis,
		sPtr<stdObject>(thNEW(ptsMediatorPacket,(cmd, 0, thNULL, s))))));
	return 0;
}

int
ptsMediatorInternal_::pl_write_arg(int idx, sPtr<pigData> d)
{
	if ( agent == thNULL )
		return -1;
	/* d は promise ("delayed" 継続の cdr->cdr) の可能性がある → compact で実値に解決してから渡す
	 * (未解決なら compact ゲートが yield → 呼び元 worker の再走で再入。投函は成功後の 1 回だけ)。
	 * External の is_cache()/serialize() が踏んでいた compact ゲートと同じ位置づけ。 */
	sPtr<pigData> v = d->compact();
	agent->eventHandler(thNEW(stdEvent,(TSE_PACKET, ifThis,
		sPtr<stdObject>(thNEW(ptsMediatorPacket,(C_ARG_DATA, (uint32_t)idx, v, thNULL))))));
	return 0;
}

/* C_ARG_END = 計算開始 (§2.1)。目標パスは outCache 自身が持つので str には載せない (§5.2:
 * C_ARG_END の正本は data = 出力 pigDataCache。External も forward_packet で同じ形にした)。
 * W_END 番兵は Internal には存在しない — ワイヤが無いので「送信の終わり」を別レコードで
 * 知らせる必要が無い (旧 pl_wend は元から no-op だった)。 */
int
ptsMediatorInternal_::pl_write_end(sPtr<pigDataCache> outCache)
{
	if ( agent == thNULL )
		return -1;
	this->outCache = outCache;   /* 戻りのために保持 (§3.2 と対) */
	agent->eventHandler(thNEW(stdEvent,(TSE_PACKET, ifThis,
		sPtr<stdObject>(thNEW(ptsMediatorPacket,(C_ARG_END, 0, outCache, thNULL))))));
	return 0;
}

/* 結果 (A_SAVE_BEGIN / A_SAVE_DONE / A_ERROR) を parent (pigfAgent) へ投函する内部ヘルパ。
 * §2.1 で Mediator 界面から a_write が消えたので、これは Internal だけの private な道具
 * (実行体はこれを呼ばない — 結果は TSE_RETURN で返す)。pigData のまま = 文字列化しないので
 * エラーの fatal ビットも構造化された値もそのまま届く (2026-07-30 メモ L651)。 */
int
ptsMediatorInternal_::a_write(int cmd, sPtr<pigData> d)
{
	if ( parent.is_notNull() ) {
		parent->eventHandler(thNEW(stdEvent,(TSE_PACKET, ifThis,
			sPtr<stdObject>(thNEW(ptsMediatorPacket,(cmd, 0, d, thNULL))))));
		return 0;
	}
	return -1;
}

/* 実行体の強制終了 (§4.1: 「a->destroy() がいい」)。
 * ★ 旧実装は実行体へ TSE_RETURN を投函して「wire の EOF」を模していたが、§6.2 で実行体側の
 *   TSE_RETURN 受け口が消えた (実行体が受ける TSE_RETURN はもう無い) ので、あれは**誰も見ない
 *   イベント**になっていた。destroy() なら is_destroyed() が立つ。
 * ☐ TODO(§6.1): 実行体が計算中だと状態関数が is_destroyed() を見ていないので即座には畳まれない。
 *   「destroy を前提にした状態遷移」を入れて初めて途中終了が効く。 */
void
ptsMediatorInternal_::teardown()
{
	if ( agent.is_notNull() ) {
		sPtr<ptsAgent> a = agent;
		agent = thNULL;   /* 以後 実行体の FIN 通知 (TSE_RETURN) は ACT_START が無視する */
		a->destroy();
	}
}


/*******************************************
	STATE MACHINE
********************************************/

TS_STATE(ACT_START)
{
	/* ★ destroy の作法 (ひさ指示 2026-08-06・2026-08-02 メモ §8.3 の PIGFAGENT_SHOULD_ABORT と同型)。
	 *   ① 子の TSE_RETURN を **先に** 見る
	 *   ② destroy 要求は「子へ destroy() を送って、TSE_RETURN が戻るのを待ち続ける」だけ
	 *   ③ 待つ子が居なくなって初めて FIN へ
	 * destroy を送る側は **戻ってくる内容に関知しない**。destroy された側が自分の終了処理を
	 * 行って TSE_RETURN を返す (即終了するか完走してからかは、その子の判断)。
	 * is_destroyed() は消滅要求フラグにすぎず、FIN へ進むのは状態関数の責任 (MENTAL_MODEL §4.2)。
	 * これを怠ると「何も待っていないのに終わらない tinyState 派生」が終了時まで残る (#3414)。 */
	/* 実行体の TSE_RETURN = 計算終了 (§2.2.2)。msg_obj に結果 (pigDataCache | pigDataError) が
	 * 載っている。destroy 後に返ってきた場合も同じ経路 — 実行体が中断を pigDataError で
	 * 返してくるので (§6.3)、こちらは種別で分岐するだけでよい。 */
	if ( ev->type == TSE_RETURN && agent.is_notNull() && ev->source == agent ) {
		agentResult = sPtr<pigData>::d_cast(ev->msg_obj);
		agent = thNULL;   /* 実行体は畳み終えた (以後 pl_write_* は -1) */
		return rDO|ACT_ptsMediatorInternal_RESULT;
	}
	if ( is_destroyed() ) {
		if ( agent.is_notNull() ) { agent->destroy(); return 0; }   /* TSE_RETURN を待つ */
		return rDO|FIN_START;                                       /* 待つ子は居ない */
	}
	return 0;
}

TS_STATE(ACT_ptsMediatorInternal_RESULT)   /* 結果の種別で分岐 (ptsAgentApplication と対) */
{
	outCache = sPtr<pigDataCache>::d_cast(agentResult);
	if ( outCache == thNULL ) {
		/* pigDataError (またはそれ以外) = エラー。Internal は **pigData のまま**渡すので
		 * fatal ビットもソース位置も落ちない (文字列化する External との違い・L651)。 */
		a_write(A_ERROR, ( agentResult != thNULL ) ? agentResult
		    : sPtr<pigData>(thNEW(pigDataError,("agent returned no result"))));
		return rDO|ACT_ptsMediatorInternal_DONE;
	}
	return rDO|ACT_ptsMediatorInternal_SAVEBEGIN;
}

TS_STATE(ACT_ptsMediatorInternal_SAVEBEGIN)   /* メタ書込済 = 下流が attach 可 になったら送る */
{
	/* is_valid() は「問い合わせ + 購読」を兼ねる (走行中なら TSE_ASSERT で起こしてくれる)。 */
	if ( ! outCache->is_valid() ) { return 0; }
	/* payload は本文の形で決める (ptsAgentApplication::SAVEBEGIN と同一基準): ストリーム系
	 * (D_CHUNK/D_REF) は空 — planner は共有 outCache ハンドルで受け取る。値 (D_TEXT) は
	 * **構造化 pigData をそのまま**載せる (文字列化しないのが Internal の狙い = vparser 不要)。
	 * get_body() は sException で yield し得るので、ここまでこの状態関数は冪等。 */
	sPtr<pigData> body = outCache->get_body();
	if ( body != thNULL && ptsApp != thNULL && ptsApp->module_registry != thNULL
	     && ! ptsApp->module_registry->codecs.is_stream_body(body) )
		a_write(A_SAVE_BEGIN, body);
	else
		a_write(A_SAVE_BEGIN, thNULL);
	return rDO|ACT_ptsMediatorInternal_SAVEDONE;
}

TS_STATE(ACT_ptsMediatorInternal_SAVEDONE)   /* 本体まで書き終わったら送る */
{
	if ( ! outCache->is_complete() ) { return 0; }
	a_write(A_SAVE_DONE, thNULL);
	return rDO|ACT_ptsMediatorInternal_DONE;
}

TS_STATE(ACT_ptsMediatorInternal_DONE)
{
	/* §4.2 *6 / §2.2.1: 自分の終了を parent へ TSE_RETURN で必ず 1 回。pigfAgent の BYE が
	 * これを待っている (External では pipe FIN の TSE_RETURN が同じ位置に来る)。 */
	if ( ! retSent ) {
		retSent = 1;
		if ( parent.is_notNull() )
			parent->eventHandler(thNEW(stdEvent,(TSE_RETURN, ifThis, (INTEGER64)0)));
	}
	/* ★ 送ったら自分で畳む (ひさ指摘 2026-08-06)。もう待つものは何も無い。以前は「破棄は
	 * pigfAgent の FIN が med->destroy() で行う」として return 0 で座り込んでいたが、
	 * destroy() は要求フラグを立てるだけなので、この状態は永久に終わらなかった。 */
	return rDO|FIN_START;
}

TS_STATE(FIN_START)
{
	teardown();
	/* 事故で結果まで辿り着けなかった場合でも TSE_RETURN は必ず 1 回 (§1)。 */
	if ( ! retSent ) {
		retSent = 1;
		if ( parent.is_notNull() )
			parent->eventHandler(thNEW(stdEvent,(TSE_RETURN, ifThis, (INTEGER64)0)));
	}
	agentResult = thNULL;   /* §9: 終了時点で手放す */
	outCache    = thNULL;
	return rDO|FIN_ptsObject_START;
}
