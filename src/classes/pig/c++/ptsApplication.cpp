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
#include	"pig/c++/pigModuleRegistry.h"   /* ★ #3427 ③: app 所有レジストリ (INI で thNEW) */
#include	"ts2/c++/stdEvent.h"
#include	"_ts2/c++/ptsApplication_.h"

#include	<unistd.h>
#include	<thread>            /* std::thread::hardware_concurrency() — CPU 数(portable/MinGW 対応) */
#include	<stdio.h>           /* /proc/meminfo 読み(空きメモリ watermark) */
#include	<stdlib.h>          /* getenv/atoi */
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
	/* INI でのモジュールロード失敗 (PIG_MODLOAD_FILE のみ)。派生 (ptsAgentApplication) が
	 * INI gate で見て即 FIN し、main が終了コードに写す。 */
	int			module_load_failed();

	/* pigfAgent ライフサイクル集約(public: pigfAgent が ptsApp 経由で叩く)。 */
	void			agent_enter(sPtr<tinyState> who);   /* INI: 生存数 ++ + 登録 */
	void			agent_leave(sPtr<tinyState> who);   /* FIN: 生存数 --。0 で wakeup() */
	int			agent_count();              /* 現在の生存数 */
	void			set_agentError(sPtr<pigData> e);  /* エラー集約(先勝ち)+ 全 agent を起こす */
	sPtr<pigData>		get_agentError();           /* 集約済みエラー(無ければ thNULL) */

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

	/* ★ ワーカーゲート: 同時 fork 数を stdLimitSemaphore(gateSem)で上限管理(fork EAGAIN/OOM 防止)。
	 * pigfAgent が LAUNCH 直前に gate_get() で入場(満杯なら get が yield→release/limit 拡大で再起動)、
	 * FIN で gate_release()。2 レジーム判定(低圧 progressive / 高圧 全引数 ready 化)と T=cap/2 は
	 * pigfAgent 側で gate_live()/gate_cap() を見て行う。待ち行列はセマフォ内部 stdQueue が管理(wakeup レース無し)。 */
	void			gate_get();           /* 入場(セマフォ取得)。満杯なら yield(release で再起動) */
	void			gate_release();       /* 退場(解放)。limit 固定なのでセマフォを解放するだけ */
	void			gate_backoff();       /* (未使用・将来用)fork EAGAIN 時の実効 cap 縮小 */
	int			gate_mem_wait();      /* (未使用・将来用)低メモリ かつ 稼働中 → 入場前に待つべきか */
	int			gate_cap();           /* soft 上限(=セマフォ limit。サマリ表示用) */
	int			gate_cap_dyn();       /* 実効上限(=セマフォ limit。limit 固定なので gate_cap と等価) */
	int			gate_live();          /* 今 fork して生きている agent 数(=セマフォ count) */
private:
	int			mem_ok();     /* 空きメモリが watermark 以上か(取得不能環境は常に真) */
protected:
	int			countAgent;
	int			countHit;
	int			countMiss;
	int			cacheSwept;          /* 起動時 sweep の 1 回フラグ(旧グローバル g_cacheSwept・per-app) */
	char			cacheFingerprint[512];   /* 版数指紋(srava が設定)。空=版ゲート無効 */
	sPtr<pigData>		agentError;
	sArray<INTEGER64>	usedCaches;   /* 使用済み cache hash(append-only, 重複なし) */
	sArray<INTEGER64>	inflightKeys;     /* in-flight キャッシュ hash(dedup 用) */
	sArray<sPtr<pigData> >	inflightPromises; /* 対応する最初の pigfAgent の promise */
	/* 起動した pigfAgent の登録簿(append-only)。set_agentError 時にここ全員を wakeup して
	 * SHOULD_ABORT 撤収させる(継続解決前で listen していない=イベント待ちで詰まった agent にも届く)。
	 * FIN 済みは is_destroyed() でスキップ。要素削除はしない(寿命中の総 agent 数だけ・小さい)。 */
	sArray<sPtr<tinyState> >	liveAgents;
	/* ワーカーゲート状態(同時 fork 数の制御はセマフォに集約)。 */
	int			gateCap;       /* soft 上限(ユーザ設定・サマリ/プログレッシブ閾値 T=cap/2)。AIMD 回復のターゲット。 */
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

    /* ---- ワーカーゲートの上限を決定 ----
     * soft 上限 cap = PIG_MAX_WORKERS or ncpu*4(既定)。RLIMIT でクランプしない(ユーザ設定を尊重)。
     * 実際の fork 上限は予測でなく EAGAIN で適応学習する(gate_backoff が gateSem の limit を実 fork 数へ
     * 下げ、gate_release が AIMD で soft cap まで回復)。入場制御は stdLimitSemaphore に集約。 */
    long ncpu = (long)std::thread::hardware_concurrency();   /* 0=不明 → 下で 4 に */
    if ( ncpu < 1 ) ncpu = 4;
    int cap = (int)(ncpu * 4);
    const char *e = ::getenv("PIG_MAX_WORKERS");
    if ( e != 0 && ::atoi(e) > 0 ) cap = ::atoi(e);
    if ( cap < 1 ) cap = 1;
    gateCap = cap;
    gateSem = thNEW(stdLimitSemaphore,(cap));   /* 上限=実効cap(EAGAIN で可変)・count=生存数。待ち行列内蔵。 */
    memMarginMB = 1024;    /* 既定: 空き 1GB を切ったら入場拒否(OOM 保険) */
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
 * (前進保証)。取得不能環境(macOS)は mem_ok が常に真なので常に 0。pigfAgent が真なら短い待ち→再評価。 */
int
ptsApplication_::gate_mem_wait()
{
	return ( ! mem_ok() && gateSem->count > 0 ) ? 1 : 0;
}

int
ptsApplication_::gate_cap()
{
	return gateCap;            /* soft 上限(ユーザ設定)。プログレッシブ閾値 T=cap/2 / サマリ表示。 */
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

/* 入場(セマフォ取得)。count < limit なら count++ して返る。満杯なら get() が sException を投げて yield し、
 * release / limit 拡大で起こされて呼び出し元の状態が再走する。待ち行列はセマフォ内部の stdQueue が管理する
 * (= 旧 gateWaiters 手管理の wakeup 再登録レースが構造的に起きない)。2 レジーム判定は pigfAgent 側。 */
void
ptsApplication_::gate_get()
{
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
	(void)who;
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
	module_registry->set_pdc_helper(ptsDataCache_helper());

	/* ★ #3427 ②改: モジュールのロードも app の INI で行う (旧: main の bootstrap ラムダ)。
	 *   何をロードするかは ctor 引数 (planner=探索路 / agent・probe=単一 .so / テスト=なし)。 */
	if ( moduleMode == PIG_MODLOAD_SEARCH ) {
		std::string mlerr;
		int nmod = module_registry->load_search_path(&mlerr);
		if ( ::getenv("PIG_FD_VERBOSE") != 0 )
			::fprintf(stderr, "[pig] loaded %d module(s)%s%s\n", nmod,
			          mlerr.empty() ? "" : " last-error: ", mlerr.c_str());
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
	return rDO|FIN_ptsObject_START;
}
