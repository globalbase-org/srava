/*
 * ptsApplication — piggybackTurtle の tinyState 系 実態元祖クラス。
 * pig フレームワーク全体で参照するグローバル機能(将来: pigDataCache の同一チェックリスト等)を
 * public に置く。実態親は tsApplication。自分自身を ptsApp に立てる。
 *
 * 基底は **ptsObject** (2026-08-02 メモ §1)。かつては ptsMediator を挟んでいたが、
 * ptsAgent の parent が ptsMediator である必然性が §5/§6 で消え (実行体は parent の a_write を
 * 呼ばず TSE_RETURN で結果を返すようになった)、この系統から Mediator 界面を取り除けた。
 * ptsApplication は ptsMediator の何一つ override していなかった = 純粋な名残りだった。
 * agent process の通信部材 (rio/wio/pipe/実行体) とワイヤ↔pigData 変換は派生の
 * **ptsAgentApplication** が自前で持つ (planner の親クラスと同居させない・2026-07-30 メモ L410)。
 *
 * 派生(プロセスごとの実態元祖 ptsAgentApplication / cgptsPlanner)用に gate を提供する:
 *   INI: INI_ptsObject_START(ptsApp=自分)→ INI_ptsApplication_START(派生 init)→ ACT_START
 *   FIN: 派生は FIN_START → FIN_ptsApplication_START → FIN_ptsObject_START と畳む
 *
 * 全 agent(pigfAgent)の死活・エラー集約(プランナーが「全 agent クリーン」を知るための機構):
 *   - countAgent     : 生存中の pigfAgent 数。pigfAgent が INI で agent_enter()、FIN で agent_leave()。
 *   - agentError     : promise 解決後に agent が出したエラー(継続を既に返した後なので promise では
 *                      返せない)。set_agentError() で集約(先勝ち)。
 *   pigfAgent は A_SAVE_BEGIN で継続を解決した時点で「結果はもう呼び元へ渡った」ので、その後は
 *   ptsApp に listen(TSE_UPDATED) し、set_agentError の invoke_listen で起こされて自分を撤収できる。
 *   agent_leave() は countAgent==0 で wakeup() → プランナーの WAITAGENTS を起こす。
 */
#include	"ts2/c++/tsApplication.h"
#include	"ts2/c++/stdLimitSemaphore.h"   /* ワーカーゲートの入場制御セマフォ(gateSem) */
#include	"ts2/c++/sCallSection.h"        /* pig_current_registry(): TLS 経由で caller を引く */
#include	"pig/c++/pigData.h"      /* agentError の pigData(完全型) */
#include	"pig/c++/ptsObject.h"    /* 基底 (§1: Mediator を外した) */
#include	"pig/c++/pigModuleRegistry.h"
#include	"pig/c++/ptsMediator.h"   /* module_name() (pig_current_module_id) */
#include	"pig/c++/ptsLoadControl.h"   /* #3419 §2.4 / §13.7 */
#include	"pig/c++/osglue.h"           /* #3419 §12: osglue_now_ms (サンプリング窓) */
#include	"pig/c++/pigModule.h"        /* ★ #3427 ③: app 所有レジストリ (INI で thNEW) */
#include	"ts2/c++/stdEvent.h"
#include	"_ts2/c++/ptsApplication_.h"

#include	<unistd.h>
#include	<thread>            /* std::thread::hardware_concurrency() — CPU 数(portable/MinGW 対応) */
#include	<stdio.h>           /* /proc/meminfo 読み(空きメモリ watermark) */
#include	<stdlib.h>          /* getenv/atoi */
#include	<string.h>          /* strcmp (SRAVA_GATE_ORDER の解釈) */
#if defined(__GLIBC__)
#include	<malloc.h>          /* mallinfo2 — 走行中のピーク実使用 (#3419) */
#endif
#include	<string>            /* INI のモジュールロード (エラー文字列) */

/* ptsDataCache.cpp: pigDataCache の I/O helper 生成子 (旧: 静的初期化で pigData の
 * グローバルフックへ登録していたものを、app INI からレジストリへ明示登録する形へ・#3427 ③)。 */
extern pigDataCacheHelperFn ptsDataCache_helper();

CLASS_TINYSTATE(pig/c++/ptsApplication,pig/c++/ptsObject)


#if 0

TS_BEGIN_IMPLEMENT


class TS_THISCLASS : public TS_BASECLASS {
public:
	/* ★ #3427 ③: モジュールロード指示を ctor で受け、INI_ptsObject_START が実行する。
	 *   _moduleMode: PIG_MODLOAD_NONE(既定・テスト/smoke) / _SEARCH(planner=探索路) /
	 *                _FILE(agent・probe=単一 .so を RTLD_NOW)。
	 *   _moduleFile: _FILE のときの .so パス。 */
	ptsApplication_(
		sPtr<tsApplication> parent,
		int _moduleMode = 0 /*PIG_MODLOAD_NONE*/,
		const char *_moduleFile = 0);

	sRptr<tsApplication,tinyState>		parent;

	/* ★ #3427 ③: レジストリ (モジュール/型/agent/codec/backend/値パーサのハブ) は app が所有する。
	 * INI_ptsObject_START で thNEW し、プロセス全体の可変 static を全廃 (リエントラント化)。
	 * pts 系は `ptsApp->module_registry->…`、素の pigData 層は pig_current_registry() で引く。 */
	sPtr<pigModuleRegistry>	module_registry;
	sPtr<ptsLoadControl>	load_control;   /* ★ #3419 §2.4 (§13.7 で tinyState 実体へ) */
	/* INI でのモジュールロード失敗 (PIG_MODLOAD_FILE のみ)。派生 (ptsAgentApplication) が
	 * INI gate で見て即 FIN し、main が終了コードに写す。 */
	int			module_load_failed();

	/* pigfAgent ライフサイクル集約(public: pigfAgent が ptsApp 経由で叩く)。 */
	void			agent_enter(sPtr<tinyState> who);   /* INI: 生存数 ++ + 登録 */
	void			agent_leave(sPtr<tinyState> who);   /* FIN: 生存数 --。0 で wakeup() */
	int			agent_count();              /* 現在の生存数 */
	void			set_agentError(sPtr<pigData> e);  /* エラー集約(先勝ち)+ 全 agent を起こす */
	sPtr<pigData>		get_agentError();           /* 集約済みエラー(無ければ thNULL) */
	/* ★ agent のエラーを **記録だけ**する (2026-08-26・ひさ提案)。
	 * set_agentError は「最初の 1 件」しか保たない (先勝ち) ので、**落ちた本人の理由が
	 * 傍観者の汎用エラーに負けて消える**ことがあった。async の continue-and-collect と同じく
	 * 全部溜めて末尾で列挙する。
	 * ⚠ **起こさない・撤収トリガにしない** — 撤収の意味論は set_agentError のままにする
	 *   (wake-all の連鎖嵐を避ける・§8.3 の設計を変えない)。
	 * ⚠ 同一文言は畳む (撤収で多数の agent が同じ "aborted" を出すため)。上限あり。 */
	void			record_agentError(sPtr<pigData> e);
	int			agent_error_count();
	sPtr<pigData>		agent_error_at(int i);

	/* 使用済みキャッシュの登録簿(= pigDataCache の同一チェックリスト/global dedup list)。
	 * pigfAgent が cache を生成/ヒットするたび hash を登録。プランナー終了時、登録外の
	 * 完了キャッシュ(= この run で使われなかったもの)を削除するのに使う(1.2.5)。 */
	void			cache_use(pHashKeyType h);  /* 使用登録(重複は無視) */

	/* キャッシュ HIT/MISS 計数(pigfAgent が HIT=既存キャッシュ短絡 / MISS=agent 起動 時に増やす)。
	 * プランナー終了時にサマリを出して「再利用されたか/再計算したか」を見える化する。 */
	void			cache_hit();
	void			cache_miss();
	int			cache_hits();
	int			cache_misses();
	/* 起動時キャッシュ sweep を 1 回だけ走らせる once フラグ(per-app=per-planner)。初回のみ 1 を返し
	 * フラグを立てる(2 回目以降 0)。pigfAgent が最初の agent 頭で使う(旧グローバル g_cacheSwept)。 */
	int			cache_take_startup();
	/* キャッシュ版数ゲートの指紋。srava(cgptsPlanner)が「何が変わったら無効か」を決めて INI で設定し、
	 * pigfAgent が pigCacheManager::startup_sweep へ渡す。空=版ゲート無効。 */
	const char *		cache_fingerprint();
	void			cache_set_fingerprint(const char *fp);

	/* ★ in-flight 重複生成 dedup: 同一 hashVal を計算する pigfAgent が複数あるとき、最初の 1 つだけが
	 * agent を起動し、その promise を登録する。次点以降は inflight_lookup で最初の promise を取り、
	 * 自分の promise をそれに解決(受け売り)して計算しない(= 同一キャッシュへの複数 writer 競合を根絶)。
	 * inflight_claim: 未登録なら p を登録して thNULL を返す(=自分が最初)。登録済みなら既存 promise を返す
	 * (=次点)。get/put を 1 呼び出しに畳む(planner 単一スレッドなので get→put 間の割込みなし=原子的)。 */
	sPtr<pigData>		inflight_claim(pHashKeyType h, sPtr<pigData> p);
	/* ★ #3419 (2026-08-22): 計算が終わったら台帳から外す。**in-flight の間だけ必要な表**で、
	 * 完了後は後続が通常のキャッシュ HIT 経路で拾えるため、残す意味が無い。
	 * ⚠ 外さないと、登録された _front (= DAG ノード) が program 終了まで生き、その result の
	 *   継続 pair → promise → pigDataCache → メッシュ実体まで丸ごと滞留する。 */
	void			inflight_release(pHashKeyType h);

	/* ★ ワーカーゲート: 同時 fork 数を stdLimitSemaphore(gateSem)で上限管理(fork EAGAIN/OOM 防止)。
	 * pigfAgent が LAUNCH 直前に gate_get() で入場(満杯なら get が yield→release/limit 拡大で再起動)、
	 * FIN で gate_release()。2 レジーム判定(低圧 progressive / 高圧 全引数 ready 化)と T=cap/2 は
	 * pigfAgent 側で gate_live()/gate_cap() を見て行う。待ち行列はセマフォ内部 stdQueue が管理(wakeup レース無し)。 */
	void			gate_get();           /* 入場(セマフォ取得)。満杯なら yield(release で再起動) */
	void			gate_release();       /* 退場(解放)。limit 固定なのでセマフォを解放するだけ */
	/* ★ #3419 §12.8: 稼働中 agent 数の増減 (C_AGENT)。**種別で分けない** — thread_kind を
	 * 撤去したため (ひさ判断 2026-08-21)。 */
	void			load_agent_enter();
	/* ★ #3419 T4-b: agent の実 pid を登録/解除する (C_MEM の集計に使う)。
	 * in-proc は planner に含まれるので pid=0 は無視する。 */
	void			load_pid_add(uint32_t pid);
	void			load_pid_del(uint32_t pid);
	/* ★ #3419 §14.9: in-proc agent は planner と同一アドレス空間で RSS では区別できないので
	 * **数える** (契機は ptsMediatorInternal の enable/teardown)。 */
	void			load_inproc_add();
	void			load_inproc_del();
	void			load_agent_leave();
	/* ★ #3419 §2.1: L_AGENT をゲートの limit へ反映する。 */
	void			load_apply_agent_limit();
	/* ★ #3419 §17: CPU 項のための**周期**サンプル。co_ptsLoadControl が 250ms ごとに呼ぶ。
	 * ⚠ ゲートの入退場だけを契機にすると観測の窓が開きすぎて、制御に使える信号にならない。 */
	void			load_sample_cpu();

	/* ★ #3419 §17.3 (ひさ案 2026-08-24): **設定を srava の変数からも読めるようにする**。
	 * CACHE_DIR / CACHE_RETAIN と同じ流儀: planner が env 変数を「環境変数を初期値に」事前定義し、
	 * 使う側は **変数を優先・無ければ環境変数・無ければ既定** の順で解決する。
	 * これで `LOAD_CPU = 50;` のようにスクリプトから設定でき、意味は環境変数と同じになる。
	 * ⚠ 効くのは「使うたびに読む」設定だけ。起動時に 1 度しか読まないものは別 (§17.3 の表)。 */
	void			set_root_env(sPtr<pigEnvironment> e);
	sPtr<pigEnvironment>	root_env();
	/* ★ 解決順 = **srava 変数 → 環境変数 → 既定**。環境変数名と既定は **設定表** が持つ
	 * (pigcfg_table)。呼び手は変数名だけ渡す = 既定の二重管理をしない。 */
	int			cfg_int(const char *var);
	sPtr<stdString>		cfg_str(const char *var);
	/* ★ #3419 §17.3: ゲート側の設定 (GATE_ORDER) を srava 変数から反映し直す。
	 * co_ptsConfigWatch → ptsLoadControl::refresh_config() 経由で周期的に呼ばれる。 */
	void			refresh_gate_config();
	void			gate_backoff();       /* (未使用・将来用)fork EAGAIN 時の実効 cap 縮小 */
	int			gate_mem_wait();      /* (未使用・将来用)低メモリ かつ 稼働中 → 入場前に待つべきか */
	int			gate_peak();          /* ★ 走行中の同時 agent 数の最大 (サマリ表示用) */
	int			gate_cap();           /* soft 上限(=セマフォ limit。サマリ表示用) */
	int			gate_cap_dyn();       /* 実効上限(=セマフォ limit。limit 固定なので gate_cap と等価) */
	int			gate_live();          /* 今 fork して生きている agent 数(=セマフォ count) */
	/* ★ #3419 (2026-08-23) ゲート入場順序の実験: agent の**生成通し番号**を配る。
	 * pigfAgent が INI で 1 度だけ取り、priority() の元にする (後発ほど小さい priority = LIFO 近似 =
	 * 深さ優先寄り)。app 所有の可変メンバなので複数 planner 同居でも混ざらない (#3427 のリエントラント方針)。 */
	int			agent_next_seq();
	/* ★ #3419 §16.14: 「枠を握ったまま入力を待っていた時間」の集計 (pigfAgent が 1 回ずつ報告)。 */
	void			gate_note_idle(long long ms);
	void			gate_idle_stats(long long *sum_ms, long long *max_ms, int *n_agents, int *n_waited);
	/* ★ #3419: 走行中のピーク実使用 / ピーク RSS を取り出す (サマリ表示用)。 */
	void			gate_peak_memory(unsigned long long *live_out, unsigned long long *rss_out);
	/* ゲート待ち行列を priority 順にしているか (0=先着順・既定 / 1=priority 順)。
	 * pigfAgent はこれを見る必要はない (priority() は常に返してよく、無効なら参照されないだけ)。
	 * サマリ表示と、実験条件をログに残すために公開する。 */
	int			gate_order_lifo();

private:
	int			mem_ok();     /* 空きメモリが watermark 以上か(取得不能環境は常に真) */
protected:
	int			countAgent;
	int			countHit;
	int			countMiss;
	int			cacheSwept;          /* 起動時 sweep の 1 回フラグ(旧グローバル g_cacheSwept・per-app) */
	char			cacheFingerprint[512];   /* 版数指紋(srava が設定)。空=版ゲート無効 */
	sPtr<pigData>		agentError;
	/* ★ 記録した agent エラー全部 (先勝ちの agentError とは別・文言で重複排除)。 */
	sArray<sPtr<pigData> >	agentErrors;
	sArray<INTEGER64>	usedCaches;   /* 使用済み cache hash(append-only, 重複なし) */
	sArray<INTEGER64>	inflightKeys;     /* in-flight キャッシュ hash(dedup 用) */
	sArray<sPtr<pigData> >	inflightPromises; /* 対応する最初の pigfAgent の promise */
	/* 起動した pigfAgent の登録簿(append-only)。set_agentError 時にここ全員を wakeup して
	 * SHOULD_ABORT 撤収させる(継続解決前で listen していない=イベント待ちで詰まった agent にも届く)。
	 * FIN 済みは is_destroyed() でスキップ。要素削除はしない(寿命中の総 agent 数だけ・小さい)。 */
	sArray<sPtr<tinyState> >	liveAgents;
	/* ワーカーゲート状態(同時 fork 数の制御はセマフォに集約)。 */
	int			gateCap;       /* **初期**上限(= ランプ開始値。サマリ/プログレッシブ閾値 T=cap/2)。 */
	sPtr<pigEnvironment>	rootEnv;       /* ★ #3419 §17.3: planner の根 env (設定の上書き元) */
	int			agentSeq;      /* ★ agent 生成通し番号のカウンタ(agent_next_seq が ++ して返す) */
	long long		gateIdleSum;   /* ★ #3419 §16.14: 握ってから全引数が揃うまでの延べ ms */
	long long		gateIdleMax;   /* 同・最大 */
	int			gateIdleN;     /* 報告した agent 数 (遅延引数を持つもの) */
	int			gateIdleWaited;/* うち 1ms 以上待った数 */
	/* ★ #3419 (2026-08-23・bench 提案): 走行中の **ピーク実使用** (mallinfo2 の uordblks) と
	 * ピーク RSS。ゲートの入退場ごとに標本を取る。
	 * ⚠ ピーク RSS だけでは「同時生存量」を測れない — allocator の未返却・arena 分散・断片化が
	 *   高水位を作るので、順序を変えても動かない可能性がある。uordblks は allocator を経由しない
	 *   実使用そのものなので、「同時生存中間結果が減ったか」という仮説の当の量を直接測れる。
	 * ★ 両者を並べると **差がそのまま allocator の水増し**になる (§15.6 の定量化)。 */
	unsigned long long	peakLiveBytes;
	unsigned long long	peakRssBytes;
	void			mem_peak_sample();   /* 標本 1 点(安い。glibc 以外では RSS のみ) */
	int			gateLifo;      /* ★ ゲート待ち行列を priority 順にしたか(SRAVA_GATE_ORDER=lifo) */
	sArray<uint32_t>	loadPids;     /* ★ #3419: 稼働中の agent プロセス pid */
	unsigned		loadN;        /* 稼働中 agent 数 (§12.8: 種別で分けない) */
	unsigned		loadPeak;     /* ★ その走行での同時 agent 数の最大 (サマリ用) */
	unsigned		loadInproc;   /* ★ #3419 §14.9: うち in-proc で走っている数 */
	int			memMarginMB;   /* 空きメモリの下限(これを切ったら入場拒否)。0=メモリゲート無効 */
	sPtr<stdLimitSemaphore>	gateSem;   /* 入場制御。count=今 fork して生きてる数, limit()=実効cap(EAGAIN で可変)。 */
	/* ★ #3427 ③: INI で実行するモジュールロードの指示 (ctor 引数・PIG_MODLOAD_*)。 */
	int			moduleMode;
	sPtr<stdString>		moduleFile;        /* PIG_MODLOAD_FILE の .so パス */
	int			moduleLoadFailed;  /* INI の load_file 失敗 (agent は起こさず終了コード 1) */
	TS_DEFARGS
};

TS_END_IMPLEMENT

TS_BEGIN_INTERFACE
#include	"ts2/c++/sRptr.h"
#include	"ts2/c++/sArray.h"
#include	"ts2/c++/stdLimitSemaphore.h"   /* gateSem(sPtr メンバ)の完全型 */
#include	"pig/c++/pigData.h"   /* pHashKeyType(=INTEGER64)をメンバ/引数型に */
class tsApplication;
class tinyState;
class pigData;
class stdString;
class stdLimitSemaphore;
class pigModuleRegistry;   /* ★ #3427 ③: module_registry(sPtr メンバ)。消費側が完全型を include する */
class ptsLoadControl;      /* ★ #3419 §2.4: 負荷コントロール (app 所有) */
TS_END_INTERFACE

#endif


ptsApplication_::ptsApplication_(TS_ARGS0)
        : ptsObject_(parent),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
    moduleMode = _moduleMode;
    moduleFile = ( _moduleFile != 0 ) ? thNEW(stdString,(_moduleFile)) : sPtr<stdString>(thNULL);
    moduleLoadFailed = 0;
    countAgent = 0;
    countHit   = 0;
    countMiss  = 0;
    cacheSwept = 0;
    cacheFingerprint[0] = 0;

    /* ---- ワーカーゲートの初期上限を決定 ----
     * ★ #3419 (ひさ設計 2026-08-30): **静的天井 PIG_MAX_WORKERS を撤去した**。
     *   上限の決定は ptsLoadControl (V_CPU = SRAVA_LOAD_CPU / SRAVA_LOAD_AGENT) の 1 箇所に集約。
     *   ⚠ ここで作る limit は「ロード制御が最初の値を入れるまでの初期値」でしかない。
     *   ランプの開始値と同じ SRAVA_LOAD_RAMP_START を使う (ロード制御が直後に同じ値を入れる)。
     *   以前は ncpu*4 で始めていたので、最初の apply までの一瞬だけ広く開いていた。
     * ★ limit は**固定**方針。fork 上限を EAGAIN から学習することはしない(fork が cap に届かない時は
     *   backoff せず即エラー — pigfAgent LAUNCH 参照)。gate_backoff() は (未使用・将来用) で
     *   どこからも呼ばれない。入場制御は stdLimitSemaphore に集約。
     *   ※ 旧実装は EAGAIN で backoff → AIMD で回復する適応 cap だったが撤去済み (gate_release 参照)。
     *      この段落を書き換えるときは gate_release()/gate_backoff() の実装と突き合わせること。 */
    int cap = 2;
    {
        const char *e = ::getenv("SRAVA_LOAD_RAMP_START");
        if ( e != 0 && e[0] != 0 && ::atoi(e) > 0 ) cap = ::atoi(e);
    }
    if ( cap < 1 ) cap = 1;
    gateCap = cap;
    loadN = 0;
    loadPeak = 0;
    loadInproc = 0;
    gateSem = thNEW(stdLimitSemaphore,(cap));   /* limit=cap(固定)・count=生存 agent 数。待ち行列内蔵。 */
    /* ---- ★ #3419 (2026-08-23): ゲートの**入場順序**。既定は従来どおり先着順(FIFO)。
     *   SRAVA_GATE_ORDER=lifo で待ち行列を priority() 順にする(tinyState #3449 の enablePriority)。
     *   pigfAgent::priority() は「生成通し番号の負値」を返すので、**後から生まれた agent ほど先に入場**
     *   = DAG を深さ優先寄りに掘る。狙いは同時に生きる中間結果を減らすこと(ピーク削減)と、
     *   過負荷時に「一つずつ確実に終わらせて完成キャッシュを残す」こと。
     *   ⚠ 対照の口として残す(消さない): fifo/lifo をこの env だけで切り替えて比較できるようにする。 */
    agentSeq = 0;
    gateIdleSum = 0;
    gateIdleMax = 0;
    gateIdleN = 0;
    gateIdleWaited = 0;
    peakLiveBytes = 0;
    peakRssBytes = 0;
    gateLifo = 0;
    {
        /* ⚠ ここは rootEnv がまだ無い (planner の env 生成前) ので環境変数だけ。
         * srava 変数からの上書きは refresh_gate_config() が周期的に反映する (§17.3)。 */
        const char *go = ::getenv("SRAVA_GATE_ORDER");
        if ( go != 0 && ::strcmp(go, "lifo") == 0 )
            gateLifo = 1;
        gateSem->enablePriority = gateLifo;
    }
    memMarginMB = 1024;    /* (未使用・将来用) 低メモリ時に入場を絞る際の閾値。
                            * ★ この経路は現在**無効**: pigfAgent の GATE が mem_ok()/gate_mem_wait() を
                            *   呼んでいない (fork 同様デッドロック源のため当面無効・IF だけ残置)。 */
    const char *m = ::getenv("PIG_MEM_MARGIN_MB");
    if ( m != 0 && ::atoi(m) >= 0 ) memMarginMB = ::atoi(m);
}

/* 空きメモリが watermark 以上か。/proc/meminfo の MemAvailable を見る。
 * 取得不能(macOS 等)や memMarginMB<=0 のときは常に真 = メモリゲート無効(プロセス数のみで絞る)。 */
int
ptsApplication_::mem_ok()
{
	if ( memMarginMB <= 0 ) return 1;
	FILE *f = ::fopen("/proc/meminfo", "r");
	if ( f == 0 ) return 1;
	long availKB = -1;
	char line[256];
	while ( ::fgets(line, sizeof line, f) != 0 )
		if ( ::sscanf(line, "MemAvailable: %ld kB", &availKB) == 1 ) break;
	::fclose(f);
	if ( availKB < 0 ) return 1;
	return ( availKB / 1024 ) >= memMarginMB;
}

/* 低メモリ かつ 稼働中 agent あり → 入場前に少し待つべき(OOM 保険)。計算中 0 のときは 1 個は通す
 * (前進保証)。取得不能環境(macOS)は mem_ok が常に真なので常に 0。
 * ★ (未使用・将来用) 現在この関数を呼ぶ者は居ない。pigfAgent の GATE はメモリによる入場制限を
 *   行わない (fork 同様デッドロック源のため当面無効)。将来有効化するなら「真なら短い待ち→再評価」。 */
int
ptsApplication_::gate_mem_wait()
{
	return ( ! mem_ok() && gateSem->count > 0 ) ? 1 : 0;
}

int
ptsApplication_::gate_peak()
{
	return (int)loadPeak;      /* 走行中の同時 agent 数の最大 */
}

int
ptsApplication_::gate_cap()
{
	return gateCap;            /* 初期上限(= ランプ開始値)。プログレッシブ閾値 T=cap/2 / サマリ表示。 */
}

int
ptsApplication_::gate_cap_dyn()
{
	return gateSem->limit();   /* 実効上限(EAGAIN で下がり AIMD で回復)。サマリ用。 */
}

int
ptsApplication_::gate_live()
{
	return gateSem->count;     /* 今 fork して生きている agent 数 = セマフォ取得数。 */
}

/* ★ #3419 (2026-08-23): ピーク実使用 / ピーク RSS の標本を 1 点取る。
 * ゲートの入退場ごとに呼ぶ (実効上限が小さいので入退場は密 = 十分な標本密度になる)。
 * mallinfo2 は glibc のみ。無い環境では RSS だけ更新する (指標が片方になるだけで壊れない)。 */
void
ptsApplication_::mem_peak_sample()
{
#if defined(__GLIBC__)
	{
		struct mallinfo2 mi = ::mallinfo2();
		unsigned long long live = (unsigned long long)mi.uordblks;
		if ( live > peakLiveBytes ) peakLiveBytes = live;
	}
#endif
#if defined(__linux__)
	/* ⚠⚠ ここで `osglue_proc_memory` を呼んではいけない (2026-08-23 に実測で踏んだ)。
	 *   既定の metric が **Pss** で、`/proc/<pid>/smaps_rollup` はページテーブルを全走査する。
	 *   大きなプロセスでは 1 回が桁違いに高くつき、ゲートの入退場ごとに呼ぶと
	 *   **走行時間が目に見えて延びる** = **計装が測定対象を乱した**。負荷コントロールの
	 *   sample_memory は呼ばれる回数がごく少ないので Pss で構わないが、この高頻度サンプラは別物。
	 * ★ 代わりに `/proc/self/statm` の 2 番目 (resident pages) を読む。全走査が無く桁違いに安い。 */
	{
		FILE *f = ::fopen("/proc/self/statm", "r");
		if ( f != 0 ) {
			unsigned long long total = 0, resident = 0;
			if ( ::fscanf(f, "%llu %llu", &total, &resident) == 2 ) {
				unsigned long long rss = resident * (unsigned long long)::sysconf(_SC_PAGESIZE);
				if ( rss > peakRssBytes ) peakRssBytes = rss;
			}
			::fclose(f);
		}
	}
#endif
}

/* ★ #3419: agent 生成通し番号を 1 つ配る (1 起点)。planner は単一スレッドで agent を作るので
 * 素の ++ でよい (mutex 不要)。app ごとのメンバなので複数 planner 同居でも独立。 */
void
ptsApplication_::gate_peak_memory(unsigned long long *live_out, unsigned long long *rss_out)
{
	if ( live_out ) *live_out = peakLiveBytes;
	if ( rss_out )  *rss_out  = peakRssBytes;
}

/* ★ #3419 §16.14: pigfAgent が「枠を握ってから全引数が揃うまで」を 1 回報告する。 */
void
ptsApplication_::gate_note_idle(long long ms)
{
	if ( ms < 0 ) ms = 0;
	gateIdleSum += ms;
	if ( ms > gateIdleMax ) gateIdleMax = ms;
	gateIdleN++;
	if ( ms >= 1 ) gateIdleWaited++;
}

void
ptsApplication_::gate_idle_stats(long long *sum_ms, long long *max_ms, int *n_agents, int *n_waited)
{
	if ( sum_ms )   *sum_ms   = gateIdleSum;
	if ( max_ms )   *max_ms   = gateIdleMax;
	if ( n_agents ) *n_agents = gateIdleN;
	if ( n_waited ) *n_waited = gateIdleWaited;
}

int
ptsApplication_::agent_next_seq()
{
	return ++agentSeq;
}

/* ゲート待ち行列が priority 順か (実験条件のログ/サマリ用)。 */
int
ptsApplication_::gate_order_lifo()
{
	return gateLifo;
}

/* 入場(セマフォ取得)。count < limit なら count++ して返る。満杯なら get() が sException を投げて yield し、
 * release / limit 拡大で起こされて呼び出し元の状態が再走する。待ち行列はセマフォ内部の stdQueue が管理する
 * (= 旧 gateWaiters 手管理の wakeup 再登録レースが構造的に起きない)。2 レジーム判定は pigfAgent 側。 */
void
ptsApplication_::gate_get()
{
	/* ★ #3419 §17.3: 入場のたびに GATE_ORDER を見る。周期反映 (250ms) だけだと
	 * **短いスクリプトでは代入が間に合わない** (実測: volume(box) 1 個で fifo のままだった)。
	 * 変数 1 個の引き当てなので入場ごとに見ても安い。 */
	refresh_gate_config();
	gateSem->get();
}

/* 退場(解放)。**limit は固定方針**(fork 上限を動的に学習しない)なので、ここでは limit を一切いじらず
 * セマフォを解放するだけ。空き枠 1 つ分の待機者がいれば release が起こす。
 * (旧: AIMD で +1 ずつ soft cap まで回復していたが、物理 fork 上限が後から増える前提は不合理なので撤去。
 *  fork が cap に届かない時は backoff せず即エラーにする方針 — pigfAgent LAUNCH 参照。) */
void
ptsApplication_::gate_release()
{
	/* セマフォを解放するだけ。空き枠 1 つ分の待機者(gate_get で yield 中)がいれば release が起こす。
	 * ★ 新方式(全子 begin 後に親が gate を取る admission 順)では、GATE で待つのは gate_get(セマフォ)
	 *   だけ。高圧 blocker の park(gate_listen/invoke_listen/40ms poll)は不要になったので撤去した。 */
	gateSem->release();
	mem_peak_sample();   /* ★ #3419: 退場のたびにも標本を取る(入退場の両エッジで密に取る) */
}

/* (未使用・将来用)fork EAGAIN 時に実効上限を縮小する案。現在は limit 固定方針(fork 失敗は即エラー)なので
 * 呼ばれない。メモリ容量等を含む動的制御を将来ちゃんと整理する時の足場として残す。 */
void
ptsApplication_::gate_backoff()
{
	int lim = ( gateSem->count - 1 < 1 ) ? 1 : gateSem->count - 1;
	if ( lim < gateSem->limit() )
		gateSem->limit(lim);
	gateSem->release();
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
ptsApplication_::agent_enter(sPtr<tinyState> who)
{
	countAgent++;
	liveAgents.push(who);
}

void
ptsApplication_::agent_leave(sPtr<tinyState> who)
{
	/* ★ #3450 (ひさ整理 2026-08-29): **登録簿の参照をここで手放す**。以前は要素を消さず
	 * (「寿命中の総 agent 数だけ・小さい」)、ZOM 済み agent ~200 個が planner 終了まで生き、
	 * teardown で一斉に refList へ流れ込んでいた。gc が tinyState を畳む途中で状態遷移が動き、
	 * タイマ経由で fwIO が配送オブジェクトを生成・破棄して is_stable() を false に戻すため、
	 * FIN_STABLE_WAIT が自分の churn の切れ目を待つレースになっていた (teardown が時折長く伸びる裾)。
	 * 問題は個数ではなく「生かし続けること」。
	 * agent_leave は FIN 確定後 (med 回収・gate 返却済み) にしか呼ばれず、撤収 wakeup
	 * (set_agentError の走査) が要る窓はもう無い。走査側は is_notNull() を見ているので
	 * 穴が空いても安全・詰め直し不要。 */
	for ( int i = 0 ; i < liveAgents.length() ; ++i )
		if ( liveAgents[i] == who ) { liveAgents[i] = thNULL; break; }
	if ( --countAgent <= 0 ) {
		countAgent = 0;
		wakeup();              /* 全 agent クリーン → プランナーの WAITAGENTS を起こす */
	}
}

int
ptsApplication_::agent_count()
{
	return countAgent;
}

void
ptsApplication_::set_agentError(sPtr<pigData> e)
{
	/* ⚠ ここでは record しない (2026-08-26)。set_agentError は planner の**撤収トリガ**でも
	 * 呼ばれる ("aborted: fatal error" / "interrupted by SIGINT") ので、記録すると列挙に
	 * **内部マーカが混ざる**。記録するのは pigfAgent が自分の理由を作ったときだけ。 */
	int first = ( agentError == thNULL );
	if ( first )                   /* 先勝ち(最初のエラーを保持) */
		agentError = e;
	wakeup();                      /* プランナー(=自分)を起こす */
	if ( first ) {
		/* 生存中の全 agent を起こす。各 agent は待ち状態頭の SHOULD_ABORT で撤収(agent kill+FIN)。
		 * イベント待ちで詰まっている agent(継続解決前で listen していない)にも届かせるのが要点。 */
		for ( int i = 0 ; i < liveAgents.length() ; ++i )
			if ( liveAgents[i].is_notNull() && ! liveAgents[i]->is_destroyed() )
				liveAgents[i]->wakeup();
	}
}

/* agent のエラーを記録だけする (起こさない・撤収トリガにしない)。
 * ★ 同一文言は畳む: 撤収で多数の agent が同じ "aborted" を出すので、そのまま溜めると
 *   末尾の列挙が同じ行で埋まる。★ 上限も置く (壊れ方が「大量出力」にならないように)。 */
void
ptsApplication_::record_agentError(sPtr<pigData> e)
{
	if ( ! e.is_notNull() || ! e->is_error() )
		return;
	if ( agentErrors.length() >= 16 )
		return;
	sPtr<stdString> m = e->get_str();
	if ( ! m.is_notNull() )
		return;
	for ( int i = 0 ; i < agentErrors.length() ; ++i ) {
		sPtr<stdString> o = agentErrors[i]->get_str();
		if ( o.is_notNull() && ::strcmp(o->get_str(), m->get_str()) == 0 )
			return;                /* 同じ文言は 1 度だけ */
	}
	agentErrors.push(e);
}

int
ptsApplication_::agent_error_count()
{
	return agentErrors.length();
}

sPtr<pigData>
ptsApplication_::agent_error_at(int i)
{
	return ( i >= 0 && i < agentErrors.length() ) ? agentErrors[i] : sPtr<pigData>();
}

sPtr<pigData>
ptsApplication_::get_agentError()
{
	return agentError;
}

void
ptsApplication_::cache_use(pHashKeyType h)
{
	for ( int i = 0 ; i < usedCaches.length() ; ++i )
		if ( usedCaches[i] == h )
			return;            /* 既登録 */
	usedCaches.push(h);
}

void ptsApplication_::cache_hit()  { countHit++; }
void ptsApplication_::cache_miss() { countMiss++; }
int  ptsApplication_::cache_hits()   { return countHit; }
int  ptsApplication_::cache_misses() { return countMiss; }
int  ptsApplication_::cache_take_startup() { if ( cacheSwept ) return 0; cacheSwept = 1; return 1; }
const char *ptsApplication_::cache_fingerprint() { return cacheFingerprint; }
void ptsApplication_::cache_set_fingerprint(const char *fp)
{
	if ( fp == 0 ) fp = "";
	::snprintf(cacheFingerprint, sizeof cacheFingerprint, "%s", fp);
}

/* hashVal h について: 既登録なら最初の promise を返す(=次点・受け売りする)。未登録なら p を
 * 登録して thNULL を返す(=自分が最初・計算を進める)。線形探索(unique cache 数ぶん・小)。 */
sPtr<pigData>
ptsApplication_::inflight_claim(pHashKeyType h, sPtr<pigData> p)
{
	for ( int i = 0 ; i < inflightKeys.length() ; ++i )
		if ( inflightKeys[i] == (INTEGER64)h )
			return inflightPromises[i];   /* 先行あり */
	inflightKeys.push((INTEGER64)h);
	inflightPromises.push(p);
	return thNULL;                            /* 自分が最初 */
}

/* ★ 計算完了で台帳から外す (末尾と入れ替えて縮める・順序は不要)。 */
void
ptsApplication_::inflight_release(pHashKeyType h)
{
	int n = inflightKeys.length();
	for ( int i = 0 ; i < n ; ++i )
		if ( inflightKeys[i] == (INTEGER64)h ) {
			inflightKeys[i]     = inflightKeys[n-1];
			inflightPromises[i] = inflightPromises[n-1];
			inflightKeys.length(n-1);
			inflightPromises.length(n-1);
			return;
		}
}


int
ptsApplication_::module_load_failed()
{
	return moduleLoadFailed;
}

/* ★ #3427 ③改 (2026-08-14): 「今の app」= sCallSection caller の parent 遡り。
 * TS_STATE 文脈は eventHandler 全体が call section で包まれる (tinyState.cpp:665) ので
 * caller() は常に有効。caller が ptsObject でなくても親を遡って最初の ptsObject の
 * ptsApp を使う。ts2Parallel worker 文脈でも安全: spawn worker の親は常に _root
 * (tinyState develop-v2 b601451。全 worker 終了まで生存保証) なので、FIN 済み worker で
 * 鎖が切れることはない (旧 pigAppScope TLS 回避はこの修正を受けて撤去)。 */
sPtr<ptsApplication>
pig_current_app()
{
	sPtr<tinyState> c = sCallSection::key->caller();
	for ( int depth = 0 ; c != thNULL && depth < 64 ; ++depth ) {
		sPtr<ptsObject> po = sPtr<ptsObject>::d_cast(c);
		if ( po != thNULL )
			return po->ptsApp;
		c = c->parent;
	}
	return sPtr<ptsApplication>(thNULL);
}

/* ★ **いま走っている op はどのモジュールのものか** を返す (無ければ -1)。
 *
 * ★なぜ要るか (ひさ判断 2026-08-26): モジュール専用の設定 (module(so,{threads:N}) 等) は
 *   **モジュール別**でなければならない。ところが幾何層 (vdGrid 等) は ptsObject 派生ではなく、
 *   自分がどのモジュールの実装として呼ばれているかを知らない。共有ライブラリを 4 モジュールで
 *   共有する openvdb 系では特に効く。
 *
 * ★ 引き方は 2 段 (どちらも **実行時の推測をしない**):
 *   ① in-proc: caller 鎖に ptsMediatorInternal が居るので、その moduleName を使う
 *      (1 プロセスに複数モジュールが同居するので、これが唯一の正解)
 *   ② agent プロセス: メディエータが居ない。**その .so は 1 本だけ**なので、レジストリの
 *      「唯一のモジュール」解決 (resolve_single_or_named(0)) で確定する
 * ⚠ どちらも取れなければ -1 (呼び手は「設定なし」と同じ扱いにする)。 */
int
pig_current_module_id()
{
	sPtr<pigModuleRegistry> reg = pig_current_registry();
	if ( reg == thNULL )
		return -1;
	sPtr<tinyState> c = sCallSection::key->caller();
	for ( int depth = 0 ; c != thNULL && depth < 64 ; ++depth ) {
		sPtr<ptsMediator> md = sPtr<ptsMediator>::d_cast(c);
		if ( md != thNULL ) {
			sPtr<stdString> nm = md->module_name();
			if ( nm.is_notNull() )
				return reg->id_of_name(nm->get_str());
			break;   /* メディエータは居るが名前を持たない = External → 下の単一解決へ */
		}
		c = c->parent;
	}
	return reg->resolve_single_or_named(0);   /* agent プロセス: .so は 1 本 */
}

sPtr<pigModuleRegistry>
pig_current_registry()
{
	sPtr<ptsApplication> app = pig_current_app();
	if ( app == thNULL )
		return sPtr<pigModuleRegistry>(thNULL);
	return app->module_registry;
}

/*******************************************
	STATE MACHINE
********************************************/

TS_STATE(INI_ptsObject_START)   // ptsObject の gate を上書き: 自分が pig 実態元祖
{
	ptsApp = ifThis;            // ifThis = sPtr<ptsApplication>

	/* ★ #3427 ③: レジストリ (モジュール/型/agent/codec/backend/値パーサのハブ) は app が
	 *   ここで生成し所有する。プロセス全体の可変 static を全廃 = 同一プロセス複数 app でも
	 *   レジストリが混ざらない (リエントラント)。 */
	module_registry = thNEW(pigModuleRegistry,());
	/* ★ #3419 §2.4: 負荷コントロール。ptsApplication の起動冒頭で作り、以後ここから使う。 */
	/* ★ §13.7: gateSem を渡す (ランプが直接 limit を書く)。gateSem は ctor で生成済み。 */
	load_control = thNEW(ptsLoadControl,(ifThis, gateSem));
	module_registry->set_pdc_helper(ptsDataCache_helper());

	/* ★ #3427 ②改: モジュールのロードも app の INI で行う (旧: main の bootstrap ラムダ)。
	 *   何をロードするかは ctor 引数 (planner=探索路 / agent・probe=単一 .so / テスト=なし)。 */
	if ( moduleMode == PIG_MODLOAD_SEARCH ) {
		/* ★ #3452 (ひさ設計 2026-08-26): 起動時の**実ロード (dlopen) を撤去**。モジュール数が
		 * 増えるほど dlopen (静的初期化含む) の起動コストが計測値に乗ってくるため、実ロードは
		 * script の module(so) 呼び出しへ完全に委ねる。ここでは探索路の**列挙だけ**行い、
		 * module() の名前解決 (resolve_module_file) が効くようにする (実質ゼロコスト)。
		 * module() を一度も呼ばないスクリプトは、以降どの op も「no module can execute op」で
		 * 明示的にエラーになる (旧: 見つかった全モジュールが暗黙に有効だった)。
		 * 全モジュールを一括で読みたい場合は `include "module/all.sra";` を使う (docs §2.4)。 */
		module_registry->enumerate_search_path();
	} else if ( moduleMode == PIG_MODLOAD_FILE ) {
		/* カーネル .so を dlopen → 記述子から make_agent/型/codec/ソルトを登録 (RTLD_NOW で
		 * 全シンボル解決)。失敗したら実行体を起こせない = 明示エラー (docs §7 検証)。 */
		std::string mlerr;
		const char *mf = ( moduleFile != thNULL ) ? moduleFile->get_str() : 0;
		if ( module_registry->load_file(mf, &mlerr, /*lazy=*/false) == 0 ) {
			::fprintf(stderr, "[pig] cannot load module '%s': %s\n",
			          mf ? mf : "(null)", mlerr.c_str());
			moduleLoadFailed = 1;   /* 派生 (ptsAgentApplication) が INI gate で見て即 FIN */
		}
	}
	return rDO|INI_ptsApplication_START;
}

TS_STATE(INI_ptsApplication_START)   // 派生(ptsAgentApplication / cgptsPlanner)がここを上書きする
{
	return rDO|ACT_START;
}

TS_STATE(ACT_START)             // smoke(単体 ptsApplication 用): ptsApp 自己ハンドル確認 → 自己終了。
{                               // 派生は ACT_START を持たず INI_ptsApplication_START から自前状態へ抜ける
	if ( ptsApp.is_notNull() )
		::printf("[pts] ptsApplication up, ptsApp=self OK\n");
	else
		::printf("[pts] ptsApp NULL (FAIL)\n");
	return rDO|FIN_START;
}

TS_STATE(FIN_START)             // 自分が root(agent process 役 / 単体 smoke)のときの入口。
{                               // 派生は自前の FIN_START から FIN_ptsApplication_START へ畳む
	return rDO|FIN_ptsApplication_START;
}

TS_STATE(FIN_ptsApplication_START)   // 派生用 FIN gate(派生は FIN_START からここへ畳む)
{
	/* ★ #3427 ③: レジストリを手放す (app と同寿命)。ここに来る時点で全 agent は撤収済み。 */
	module_registry = thNULL;
	/* ⚠ §13.7: tinyState 実体なので**必ず destroy する**。タイマ登録が残ると app が終われない。 */
	if ( load_control != thNULL ) { load_control->destroy(); load_control = thNULL; }
	return rDO|FIN_ptsObject_START;
}

/* ═══ #3419: 負荷コントロールとゲートの連携 ═══════════════════════════ */

/* ★ #3419 §12.8: 稼働中 agent を数える。**種別で分けない** (thread_kind を撤去したため)。
 * かつては「スレッドを分岐すると申告したモジュール / しないモジュール」で分けて L_THR の
 * 分母にしていたが、その配分ごと撤去した (ひさ判断 2026-08-21・§12.7 の実測を受けて)。 */
void
ptsApplication_::load_agent_enter()
{
	loadN++;
	/* ★ 2026-08-30: **ピークを覚える**。サマリが終了時点の limit しか出しておらず、
	 *   「実効 N」という語から**走行中の同時数と誤読された** (CGALP 指摘)。
	 *   走行中の同時数はここでしか分からないので、ここで最大を取る。 */
	if ( loadN > loadPeak ) loadPeak = loadN;
	if ( load_control != thNULL ) load_control->set_agents(loadN);
}

void
ptsApplication_::load_agent_leave()
{
	if ( loadN > 0 ) loadN--;
	if ( load_control != thNULL ) load_control->set_agents(loadN);
}

/* ★ #3419 §14.9: in-proc agent の在籍数。**メモリでは測れないので数だけ**を評価式へ渡す
 * (l_agent_mem の分母。分子側は planner の RSS に自然に入っている)。 */
void
ptsApplication_::load_inproc_add()
{
	loadInproc++;
	if ( load_control != thNULL ) load_control->set_inproc(loadInproc);
}

void
ptsApplication_::load_inproc_del()
{
	if ( loadInproc > 0 ) loadInproc--;
	if ( load_control != thNULL ) load_control->set_inproc(loadInproc);
}

void
ptsApplication_::load_pid_add(uint32_t pid)
{
	if ( pid == 0 ) return;
	loadPids.push(pid);
}

void
ptsApplication_::load_pid_del(uint32_t pid)
{
	if ( pid == 0 ) return;
	/* 末尾と入れ替えて縮める (順序は不要)。 */
	int n = loadPids.length();
	for ( int i = 0 ; i < n ; i++ )
		if ( loadPids[i] == pid ) {
			loadPids[i] = loadPids[n-1];
			loadPids.length(n-1);
			return;
		}
}

/* ★ L_AGENT をゲートの limit へ反映する。**目標値**なので、既に入場している分が
 * 上限を上回っていても追い出さない (次の入場から効く)。 */
/* ★ #3419 §17: CPU の標本を 1 点取り、下がっていれば即追従する。
 * pid の集合はここ (app) が持っているので、co_ptsLoadControl はここへ取りに来る。 */
/* ★ #3419 §17.3: planner が起動時に自分の根 env を預ける。 */
void
ptsApplication_::set_root_env(sPtr<pigEnvironment> e)
{
	rootEnv = e;
}

sPtr<pigEnvironment>
ptsApplication_::root_env()
{
	return rootEnv;
}

/* ★ #3419 §17.3: **設定表** — 変数名 / 環境変数名 / 既定値。ここが唯一の真実。
 * planner はこの表を使って srava 変数を **実効値で** 事前定義する (未代入でも読める)。
 * 消費側は cfg_int/cfg_str に変数名だけ渡す。★ 既定をコード中に二重に持たない。 */
const pigCfgEntry *
pigcfg_table(void)
{
	static const pigCfgEntry T[] = {
		/* 変数名            環境変数名                  既定    起動時のみ? */
		{ "LOAD_MEM",        "SRAVA_LOAD_MEM",        "50",   0 },
		{ "LOAD_MEM_MB",     "SRAVA_LOAD_MEM_MB",     "0",    0 },
		/* ★ 既定 rss (ひさ判断 2026-08-24)。★ ini_only=1 (ひさ判断 2026-08-29): 実行中に metric が
		 * 変わると C_MEM の**同じ 1 本の時系列に Pss 由来と VmRSS 由来が混ざる** (Pss <= RSS なので
		 * 段差が入り、負荷判定はそれを「メモリ急増」と読む)。最初の agent 入場時に確定させる。 */
		{ "LOAD_MEM_METRIC", "SRAVA_LOAD_MEM_METRIC", "rss",  1 },
		{ "LOAD_CPU",        "SRAVA_LOAD_CPU",        "90",   0 },
		{ "LOAD_CPU_MS",     "SRAVA_LOAD_CPU_MS",     "250",  1 },
		{ "LOAD_WINDOW_MS",  "SRAVA_LOAD_WINDOW_MS",  "500",  0 },
		{ "LOAD_RAMP",       "SRAVA_LOAD_RAMP",       "1",    0 },
		{ "LOAD_RAMP_MS",    "SRAVA_LOAD_RAMP_MS",    "250",  0 },
		{ "LOAD_RAMP_START", "SRAVA_LOAD_RAMP_START", "2",    1 },   /* #3451: 最初の agent 入場までは代入可 */
		{ "LOAD_AGENT",      "SRAVA_LOAD_AGENT",      "0",    0 },
		{ "LOAD_LOG",        "SRAVA_LOAD_LOG",        "0",    0 },
		{ "GATE_ORDER",      "SRAVA_GATE_ORDER",      "fifo", 0 },
		{ "GATE_WHEN",       "SRAVA_GATE_WHEN",       "auto", 0 },
		{ "GATE_TRACE",      "SRAVA_GATE_TRACE",      "0",    0 },
		{ 0, 0, 0, 0 }
	};
	return T;
}

static const pigCfgEntry *
cfg_find(const char *var)
{
	const pigCfgEntry *t = pigcfg_table();
	for ( int i = 0 ; t[i].var != 0 ; ++i )
		if ( ::strcmp(t[i].var, var) == 0 ) return &t[i];
	return 0;
}

/* 設定の解決: **srava 変数 → 環境変数 → 既定**。CACHE_DIR の流儀。 */
sPtr<stdString>
ptsApplication_::cfg_str(const char *var)
{
	if ( rootEnv.is_notNull() && var != 0 ) {
		sPtr<pigData> v = rootEnv->get_var(thNEW(stdString,(var)));
		if ( v.is_notNull() && ! v->is_error() ) {
			sPtr<stdString> sv = v->get_str();
			if ( sv.is_notNull() && sv->get_str()[0] != 0 ) return sv;
		}
	}
	const pigCfgEntry *e = cfg_find(var);
	const char *ev = ( e != 0 ) ? ::getenv(e->env) : 0;
	if ( ev != 0 && ev[0] != 0 ) return thNEW(stdString,(ev));
	return ( e != 0 && e->def != 0 ) ? thNEW(stdString,(e->def)) : sPtr<stdString>(thNULL);
}

int
ptsApplication_::cfg_int(const char *var)
{
	sPtr<stdString> v = cfg_str(var);
	return ( v.is_notNull() ) ? ::atoi(v->get_str()) : 0;
}

/* ★ #3419 §17.3: ゲート側の設定を srava 変数から反映し直す。 */
void
ptsApplication_::refresh_gate_config()
{
	if ( gateSem == thNULL ) return;
	sPtr<stdString> go = cfg_str("GATE_ORDER");
	int lifo = ( go.is_notNull() && ::strcmp(go->get_str(), "lifo") == 0 );
	if ( lifo != gateLifo ) {
		gateLifo = lifo;
		gateSem->enablePriority = gateLifo;
	}
}

void
ptsApplication_::load_sample_cpu()
{
	if ( load_control == thNULL || gateSem == thNULL ) return;
	load_control->sample_cpu(( loadPids.length() > 0 ) ? &loadPids[0] : 0,
	                         (unsigned)loadPids.length());
	load_control->follow_down();   /* ★ 下げだけ即座に (上げはランプ・§13.7) */
	load_control->log_cpu_term();  /* ★ #3419 §17: CPU 項の推移を見る (SRAVA_LOAD_LOG) */
}

void
ptsApplication_::load_apply_agent_limit()
{
	if ( load_control == thNULL || gateSem == thNULL ) return;
	/* ★ #3419 T4-b: C_MEM を測り直す。
	 * ⚠ **現状は planner 自身のみ**。in-proc agent は planner と同一アドレス空間なので
	 *   これで正しく含まれる (§4.1 のひさ判断) が、**process agent の分は含まれない**。
	 *   理由: agent は ts2System の `sh -c` 経由で起動されるため実体が**孫プロセス**で、
	 *   Mediator が持つ pid は shell のもの。孫の pid を portable に辿る手段が無い。
	 *   → 影響は「C_MEM を過小評価 → L_AGENT が緩くなる」方向。V_MEM は**目標値**なので
	 *     安全側ではあるが、process 実行主体の構成ではメモリ制御が効かない。§9-11。 */
	/* ★ #3419 T4-b: planner 自身 (= in-proc agent を含む) + 稼働中の agent プロセス群。
	 * agent は 2026-08-11 から直接 execvp の子なので実 pid が取れる。 */
	mem_peak_sample();   /* ★ #3419: 入場のたびにピーク実使用/RSS を更新 */
	load_control->sample_memory(( loadPids.length() > 0 ) ? &loadPids[0] : 0,
	                            (unsigned)loadPids.length());
	load_control->sample_cpu(( loadPids.length() > 0 ) ? &loadPids[0] : 0,
	                         (unsigned)loadPids.length());
	/* ★ §13.7: **下げだけ即座に追従する**。上げは ptsLoadControl のタイマ状態が 1 ずつ行う
	 *   (壊れ方が非対称なので制御も非対称: 上げの誤りは OOM = 回復不能・下げの誤りは遅いだけ)。 */
	load_control->follow_down();
	/* ★ §13.7: 目標が上がったなら**眠っているランプを起こす** (常時タイマを張らないため)。
	 *   ptsApp は自分自身への sPtr (INI で ifThis を入れてある)。 */
	if ( load_control->needs_ramp() && ptsApp != thNULL )
		load_control->eventHandler(thNEW(stdEvent,(TSE_ASSERT, sPtr<tinyState>(ptsApp), (INTEGER64)0)));
}
