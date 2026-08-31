/*
 * ptsLoadControl — メモリの負荷コントロールと、ゲート上限の**緩やかな立ち上がり** (#3419)。
 *   設計: docs/srava_load_control_design.md §13.7 (現行方針は §13)。
 *
 * ★ 旧 pigLoadControl (素の stdObject) を tinyState 実体へ置き換えたもの (ひさ判断 2026-08-21)。
 *   タイマを持つ必要が出たため。ランプの状態と、その根拠になる測定を**同じ場所に置く**
 *   (別々にすると読み違える — §13.6.1 で bench が min の合成値から項を読もうとした件と同じ構図)。
 *
 * ═══ なぜランプが要るか (§13.6 の実測) ═══════════════════════════════
 * 評価式 L_AGENT = V_MEM/C_MEM × C_AGENT は、**1 点の観測からの外挿**である。
 * C_AGENT=1・C_MEM=13MB (まだ育っていない) のとき「5000 個入る」と答えてしまい、
 * V_CPU 個が一斉に入場し、その後で全員が育つ。**ピークは入場後に決まる**ので、
 * V_MEM を大きく絞ってもピークが変わらなかった (実測・§13.6)。
 *
 * ★ ランプはこの外挿を**測定の列**に変える: 1 個入れて測る → 余裕があれば 2 個 → 測る → …
 *   上限に達したときには、そこまでの実測を経ている。
 *
 * ★ 非対称にする (ひさ判断): **上げは遅く・下げは速く**。
 *   上げを間違えると OOM (回復不能)・下げを間違えても遅くなるだけ (回復可能) だから。
 *
 * ⚠ **ランプ間隔 T は未検証**。T が「agent がメモリを育てきる時間」より短いと、
 *   育つ前に次を入れてしまい**現状の緩慢版にしかならない**。SRAVA_LOAD_RAMP_MS で振れる。
 *   SRAVA_LOAD_RAMP=0 で**ランプ無効** (= 従来どおり目標値を直接入れる) = 対照。
 * ═════════════════════════════════════════════════════════════════════
 */
#include	"pig/c++/osglue.h"
#include	"ts2/c++/stdLimitSemaphore.h"
#include	"ts2/c++/stdInterval.h"
#include	"pig/c++/ptsApplication.h"   /* ptsObject_ の ptsApp 値メンバの完全型 (他 pts と同じ作法) */
#include	"pig/c++/co_ptsLoadControl.h"   /* ★ #3419 §17: CPU 項の周期サンプル担当 */
#include	"pig/c++/co_ptsConfigWatch.h"   /* ★ #3419 §17.3: 設定の周期チェック担当 */
#include	"_ts2/c++/ptsLoadControl_.h"
#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>

/* 環境変数を整数で読む。未設定 / 不正なら def。 */
static int
cfg_of(sPtr<ptsApplication> app, const char *var, const char *envname, int def)
{
	/* ★ #3419 §17.3: srava 変数 → 環境変数 → 既定 (既定は設定表が持つ)。
	 * app が居ない (単体テスト) ときだけ、呼び手の def を使う。 */
	if ( app.is_notNull() ) return app->cfg_int(var);
	return osglue_env_int(envname, def);
}

/* 設定を文字列で読む。int 版と同じ解決順 (srava 変数 → 環境変数 → 既定)。 */
static int
cfg_is(sPtr<ptsApplication> app, const char *var, const char *envname, const char *want)
{
	if ( app.is_notNull() ) {
		sPtr<stdString> v = app->cfg_str(var);
		return ( v.is_notNull() && ::strcmp(v->get_str(), want) == 0 );
	}
	const char *e = ::getenv(envname);
	return ( e != 0 && ::strcmp(e, want) == 0 );
}

static int
env_int(const char *name, int def)
{
	/* ★ #3419 §17.2: 実体は osglue の共通実装。規約 (正論理・値で判定) は osglue.h を参照。 */
	return osglue_env_int(name, def);
}

CLASS_TINYSTATE(pig/c++/ptsLoadControl,pig/c++/ptsObject)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	/* parent = app。gate = 上限を書き込む対象のワーカーゲート。 */
	ptsLoadControl_(
		sPtr<tinyState> parent,
		sPtr<stdLimitSemaphore> gate);

	sRptr<tinyState,tinyState>	parent;

	/* ---- 設定 (起動時に確定) ---- */
	unsigned long long	v_mem() const   { return vMem; }
	unsigned		v_cpu() const   { return vCpu; }
	int			log_on() const  { return logOn; }

	/* ---- 状態の更新 (呼び手は TS_STATE 内から) ---- */
	void			set_agents(unsigned n);
	/* ★ §14.9: in-proc agent は planner と同一アドレス空間で**測れない**ので、数を教えてもらう。 */
	void			set_inproc(unsigned n);
	void			sample_memory(const uint32_t *pids, unsigned npids);
	void			sample_cpu(const uint32_t *pids, unsigned npids);
	double			c_cpu() const { return cCpu; }

	/* ---- 算出 ---- */
	unsigned		l_agent() const;       /* ★ 目標値 */
	unsigned		l_agent_mem() const;
	unsigned		l_agent_cpu() const;   /* ★ #3419 §17: CPU 項 */   /* メモリ項だけ (V_CPU 上限を掛ける前) */
	unsigned		effective() const { return effLimit; }   /* いまゲートに入れている値 */

	/* ★ 目標が下がったら**即座に**追従する (上げはタイマ状態がやる)。 */
	void			follow_down();
	int			needs_ramp() const;
	void			log_state(const char *what) const;
	/* ★ #3419 §17: CPU 項の推移だけを 1 行で (周期サンプルのたび)。 */
	void			log_cpu_term() const;
	/* ★ #3419 §17.3: 設定を **srava 変数から読み直す** (CACHE_DIR の流儀)。
	 * co_ptsLoadControl の周期タイマから呼ばれるので、スクリプトの代入は 250ms 以内に効く。
	 * ⚠ 起動時にしか意味が無い設定 (LOAD_RAMP_START) は、変化を検出したら**効かないと警告**する。 */
	void			refresh_config();
private:
	void			apply(unsigned n, const char *why);
	unsigned		ramp_target() const;   /* ★ 上げの目標 (下げ側と同じ形) */

	unsigned long long	vMem;
	unsigned		vCpu;
	unsigned long long	cMem;          /* C_MEM = planner + 実測できた agent の合計 */
	unsigned long long	plannerMem;    /* うち planner 自身 */
	unsigned		measuredAgents;/* ★ **実測できた** process agent 数 (C_AGENT とは違う) */
	unsigned		inprocAgents;  /* ★ in-proc agent 数 (§14.9: 測れないので数える) */
	unsigned long long	baselineMem;   /* ★ 基準線 = agent が育つ前の planner (§14.9) */
	unsigned		cAgent;        /* ゲートが数えている agent 数 */
	double			cCpu;          /* ★ #3419 §17 (2026-08-24) から **評価式で使う** (旧: 観測専用・§13.2) */
	unsigned long long	lastCpuUsec;
	INTEGER64		lastCpuUs;     /* 前回 CPU サンプルの時刻 (単調・<0 = 未サンプル) */
	INTEGER64		windowUs;
	unsigned		fixedAgent;
	int			cpuPct;
	int			memPct;
	int			logOn;
	int			cpuOff;        /* ★ #3419 §17: CPU 項を切る対照の口 (SRAVA_LOAD_CPU=0) */
	/* ---- ランプ ---- */
	/* ★ #3419 §17: CPU 項の周期サンプル担当 (別オブジェクト)。ランプのタイマとは
	 *   **別の物理量**なので分けてある (ひさ判断 2026-08-24)。INI で起こし FIN で destroy。 */
	sPtr<co_ptsLoadControl>	cocpu;
	/* ★ #3419 §17.3: 設定の周期チェック担当 (別オブジェクト)。destroy 時にも 1 回見る。 */
	sPtr<co_ptsConfigWatch>	cocfg;
	unsigned		effLimit;      /* 実効値 = gate->limit() に入れている値 */
	INTEGER64		rampUs;        /* 上げの間隔 */
	INTEGER64		t0;            /* 起動時刻 (ログを相対時刻で出すため) */
	int			rampOff;       /* 1 = ランプ無効 (対照) */
	int			rampStartUsed;    /* ★ #3419 §17.3: 実際に採用した値 (latch 時に更新) */
	int			rampStartWarned;  /* 同・「効きません」を 1 度だけ言うための印 */
	int			rampStartLatched; /* ★ #3451: 最初の pigfAgent 入場時に LOAD_RAMP_START を
	                                   * 確定させたか (0=未確定・以降は script var も拾える) */
	int			memMetricPssUsed; /* ★ #3419: 実際に採用した metric (1=pss / 0=rss) */
	int			memMetricWarned;  /* 同・「効きません」を 1 度だけ言うための印 */
	int			memMetricLatched; /* 同・確定させたか (RAMP_START と同じ latch 点) */
	TS_DEFARGS
};

TS_END_IMPLEMENT

TS_BEGIN_INTERFACE
#include	"ts2/c++/sRptr.h"
#include	"ts2/c++/sTimer.h"
class tinyState;
class stdLimitSemaphore;
class co_ptsLoadControl;
class co_ptsConfigWatch;
TS_END_INTERFACE

#endif

TS_PRIVATE(sTimer timer;)

/* ★★ V_CPU (= 静的な agent 数の天井) の決定は**ここ 1 箇所**。#3419 (ひさ設計 2026-08-30)。
 *
 *   V_CPU = ( SRAVA_LOAD_CPU == 0 ) ? SRAVA_LOAD_AGENT : ncpu * SRAVA_LOAD_CPU / 100
 *
 *   ・SRAVA_LOAD_CPU (%)  … 既定 90。コア数に対する割合で天井を決める。
 *   ・SRAVA_LOAD_AGENT    … % ではなく**個数で直接**天井を決めたいとき用。
 *                           SRAVA_LOAD_CPU=0 (= 動的 CPU 項 OFF) のときだけ読まれる。
 *   ⚠ 両方 0 なら ncpu (= 100%) に落とす。0 のままだと L_AGENT が 1 に潰れる。
 *
 * ★ コンストラクタと refresh_config() の**両方から呼ぶ**。以前は同じ式が 2 箇所に書かれ、
 *   片方 (refresh_config) だけが旧名 PIG_MAX_WORKERS を見落として 250ms 後に上限が消える、
 *   というバグになっていた (2026-08-30 に発見)。式を 2 度書かないことが再発防止そのもの。 */
static unsigned
pig_v_cpu(unsigned cpus, int cpuPct, unsigned agentN)
{
	unsigned v;
	if ( cpuPct <= 0 )
		v = ( agentN > 0 ) ? agentN : cpus;
	else
		v = (unsigned)((double)cpus * (double)cpuPct / 100.0);
	return ( v < 1 ) ? 1 : v;
}

ptsLoadControl_::ptsLoadControl_(TS_ARGS0)
	: ptsObject_(parent),
	  parent(tinyState_::parent)
{
	TS_CPARGS0
	cMem        = 0;
	plannerMem  = 0;
	measuredAgents = 0;
	inprocAgents = 0;
	baselineMem = 0;
	cAgent      = 0;
	cCpu        = -1.0;
	lastCpuUsec = 0;
	lastCpuUs   = -1;
	windowUs    = (INTEGER64)env_int("SRAVA_LOAD_WINDOW_MS", 500) * 1000;

	memPct      = env_int("SRAVA_LOAD_MEM",   50);   /* ★ 既定 50%: §13.5 */
	/* ★ #3419 §17 / §17.2 (ひさ案 2026-08-24): **SRAVA_LOAD_CPU が CPU 項の on/off も兼ねる**。
	 *     0      → CPU 項 OFF。⚠ ただし**静的上限 V_CPU は 100% (コア数) に戻す**。
	 *              §12.7.0 のとおり「否定されたのは動的な項だけで、静的上限は常に効いていた」。
	 *              ここを 0 にすると L_AGENT が 1 に潰れてしまう。
	 *     N (≥1) → CPU 項 ON・V_CPU = コア数 × N%。既定 **90**
	 *   90 なのは、飽和領域で L ≒ 0.9×C_AGENT の乗算的減少にするため (100 だと下がらない)。
	 *   メモリ側の SRAVA_LOAD_MEM=50 と同じ役割。 */
	cpuPct      = env_int("SRAVA_LOAD_CPU",   90);
	cpuOff      = ( cpuPct <= 0 );
	fixedAgent  = (unsigned)env_int("SRAVA_LOAD_AGENT", 0);
	logOn       = env_int("SRAVA_LOAD_LOG",    0);
	/* ⚠ 黙って無視しない: SRAVA_LOAD_AGENT は SRAVA_LOAD_CPU=0 のときだけ読まれる。 */
	if ( fixedAgent > 0 && ! cpuOff )
		::fprintf(stderr, "[load] WARNING: SRAVA_LOAD_AGENT is read only when SRAVA_LOAD_CPU=0; "
		                  "the value (%u) has no effect (SRAVA_LOAD_CPU=%d is used instead)\n",
		          fixedAgent, cpuPct);
	unsigned long long total = 0;
	vMem = 0;
	if ( osglue_system_memory(&total, 0) == 0 && total > 0 )
		vMem = (unsigned long long)((double)total * (double)memPct / 100.0);
	/* ★ 測定用: V_MEM を MB の絶対値で直接指定する (§13.3。整数 % では大容量機で絞りきれない)。 */
	{
		int mb = env_int("SRAVA_LOAD_MEM_MB", 0);
		if ( mb > 0 ) vMem = (unsigned long long)mb * 1024ULL * 1024ULL;
	}

	unsigned cpus = osglue_usable_cpus();
	if ( cpus == 0 ) cpus = 4;
	vCpu = pig_v_cpu(cpus, cpuPct, fixedAgent);

	/* ---- ランプ ---- */
	/* ★ 既定 250ms (ひさ判断 2026-08-22)。カーネルによって最適な周期が違うので妥協点を取る
	 *   (長い方が得なカーネルと、長いほど代償が増えるカーネルがある)。
	 *   残る超過分は **V_MEM を割り引いて設定する**ことで吸収する運用とする。 */
	rampUs   = (INTEGER64)env_int("SRAVA_LOAD_RAMP_MS", 250) * 1000;
	/* ⚠ **ガードを切る口は残すが、黙って切らせない** (ひさ判断 2026-08-22)。
	 *   「間違えてランプ無しで走らせてメモリ爆発」を防ぐため、**必ず警告を出す**。
	 *   ★ 口自体を消さないのは、**対照が取れなくなると機構を検証できなくなる**から
	 *   (今日ずっと効いている作法)。 */
	/* ★ #3419 §17.2: 正論理へ。SRAVA_LOAD_RAMP=0 でランプ無効 (既定 1 = 有効)。
	 * ★ 2026-08-30: 旧名 SRAVA_LOAD_RAMP_OFF を**撤去**した。コンストラクタでしか読まれず
	 *   refresh_config() (250ms) が新名しか見ないため、**起動 250ms 後にランプが復活していた**
	 *   (= 対照を取ったつもりで取れていない)。PIG_MAX_WORKERS と同じ穴。 */
	rampOff  = ( env_int("SRAVA_LOAD_RAMP", 1) == 0 );
	if ( rampOff )
		::fprintf(stderr, "[load] WARNING: memory guard ramp-up is DISABLED.\n"
		                  "        Agents are admitted all at once, so peak memory rises sharply.\n"
		                  "        Use this only for controlled comparison runs.\n");
	t0       = stdInterval::now();
	/* ★ #3451: ここでの値は「最初の agent が来るまで」の仮の下限に過ぎない。
	 * script 変数 (LOAD_RAMP_START) はまだ評価されていない可能性があるので、raw env のみ見る。
	 * 確定は set_agents() が最初の agent 入場を検出した時点 (rampStartLatched) で行う。 */
	effLimit = (unsigned)env_int("SRAVA_LOAD_RAMP_START", 2);
	rampStartUsed    = (int)effLimit;
	rampStartWarned  = 0;
	rampStartLatched = 0;
	/* ★ #3419: metric も同じ理由で「最初の agent 入場」まで確定させない (script var 未評価)。
	 * それまでに osglue が呼ばれた場合は osglue 側の既定 (env → rss) が効く。 */
	memMetricPssUsed = 0;
	memMetricWarned  = 0;
	memMetricLatched = 0;
	if ( effLimit < 1 ) effLimit = 1;
}

/*******************************************
	STATE MACHINE
********************************************/

TS_STATE(INI_ptsObject_START)
{
	if ( gate == thNULL )
		return rDO|FIN_START;
	/* ★ #3419 §17: 周期サンプラは **cpuOff でも必ず起こす**。
	 * ⚠ ここを `if (!cpuOff)` にしていたため、SRAVA_LOAD_CPU=0 (当時の旧名 SRAVA_LOAD_CPU_OFF=1) が
	 *   「CPU 項を外す」と「周期サンプルを止める」を**同時に**切ってしまい、
	 *   対照が 2 変数を動かしていた (2026-08-24 に自分で踏んだ)。
	 * ★ 対照として切りたいのは **評価式への寄与だけ** なので、cpuOff は l_agent_cpu() の
	 *   中でだけ見る。標本 (C_CPU の値・ログ) は両条件で同一の取り方にする。 */
	cocpu = thNEW(co_ptsLoadControl,(ifThis, ifThis));
	cocfg = thNEW(co_ptsConfigWatch,(ifThis, ifThis));   /* ★ #3419 §17.3 */
	if ( rampOff ) {
		/* 対照: ランプ無効 = 目標値をそのまま入れる (2026-08-21 以前の振る舞い)。
		 * ⚠⚠ 旧: ここで ACT_START へ行っていた。ACT_START は is_destroyed() を見ないので
		 *   **FIN_START に到達せず**、下で destroy すべき相棒 (cocpu) が生き残って
		 *   **app が終われなくなる** (srava_workergate_eagain が Timeout・2026-08-24 に踏んだ)。
		 *   ランプが無い時代は自分のタイマも張っていなかったので無害だった。
		 * ★ IDLE は is_destroyed() を見て FIN_START へ抜けるので、そちらへ行く。 */
		apply(l_agent(), "ランプ無効");
		return rDO|ACT_ptsLoadControl_IDLE;
	}
	if ( effLimit > l_agent() ) effLimit = l_agent();
	apply(effLimit, "初期値");
	return rDO|ACT_ptsLoadControl_TICK;
}

/* ★ 上げの契機。**時間駆動でなければならない** — 待っているのは「イベント」ではなく
 * 「agent が育つこと」= 時間だから (§13.7)。入退場イベント駆動では育つ前に次が入る。
 *
 * ⚠ **用事があるときだけタイマを張る**。常時張ると tinyState のフレームワークに登録が残り、
 *   **app が終われない** (実装初回にこれで teardown ハングを踏んだ・2026-08-21)。
 *   上げ切ったら ACT_ptsLoadControl_IDLE で眠り、目標が上がったら app が TSE_ASSERT で起こす。 */
TS_STATE(ACT_ptsLoadControl_TICK)
{
	if ( effLimit >= ramp_target() )
		return rDO|ACT_ptsLoadControl_IDLE;   /* 用事なし → タイマを張らない */
	timer.start(ifThis, rampUs);
	return ACT_ptsLoadControl_RAMP;
}

TS_STATE(ACT_ptsLoadControl_RAMP)
{
	/* ⚠ **待っている間に destroy されうる**。ここを見ないとタイマ待ちから抜けられず、
	 *   app が終われない (実装初回に踏んだ・ひさ指摘 2026-08-21)。
	 * ★ 理由: 状態遷移機械は**定義された遷移以外の遷移をしない**ので、destroy() は外から
	 *   遷移を差し込めない = 印を置くだけ。**待つ側がポーリングする**のが唯一の形。
	 *   (弱点ではなく、機械が解析可能であるための条件。) */
	if ( is_destroyed() ) { timer.stop(ifThis); return rDO|FIN_START; }
	if ( ! timer.is_expire(ifThis) ) return 0;
	unsigned target = ramp_target();
	if ( effLimit < target )
		apply(effLimit + 1, "ランプ +1");   /* ★ 上げは 1 ずつ = 各段が実測に裏打ちされる */
	return rDO|ACT_ptsLoadControl_TICK;     /* 用事が残っていれば TICK が再武装する */
}

/* 眠り。app が kick (TSE_ASSERT) してきたら、用事があるか見に行く。 */
TS_STATE(ACT_ptsLoadControl_IDLE)
{
	if ( is_destroyed() ) return rDO|FIN_START;
	if ( rampOff ) return 0;   /* ★ 対照時は上げの再武装をしない (寝て destroy を待つだけ) */
	if ( ev != thNULL && ev->type == TSE_ASSERT && effLimit < ramp_target() )
		return rDO|ACT_ptsLoadControl_TICK;
	return 0;
}

TS_STATE(FIN_START)
{
	/* ⚠ タイマを止めないとフレームワークに登録が残り、app が終われない
	 * (このプロジェクトは teardown ハングを過去に踏んでいる)。 */
	timer.stop(ifThis);
	if ( cocpu.is_notNull() ) { cocpu->destroy(); cocpu = thNULL; }   /* ★ #3419 §17 */
	if ( cocfg.is_notNull() ) { cocfg->destroy(); cocfg = thNULL; }   /* ★ #3419 §17.3 */
	return rDO|FIN_ptsObject_START;
}

/*******************************************
	MEASUREMENT / EVALUATION  (plain・TS_STATE から呼ぶ)
********************************************/

void
ptsLoadControl_::apply(unsigned n, const char *why)
{
	if ( n < 1 ) n = 1;
	effLimit = n;
	if ( gate != thNULL && (int)n != gate->limit() ) {
		int before = gate->limit();
		gate->limit((int)n);
		/* ★ bench 依頼 2026-08-21: **実効値の遷移を時刻つきで**出す。これが無いと
		 *   「上限が制約になっていた時間帯があるか」を外から判定できない。 */
		if ( logOn )
			::fprintf(stderr, "[load] t=%.3fs 実効 %d → %u (%s) C_AGENT=%u 実測agent=%u\n",
			          (double)(stdInterval::now()-t0)/1e6, before, n, ( why != 0 ) ? why : "",
			          cAgent, measuredAgents);
		log_state(why);
	}
}

/* ★★ 上げの目標 = min(目標値, C_AGENT + 2)  (ひさ要望 2026-08-22)
 *
 * ⚠ **これが無いと「実効 ≤ C_AGENT + 2」が下げ側にしか効かない。**
 *   cap は follow_down() (= agent の入退場からしか呼ばれない) にしか入っておらず、
 *   ランプはタイマで**独立に** +1 していた。⇒ 「追従で下げ → ランプで上げ」で
 *   cap を超えてしまう。振動に見えていたのは、上げ側に cap が入っていない**片肺の帰結**だった。
 *
 * ★ 揃えた結果、ランプが進むのは `effLimit < cAgent + 2` すなわち
 *   **ゲートが満杯の 1 歩手前まで実際に使われているとき**だけになる。
 *   使われていない上限を先に上げておく (= 次の爆発が無制御で入る) ことが無くなる。 */
unsigned
ptsLoadControl_::ramp_target() const
{
	unsigned t   = l_agent();
	unsigned cap = cAgent + 2;   /* +2 = 「次の 1 個を入れる余地」+ 余裕 (§14.8) */
	return ( cap < t ) ? cap : t;
}

/* ★ 上げる用事があるか。app はこれを見て、眠っているランプを起こす。 */
int
ptsLoadControl_::needs_ramp() const
{
	return ( ! rampOff && effLimit < ramp_target() ) ? 1 : 0;
}

/* ★ 目標が下がったら即追従 (下げは速く)。上げはここではやらない。
 *
 * ★★ あわせて **実効 ≤ C_AGENT + 2 を常に保つ** (ひさ判断 2026-08-22)。
 *   ⚠ 理由: これが無いと **一度上がった実効値を下げる機会が実質無い**。目標 (l_agent) が
 *   実効を下回るのはメモリが逼迫したときだけなので、**スクリプトの並列度が一時的に細くなっても
 *   実効値は上がったまま**残る。その後で並列度が再び爆発すると、**ランプの制御を受けずに
 *   一斉入場してしまう** = start の効果が初回にしか効かないことになる。
 *   → **実効値を実際の並列度に張り付かせておけば、爆発のたびに start と同じ制御がかかる。**
 *   +2 は「次の 1 個を入れる余地」+ 1 の余裕 (0 だと前進できない・1 だと常に上限に張り付く)。 */
void
ptsLoadControl_::follow_down()
{
	if ( gate == thNULL ) return;
	unsigned target = l_agent();
	if ( rampOff ) { apply(target, "追従(ランプ無効)"); return; }
	if ( target < effLimit ) { apply(target, "下げ追従"); return; }
	unsigned cap = cAgent + 2;                  /* ★ 並列度に張り付かせる */
	if ( effLimit > cap ) apply(cap, "並列度に追従");
}

/* ★ #3451: LOAD_RAMP_START の確定を「起動時」から「最初の pigfAgent がゲートに入場した瞬間」へ
 * 遅らせる。ここまで来ればスクリプトの var 代入 (module() 相当) は既に評価済みなので、
 * cfg_of() が srava 変数を正しく拾える (ctor 時点は rootEnv 未確立で raw env しか読めなかった)。
 * ⚠ 一度だけ (rampStartLatched)。以降の代入は refresh_config() の「効きません」警告の対象。 */
void
ptsLoadControl_::set_agents(unsigned n)
{
	/* ★ #3419: metric も同じ入場点で確定させる。ここより後の代入は効かない (ini_only=1)。
	 * 実行中に metric が変わると C_MEM の同じ 1 本の時系列に Pss 由来と VmRSS 由来が混ざるため。 */
	if ( ! memMetricLatched && n > 0 ) {
		memMetricLatched = 1;
		memMetricPssUsed = cfg_is(ptsApp, "LOAD_MEM_METRIC", "SRAVA_LOAD_MEM_METRIC", "pss");
		osglue_set_mem_metric(memMetricPssUsed ? "pss" : "rss");
		if ( logOn )
			::fprintf(stderr, "[load] LOAD_MEM_METRIC 確定 (最初の agent 入場): %s\n",
			          memMetricPssUsed ? "pss" : "rss");
	}
	if ( ! rampStartLatched && n > 0 ) {
		rampStartLatched = 1;
		int rs = cfg_of(ptsApp, "LOAD_RAMP_START", "SRAVA_LOAD_RAMP_START", 2);
		rampStartUsed = rs;
		/* ★ #3451: apply() は無変化だと無言なので、確定した事実自体は常に 1 行残す
		 * (env 既定と一致しただけの no-op か、script var で上書きされたのかを区別できるように)。 */
		if ( logOn )
			::fprintf(stderr, "[load] LOAD_RAMP_START 確定 (最初の agent 入場): %d (rootEnv=%s)\n",
			          rs, ( ptsApp.is_notNull() && ptsApp->root_env().is_notNull() ) ? "yes" : "no");
		if ( ! rampOff ) {
			unsigned start = ( rs < 1 ) ? 1 : (unsigned)rs;
			if ( start > l_agent() ) start = l_agent();
			apply(start, "初期値(起動確定)");
		}
	}
	cAgent = n;
}

/* ★ §14.9: in-proc agent (EXEC_THREAD・manifold 等) は planner と同一アドレス空間なので
 * **RSS では区別できない**。ptsApplication が登録数を数えて教えてくる (ptsMediatorInternal の
 * enable/teardown が契機)。分母に入れないと「planner が育っただけ」に見え、
 * ★ **全部 in-proc の構成でメモリ項が一度も働かなかった**。 */
void
ptsLoadControl_::set_inproc(unsigned n)
{
	inprocAgents = n;
}

void
ptsLoadControl_::sample_memory(const uint32_t *pids, unsigned npids)
{
	unsigned long long sum = 0, m = 0;
	plannerMem = 0;
	if ( osglue_proc_memory(osglue_getpid(), &m) == 0 ) { plannerMem = m; sum += m; }
	unsigned got = 0;
	for ( unsigned i = 0 ; i < npids ; i++ )
		if ( pids != 0 && osglue_proc_memory(pids[i], &m) == 0 ) { sum += m; got++; }
	cMem = sum;
	measuredAgents = got;
	/* ★★ §14.9: **基準線** = 「評価式が駆動され始める直前」の planner (ひさ判断 2026-08-22)。
	 *
	 * ここが最初のサンプル = **最初の agent がゲートに入った時点**である。スクリプトの解析も
	 * モジュールのロードも済んでいて、agent はまだ何も確保していない。
	 *
	 * ★ 凍結してよい理由 (ひさ): **planner が抱えるメッシュは必ずどこかの agent に帰属する**
	 *   (誰にも参照されなくなれば消える)。だから planner の成長を agent に計上するのは
	 *   誤差ではなく**正しい帰属**である。
	 * ⚠ 「agent 0 個のたびに測り直す」案は採らない。**agent が 0 になるのは planner の終了時**で、
	 *   走行中に測り直す機会が実質無い (ひさ指摘)。加えて allocator が解放分を OS に返さないと
	 *   基準線がラチェット上昇し、**緩む方向** = OOM 側に外れる。 */
	if ( baselineMem == 0 && plannerMem > 0 )
		baselineMem = plannerMem;
	if ( logOn ) {
		unsigned na = measuredAgents + inprocAgents;
		::fprintf(stderr, "[load] C_MEM=%.0fMB (基準線 %.0fMB + planner 増分 %.0fMB + "
		                  "**実測できた process agent %u 個**／in-proc %u 個"
		                  "／ゲートは C_AGENT=%u) 1 agent≒%.0fMB 実効=%u 目標=%u\n",
		          sum/1e6, baselineMem/1e6,
		          ( plannerMem > baselineMem ) ? (plannerMem - baselineMem)/1e6 : 0.0,
		          got, inprocAgents, cAgent,
		          ( na > 0 && sum > baselineMem ) ? (double)(sum - baselineMem)/na/1e6 : 0.0,
		          effLimit, l_agent());
	}
}

/* ★ 観測専用 (§13.2)。時刻は stdInterval::now() = 単調増加マイクロ秒。 */
void
ptsLoadControl_::sample_cpu(const uint32_t *pids, unsigned npids)
{
	unsigned long long sum = 0, t = 0;
	if ( osglue_proc_cputime(osglue_getpid(), &t) == 0 ) sum += t;
	for ( unsigned i = 0 ; i < npids ; i++ )
		if ( pids != 0 && osglue_proc_cputime(pids[i], &t) == 0 ) sum += t;

	INTEGER64 now = stdInterval::now();
	if ( lastCpuUs < 0 ) { lastCpuUsec = sum; lastCpuUs = now; return; }
	INTEGER64 dus = now - lastCpuUs;
	if ( dus < windowUs ) return;
	/* ⚠ agent が終了すると合計が減る。負の CPU 消費は無いので基準点を置き直す。 */
	if ( sum < lastCpuUsec ) { lastCpuUsec = sum; lastCpuUs = now; return; }
	cCpu = (double)(sum - lastCpuUsec) / (double)dus;
	lastCpuUsec = sum; lastCpuUs = now;
	if ( logOn )
		::fprintf(stderr, "[load] C_CPU=%.2f コアぶん (窓 %lldms・agent %u 個)\n",
		          cCpu, (long long)(dus/1000), npids);
}

/* ★ メモリ項だけ。V_CPU 上限は掛けない。0 = 算出不能。
 *
 * ⚠⚠ **2026-08-21 修正 (bench 報告)**: 旧式は
 *       L_AGENT_MEM = V_MEM / C_MEM × C_AGENT
 *     だったが、これは **C_MEM が C_AGENT 個ぶんを含んでいる**ことを前提にしていた。
 *     実際には **agent の pid が分かるのは fork/exec/pipe 確立の後** (ptsMediatorExternal の
 *     enable_body) なので、**ゲートが数えた直後は測定対象に入っていない**。
 *     **C_AGENT は数えているのに測定対象が 0 個**という状態が多数を占め、
 *     C_MEM が planner のぶんのままなので「まだいくらでも入る」と答え続けていた。
 *
 * ★ 直し方: **実測できた分だけで 1 agent あたりを出し、そこから「何個入るか」を返す**。
 *     1 agent ≒ (C_MEM − planner) / 実測できた数
 *     入る数   = (V_MEM − planner) / 1 agent
 *   実測数が C_AGENT と食い違っても壊れず、**実測が 0 個なら「分からない」= 制限しない**
 *   と正直に縮退する (旧式は 0 個のとき巨大な値を返していた = 静かに壊れていた)。
 * ⚠ 「入場したがまだ育っていない agent」が数字に出ないことは**この修正では直らない** (§13.6)。 */
unsigned
ptsLoadControl_::l_agent_mem() const
{
	/* ★★ §14.9 (2026-08-22): **基準線を引き、in-proc も分母に入れる**。
	 *   旧: per = (C_MEM − planner) / 実測 process agent 数
	 *       → in-proc のメモリは planner の RSS に入るので**分子から引かれて消え**、
	 *         in-proc は分母にも入らない。★ 全部 in-proc だと実測 0 個 =
	 *         **メモリ項が一度も働かない**。
	 *   新: per = (C_MEM − 基準線) / (実測 process agent + in-proc agent)
	 *       基準線 = agent が育つ前の planner (sample_memory 参照)。
	 *       planner 自身の成長も agent に帰属する (ひさ判断) ので、分子に残すのが正しい。 */
	unsigned nAgent = measuredAgents + inprocAgents;
	if ( nAgent == 0 || vMem == 0 ) return 0;           /* 数えられない = 分からない */
	if ( baselineMem == 0 ) return 0;                   /* 基準線未取得 = 分からない */
	if ( cMem <= baselineMem ) return 0;
	double per = (double)(cMem - baselineMem) / (double)nAgent;
	if ( per <= 0.0 ) return 0;
	double head = (double)vMem - (double)baselineMem;   /* agent に使える残り */
	if ( head <= 0.0 ) return 1;                        /* 基準線だけで超過 → 最小 */
	double n = head / per;
	if ( n < 1.0 ) n = 1.0;
	return (unsigned)(n + 0.999999);                    /* ceil */
}

/* ★ #3419 §17 (2026-08-24・ひさ案): **CPU 項**。メモリ項とまったく同じ形で、
 * 「1 agent あたりの実測 CPU」から「V_CPU に何個入るか」を出す。
 *
 *     L_AGENT_cpu = V_CPU / (C_CPU / C_AGENT) = V_CPU × C_AGENT / C_CPU
 *
 * ★ **srava は op のスレッド数を知らなくてよい** — 実測から per-agent を割り出すので、
 *   TBB / OpenMP / raw が混在していても結果的に正しい数に収束する (§12.1③ を迂回する)。
 * ★ 飽和領域 (C_CPU = V_CPU) では L = C_AGENT × (V_CPU/C_CPU) ≒ C_AGENT となり、
 *   V_CPU を割り引いてある (SRAVA_LOAD_CPU=90) ぶんだけ **1 サンプルごとに 10% 下がる**
 *   = 乗算的減少。⚠ だから「張り付いたら L=1」という特別扱いは**入れない**。
 *   入れると **単スレッド op を 24 個** (= 健全な飽和) まで 1 に潰してしまう。
 *   式なら 40→36→32→28→25→22→21 と下りて **21 で正しく止まる** (シミュレーション確認済み)。
 * ★ 位置づけ (ひさ 2026-08-24): メモリと違いスレッドは**暴発の危険が小さい**。
 *   「防御」ではなく **「スレッド量を整える」**程度でよいので、式はこれ以上複雑にしない。
 * ⚠ 収束先は V_CPU / (threads per agent):
 *   22 スレッド/agent → 1 個 / 10 → 2 個 / 4 → 5 個 / 1 → 21 個。 */
unsigned
ptsLoadControl_::l_agent_cpu() const
{
	if ( cpuOff ) return 0;                            /* ★ 対照の口: SRAVA_LOAD_CPU=0 */
	/* ★ 分母は **cAgent** (ゲートが数えている入場中の数)。メモリ項の
	 * measuredAgents+inprocAgents とは**別物**: あちらは「C_MEM に寄与した者」で割る必要が
	 * あるための数 (§14.9) で、in-proc モジュールによっては 0 のままになる (pipe_proximity で実測)。
	 * CPU を使っているのは「いま入場している者」なので、ゲートの計数が意味論的に正しい。 */
	unsigned nAgent = cAgent;
	if ( nAgent == 0 ) return 0;                       /* 数えられない = 分からない */
	if ( cCpu <= 0.0 ) return 0;                       /* 未測定 (-1) / 実質ゼロ = 分からない */
	double per = cCpu / (double)nAgent;                /* 1 agent あたりの実測コア数 */
	if ( per <= 0.0 ) return 0;
	double n = (double)vCpu / per;
	if ( n < 1.0 ) n = 1.0;
	return (unsigned)n;                                /* ★ floor (V_CPU を超えない側へ) */
}

/* ★ 目標値。実際にゲートへ入る値は effective() (ランプ後)。 */
unsigned
ptsLoadControl_::l_agent() const
{
	/* ★ #3419 (ひさ設計 2026-08-30): fixedAgent による**短絡を撤去**した。
	 *   SRAVA_LOAD_AGENT は V_CPU を決める口 (pig_v_cpu) になったので、ここでは他の項と
	 *   同じ土俵で min を取る。⇒ 上限の決定はこの 1 箇所に集まる。
	 *   ⚠ 挙動変更: 以前は SRAVA_LOAD_AGENT=N が「N で固定」だったが、いまは「天井 N」で
	 *   メモリ項 / CPU 項がそれより小さければそちらが勝つ。 */
	unsigned n = vCpu;
	unsigned m = l_agent_mem();
	if ( m != 0 && m < n ) n = m;
	unsigned c = l_agent_cpu();          /* ★ #3419 §17 */
	if ( c != 0 && c < n ) n = c;
	return ( n < 1 ) ? 1 : n;
}

/* ★ #3419 §17.3: 設定を srava 変数から読み直す。周期タイマ (250ms) から呼ばれる。
 * ⚠ **起動時にしか読まれない設定は、変化を見つけたら「効かない」と言う** (黙って無視しない)。 */
void
ptsLoadControl_::refresh_config()
{
	int oldLog = logOn;
	windowUs   = (INTEGER64)cfg_of(ptsApp, "LOAD_WINDOW_MS", "SRAVA_LOAD_WINDOW_MS", 500) * 1000;
	memPct     = cfg_of(ptsApp, "LOAD_MEM", "SRAVA_LOAD_MEM", 50);
	int pct    = cfg_of(ptsApp, "LOAD_CPU", "SRAVA_LOAD_CPU", 90);
	cpuOff     = ( pct <= 0 );
	cpuPct     = pct;
	fixedAgent = (unsigned)cfg_of(ptsApp, "LOAD_AGENT", "SRAVA_LOAD_AGENT", 0);
	logOn      = cfg_of(ptsApp, "LOAD_LOG", "SRAVA_LOAD_LOG", 0);
	rampUs     = (INTEGER64)cfg_of(ptsApp, "LOAD_RAMP_MS", "SRAVA_LOAD_RAMP_MS", 250) * 1000;
	rampOff    = ( cfg_of(ptsApp, "LOAD_RAMP", "SRAVA_LOAD_RAMP", 1) == 0 );

	/* V_MEM / V_CPU は派生値なので毎回引き直す。 */
	unsigned long long total = 0;
	vMem = 0;
	if ( osglue_system_memory(&total, 0) == 0 && total > 0 )
		vMem = (unsigned long long)((double)total * (double)memPct / 100.0);
	int mb = cfg_of(ptsApp, "LOAD_MEM_MB", "SRAVA_LOAD_MEM_MB", 0);
	if ( mb > 0 ) vMem = (unsigned long long)mb * 1024ULL * 1024ULL;
	unsigned cpus = osglue_usable_cpus();
	if ( cpus == 0 ) cpus = 4;
	vCpu = pig_v_cpu(cpus, cpuPct, fixedAgent);

	/* ⚠ **最初の agent 入場までにしか読まれない設定** (#3451): 確定 (rampStartLatched) 後に
	 * 変わっていたら 1 度だけ「効かない」と言う。確定前の代入は set_agents() が拾うので対象外
	 * (script の var 代入がタイマ周期よりたまたま先に見えても、まだ「手遅れ」ではない)。 */
	if ( rampStartLatched && ! rampStartWarned ) {
		int rs = cfg_of(ptsApp, "LOAD_RAMP_START", "SRAVA_LOAD_RAMP_START", 2);
		if ( rs != rampStartUsed ) {
			::fprintf(stderr, "[load] WARNING: LOAD_RAMP_START is read only until the first agent is admitted; "
			                  "the assignment (%d) has no effect (value actually used = %d)\n", rs, rampStartUsed);
			rampStartWarned = 1;
		}
	}
	if ( memMetricLatched && ! memMetricWarned ) {
		int now = cfg_is(ptsApp, "LOAD_MEM_METRIC", "SRAVA_LOAD_MEM_METRIC", "pss");
		if ( now != memMetricPssUsed ) {
			::fprintf(stderr, "[load] WARNING: LOAD_MEM_METRIC is read only until the first agent is admitted; "
			                  "the assignment (%s) has no effect (value actually used = %s)\n",
			          now ? "pss" : "rss", memMetricPssUsed ? "pss" : "rss");
			memMetricWarned = 1;
		}
	}
	if ( ptsApp.is_notNull() ) ptsApp->refresh_gate_config();   /* ★ #3419 §17.3 */
	if ( logOn && ! oldLog )
		::fprintf(stderr, "[load] LOAD_LOG enabled (set from a srava variable)\n");
}

/* ★ #3419 §17: CPU 項が「何を見て何を答えたか」を毎標本 1 行で出す。
 * C_CPU が飽和しているか / 1 agent あたり何コアか / 何個入ると答えたか、を並べる。 */
void
ptsLoadControl_::log_cpu_term() const
{
	if ( ! logOn ) return;
	unsigned c = l_agent_cpu();
	double per = ( cAgent > 0 && cCpu > 0.0 ) ? cCpu / (double)cAgent : 0.0;
	::fprintf(stderr, "[load] CPU項: C_CPU=%.2f/%u=%.2fコア/agent → %s (V_CPU=%u) 実効=%u 目標=%u\n",
	          cCpu, cAgent, per,
	          ( c == 0 ) ? "-" : "", vCpu, effLimit, l_agent());
	if ( c != 0 )
		::fprintf(stderr, "[load] CPU項 = %u 個\n", c);
}

void
ptsLoadControl_::log_state(const char *what) const
{
	if ( ! logOn ) return;
	unsigned m = l_agent_mem();
	char membuf[24];
	if ( m == 0 ) ::snprintf(membuf, sizeof membuf, "-");
	else          ::snprintf(membuf, sizeof membuf, "%u", m);
	unsigned cc = l_agent_cpu();
	char cpubuf[24];
	if ( cc == 0 ) ::snprintf(cpubuf, sizeof cpubuf, "-");
	else           ::snprintf(cpubuf, sizeof cpubuf, "%u", cc);
	unsigned lo = vCpu;
	const char *decid = "V_CPU";
	if ( m  != 0 && m  < lo ) { lo = m;  decid = "メモリ"; }
	if ( cc != 0 && cc < lo ) { lo = cc; decid = "CPU"; }
	::fprintf(stderr,
	          "[load] %-14s V_MEM=%.1fGB V_CPU=%u | C_MEM=%.0fMB C_AGENT=%u | C_CPU=%.2f "
	          "| 目標=%u (メモリ項=%s CPU項=%s 上限=%u・決め手=%s) ランプ目標=%u **実効=%u** 入場停止=%s\n",
	          ( what != 0 ) ? what : "",
	          vMem / 1e9, vCpu, cMem / 1e6, cAgent, cCpu, l_agent(),
	          membuf, cpubuf, vCpu, decid, ramp_target(), effLimit,
	          ( effLimit <= cAgent && cAgent > 0 ) ? "yes" : "no");
}
