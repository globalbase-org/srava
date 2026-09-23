/*
 * cgptsPlanner — srava(プランナープロセス)本体(ptsApplication 派生 = プランナーの実態元祖)。
 *   overview.txt「## srava の動き」/ step_6and7.txt 1.2 の薄い骨格。CGAL 非依存。
 *
 * 流れ(INI_ptsApplication_START 以降):
 *   1.2.1 起動時スイープ : CACHE_DIR を舐め「W_END 番兵なし かつ writer_pid not live」の
 *                          死体キャッシュを削除(前回のハードクラッシュ残骸の保険掃除)。
 *   1.2.2 パージング     : 【スタブ】手組みツリー union(box(2,2,2), box(1,1,3)) を
 *                          pigDataFunction<pigfModuleAgent> で構築(実 lemonc++ は 2.3)。
 *   1.2.3 最適化(可変ソート): 後回し(正しさには不要)。
 *   1.2.4 評価           : tree->compact()。継続 ("delayed" . promise) を解決し最終値を得る。
 *                          is_error → stderr + exit 1 / それ以外(mesh は cache ハンドル)→ exit 0。
 *   1.2.5 終了時クリーンアップ: 使われなかった/番兵なしキャッシュ削除(最小=起動時と同じスイープ。
 *                          used 追跡は ptsApplication の dedup list 実装後 = 将来)。
 *   1.2.6 exit_code      : ctor で渡された int* へ書込み(main のローカル)。
 *
 * env: get_env() で CACHE_DIR 入りの pigEnvironment を返す。compact の helper(pigfModuleAgent)は
 *   caller=本プランナーを実態親に取り、この env から CACHE_DIR を引く。
 * agent パス: pigfModuleAgent が getenv("SRAVA_AGENT") で実エージェント srava_agent を起動。
 */
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/ptsApplication.h"   /* 基底(プランナープロセスの実態元祖) */
#include	"ts2/c++/tsApplication.h"    /* ctor の parent 型 sPtr<tsApplication> */
#include	"pig/c++/pigData.h"
#include	"pig/c++/pigModuleRegistry.h"   /* 既定カーネル = priority 最大 (K6・Phase2-5) */
#include	"pig/c++/ptsFireAndForget.h"   /* 「起動して待たない」(#3419) */
#include	"cg/c++/pigcgOperators.h"   /* export/export_async/flush 演算子(srava I/O シンク・pigcg 命名) */
#include	"pig/c++/pigfFunction.h"    /* pigDataFunction<pigfPrintAsync>(print_async チェーン) */
#include	"pig/c++/pigfAsync.h"       /* async 文の統一 helper(body 直列 + sync 発行順チェーン) */
#include	"pig/c++/pigwire.h"          /* キャッシュの W_END 番兵/streamhdr 解析(mesh 検証) */
#include	"pig/c++/pigCacheManager.h"  /* 終了時 sweep(機構は pig 層へ移設) */
#include	"pig/c++/osglue.h"           /* writer_pid の存在確認 */
#include	"cg/c++/cgptsLemonParser.h" /* ソース文字列 → pigData ツリー(lemonc++) */
#include	"ts2/c++/tsSignal.h"         /* SIGINT を TSE_SIGNAL イベント化 */
#include	"ts2/c++/stdEvent.h"         /* filter() の stdEvent / TSE_SIGNAL、parser の TSE_RETURN */
#include	"ts2/c++/stdFrameWork.h"    /* #3556: 撤収の居残りを吐かせる trace_all / trace_bit */
#include	"_ts2/c++/cgptsPlanner_.h"

#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<stdint.h>
#include	<unistd.h>
#include	<dirent.h>
#include	<strings.h>                /* strcasecmp(SRAVA_CACHE_RETAIN 解釈) */
#include	<string>                   /* #3452: SRAVA_MODULE_ALL のプレリュード合成 */

#include	"pig/c++/pigInstallPaths.h"   /* srava_agent のパス解決 (env → exe 相対 → install 既定) */
#include	<time.h>                  /* time/mktime(キャッシュ保持期日の算出) */
#include	<sys/stat.h>
#ifndef _WIN32
#include	<sys/utsname.h>            /* uname: OS/アーキ指紋(キャッシュ版数ゲート) */
#endif
#include	<signal.h>

CLASS_TINYSTATE(cg/c++/cgptsPlanner,pig/c++/ptsApplication)



/* ---- キャッシュ「ディレクトリ」掃除機構は pig 層 pigCacheManager へ移設(pigwire 形式だけに依存し
 *      CGAL/srava 非依存だったため)。ここに残るのは srava 固有の「版数指紋(何が変わったら無効か)」を
 *      作る cache_fingerprint と、表示/継続の小物 helper のみ。 ---- */

/* エラー表示を目立たせる: 前後に空行 + *** で囲う(他の [srava]… 行や print 出力に埋もれないように)。
 * m は "ERROR[file,line] …" 形式(get_str)。"ERROR[" プレフィックスは保持(検出やログ grep のため)。 */
static void show_error(const char *m) { ::fprintf(stderr, "\n*** %s ***\n\n", m); }

/* ---- キャッシュ版数指紋(srava 固有・「何が変わったらキャッシュ無効か」を決める)----
 * キャッシュは「式(ソース/op)」でアドレスするが、agent の計算結果は agent バイナリのバージョンや
 * OS/アーキ(浮動小数点の違い)で変わりうる。式ハッシュが同じでも中身が古い → 古い結果を返してしまう
 * (実例: SDF アルゴリズムを変えた後、Mac で旧結果が残り thin_spots が古い判定を返した)。
 * → この指紋(キャッシュ形式版 + OS/アーキ + agent バイナリの size/mtime)を INI で ptsApp に設定し、
 *   pigCacheManager の版ゲート機構が info.txt と比較・不一致なら全クリアする(機構は pig 層)。
 * agent の size/mtime は再ビルド/再インストール(cmake --install)で必ず変わるので、版ずれを確実に捕える。 */
#define SRAVA_CACHE_FORMAT "v2"   /* キャッシュ「ファイル形式」を変えたら手で上げる(計算変更は size/mtime が捕える) */

static void compute_cache_fingerprint(char *out, size_t outsz)
{
	char os[256] = "os=?";
#ifdef _WIN32
	::snprintf(os, sizeof os, "os=Windows/%s", (sizeof(void*) == 8) ? "x86_64" : "x86");
#else
	struct utsname u;
	if ( ::uname(&u) == 0 )
		::snprintf(os, sizeof os, "os=%s/%s", u.sysname, u.machine);   /* 例 os=Linux/x86_64 / os=Darwin/arm64 */
#endif
	const char *agent = srava_agent_path();   /* 起動側 (pigfModuleAgent) と同じ解決を使う (#3431) */
	long asz = -1, amt = -1;
	struct stat st;
	if ( ::stat(agent, &st) == 0 ) { asz = (long)st.st_size; amt = (long)st.st_mtime; }
	::snprintf(out, outsz, "srava-cache %s\n%s\nagent=%s sz=%ld mt=%ld\n",
	           SRAVA_CACHE_FORMAT, os, agent, asz, amt);
}


/* v1 のデフォルトソース(env SRAVA_SOURCE で上書き可)。1.2.2 パーズで pigData ツリーへ。
 * ★ #3452: 起動時 eager-load 撤去に伴い、引数無し起動(このソース)でも実カーネルの明示ロードが
 * 要る。「何も指定せず srava を叩く」という最も基本的な経路なので、ここは include で
 * 自己完結させる (呼び出し側に SRAVA_MODULE_ALL 等を要求しない)。 */
static const char *DEFAULT_SOURCE =
	"include \"module/all.sra\";\n"
	"var a = box(2,2,2);\n"
	"var b = box(1,1,3);\n"
	"var m = export(a ||| b);\n"
	/* ★ #3443: 頂点数 / 面数は **op が答える** (planner は幾何の語彙を持たない)。 */
	"print(\"NVF\", nverts(m), nfaces(m));\n";

#if 0

TS_BEGIN_IMPLEMENT


class TS_THISCLASS : public TS_BASECLASS {
public:
	/* _src: 実行するソース文字列(NULL なら env SRAVA_SOURCE→既定)。_fname: エラー表示用ファイル名
	 * (NULL なら "<source>")。_exitCode: 終了コードの書き込み先(main のローカル変数のアドレス。NULL 可)。
	 * いずれも以前は file-scope グローバルだったが、planner ごとに渡せるよう ctor 引数化(マルチプランナ)。 */
	cgptsPlanner_(
		sPtr<tsApplication> parent,
		const char *_src,
		const char *_fname,
		int *_exitCode,
		int _argvN = 0,           /* スクリプト後のコマンドライン引数(ARGV)の個数 */
		char **_argvV = 0);       /* 同・文字列配列(プロセス寿命なので保持のみ・解放不要) */

	sRptr<tsApplication,tinyState>		parent;

	virtual sPtr<pigEnvironment>	get_env();
	virtual sPtr<stdEvent>		filter(sPtr<stdEvent> ev);   /* TSE_SIGNAL を捕まえる */

	/* ★ #3366 由来: 非同期処理のレジストリは **srava 言語固有の機能**なので、汎用フレームワーク基底
	 * ptsApplication ではなく srava アプリ層のこの planner が所有する。app 所有 = gc_thread 管理下に置く
	 * ことで、かつて file-static グローバルだった頃の終了時 use-after-free SEGV を構造的に根絶する。
	 * async 文(統一プリミティブ)。syncTail=直前 async の done 信号(sync 発行順チェーン)。
	 * ★ #3482: 待ちとエラーの台帳は **根の見えない try** へ移した。ここは発行順チェーンだけ。
	 * print_async / export_async はここへ desugar される(専用レジストリは撤去)。 */
	sPtr<pigData>		sync_tail();                        /* 現在のチェーン末尾(初回は解決済み null) */
	void			set_sync_tail(sPtr<pigData> t);     /* 末尾を更新 */
	void			reset_sync_chain();                 /* flush(): 発行順チェーンを張り直す */
	int			drain_async();                      /* 末尾: 根の try に溜まったエラーを報告 */
	int			async_error_total();                /* async の累積エラー数(終了コード判定用) */
	/* ★ agent が出した理由を **末尾でまとめて列挙**する (2026-08-26・ひさ提案。async の
	 * continue-and-collect と同じ考え方)。
	 * ★なぜ要るか: エラーの帰属は「最初に promise 連鎖を取った agent」で決まるので、
	 *   **落ちた本人とは限らない**。agent がシグナルで死ぬ形では、正常終了した傍観者が
	 *   "agent closed unexpectedly" を先に返し、本人の具体的な理由が捨てられることがある。
	 *   誰が勝つかを決めにいく代わりに **全部出す**。
	 * ⚠ 既に表示した文言 (shownError) は飛ばす = 主エラーとの二重表示を避ける。 */
	void			show_other_agent_errors();
protected:
	sPtr<pigEnvironment>	env;
	/* ★★ #3482 段 3: **根の見えない try**。プログラム全体を囲む try で、利用者には見えない
	 * (構文に現れない)。これで **すべての agent がどれかの try に属する**という不変条件が立ち、
	 * 「try の中か外か」で振る舞いが変わる場所が無くなる。
	 *   ・トップレベルで起動した agent の待ちリストはここ
	 *   ・トップレベルの destroy() の送り先もここ
	 * ⚠ 文 (args) を持たず compact もされないので helper は付かない。待ちリストとエラー列
	 *   だけを持つ入れ物として使う (error() は catch 専用なので根では呼べない)。 */
	sPtr<pigDataTryCatch>	rootTry;
	sPtr<stdString>		cacheDir;
	sPtr<cgptsLemonParser>	parser;   /* 1.2.2 パーサ(ソース → tree) */
	sPtr<pigData>		tree;     /* パーズ結果のプログラムツリー(root=export 等) */
	sPtr<tsSignal>		sig_int;   /* SIGINT  ハンドラ(self-pipe → TSE_SIGNAL) */
	sPtr<tsSignal>		sig_term;  /* SIGTERM ハンドラ(素の kill) */
	sPtr<tsSignal>		sig_hup;   /* SIGHUP  ハンドラ(端末切断) */
	sPtr<pigData>		syncTail;         /* async の sync 発行順チェーン末尾(初回は解決済み null) */
	int			asyncErrors;      /* async の累積エラー数 */
	/* ★ **表示済みの文言を全部**覚える (列挙で二重に出さないため)。
	 * ⚠ 「最後の 1 件」だけでは足りない: async は flush/drain で複数出すので、
	 *   1 件しか覚えないと先に出した分を列挙が再表示してしまう。
	 * ⚠ ここを **static にしない** — planner はプロセスに 1 つだが、可変な大域を増やさない
	 *   (モジュール側の lint と同じ方針・ひさ指示 2026-08-26)。 */
	sArray<sPtr<stdString> >	shownErrors;
	void			show_error_m(sPtr<stdString> m);   /* 表示 + 記録 */
	/* ★ #3417 (2026-09-06): 旧 sig_abort_flag / sig_abort_num を **廃止**した。
	 *   「撤収すべきか」を agentError と 2 重に持っていたのが混乱の元で、
	 *   実際 WAITAGENTS の `sig_abort_flag && get_agentError()==NULL` は
	 *   「2 つの真理値がズレている瞬間」を扱うためだけの分岐になっていた
	 *   (そのせいで 2 回目以降の Ctrl+C が無視されていた)。
	 *   ⇒ 撤収の唯一の指標は **get_agentError() != thNULL**。
	 *      シグナル番号は filter() がその場で exitCodeOut へ畳むので保持不要。
	 *   下の 2 つは「同じ手を二度打たない」ための記憶で、撤収の判定には使わない。 */
	unsigned		eval_error : 1;       /* 評価結果がエラー値だった(キャッシュ掃除を抑止) */
	unsigned		treeDestroyed : 1;    /* EVAL で tree->destroy() を撃った */
	unsigned		agentsDestroyed : 1;  /* WAITAGENTS で全 agent へ destroy を撃った */
	const char *		srcText;      /* 実行するソース(ctor 引数。NULL=env/既定にフォールバック) */
	sPtr<stdString>		srcName;      /* エラー表示用ファイル名(parser へ渡す) */
	int *			exitCodeOut;  /* 終了コードの書き込み先(ctor 引数。NULL なら下の local を指す) */
	int			exitCodeLocal;/* exitCodeOut が NULL のときの受け皿 */
	int			argvN;        /* ARGV: スクリプト後のコマンドライン引数(個数) */
	char **			argvV;        /* ARGV: 同・文字列配列(プロセス寿命・保持のみ) */
private:
	TS_DEFARGS
};

TS_END_IMPLEMENT

TS_BEGIN_INTERFACE
#include	"ts2/c++/sRptr.h"
#include	"ts2/c++/sArray.h"
#include	"ts2/c++/stdString.h"
class tinyState;
class tsApplication;
class pigEnvironment;
class pigData;
class pigDataTryCatch;   /* ★ #3482 段 3: 根の見えない try */
class stdString;
class cgptsLemonParser;
class tsSignal;
class stdEvent;
TS_END_INTERFACE

#endif


cgptsPlanner_::cgptsPlanner_(TS_ARGS0)
        : ptsApplication_(parent, PIG_MODLOAD_SEARCH),   /* ★ #3427 ③: モジュールは基底 INI が探索路ロード */
	  parent(tinyState_::parent)
{
    TS_CPARGS0
    treeDestroyed   = 0;
    agentsDestroyed = 0;
    eval_error     = 0;
    asyncErrors    = 0;
    syncTail       = thNEW(pigDataNull,());   /* 初回 async の prev=解決済み null(即発火可) */
    srcText       = _src;
    srcName       = thNEW(stdString,( _fname ? _fname : "<source>" ));
    exitCodeLocal = 0;
    exitCodeOut   = _exitCode ? _exitCode : &exitCodeLocal;
    argvN         = _argvN;
    argvV         = _argvV;
}

sPtr<pigEnvironment>
cgptsPlanner_::get_env()
{
	return env;
}

/* イベント前処理: 終了系シグナル(SIGINT/SIGTERM/SIGHUP の TSE_SIGNAL)を捕まえてフラグを立てる
 * (各状態の頭で参照)。実処理はしない(状態機械が安全な箇所で見て set_agentError → ドレイン → cleanup)。
 * 番号は最初の 1 つを保持(exit code = 128+signum、メッセージに使う)。 */
sPtr<stdEvent>
cgptsPlanner_::filter(sPtr<stdEvent> ev)
{
	if ( ev == thNULL )
		return ev;
	if ( ev->type == TSE_SIGNAL &&
	     ( ev->msg_int == SIGINT || ev->msg_int == SIGTERM || ev->msg_int == SIGHUP ) ) {
		/* ★ #3417 (2026-09-06): フラグではなく **撤収エラーそのもの**をここで作る。
		 *   ・撤収の指標を agentError 1 本に寄せる (旧 sig_abort_flag / sig_abort_num は廃止)
		 *   ・set_agentError が **全 agent の wake-all** を連れてくるので、イベント待ちで
		 *     詰まった agent にも届く (フラグ代入だけにすると届かない)
		 *   ・PE_FATAL = 「待つ意味がない」。既存の「fatal は in-flight agent を即撤収」に乗る
		 *   ・終了コード 128+signum はここで畳む (signum を後まで持ち回らずに済む)
		 *   ⚠ tsSignal は self-pipe → TSE_SIGNAL なので、ここは **シグナルハンドラ文脈ではない**。
		 *     thNEW してよい。
		 *   ⚠ 先勝ち: 2 回目以降のシグナルでは何も起きない (連打で段階を上げる設計は採らない。
		 *     is_destroyed() は冪等で 2 度目の destroy が伝わらないうえ、キーボードの
		 *     チャッタリングで graceful のつもりが kill になる — ひさ判断 2026-09-06)。 */
		if ( get_agentError() == thNULL ) {
			const int sn = (int)ev->msg_int;
			const char *nm = ( sn == SIGTERM ) ? "SIGTERM" : ( sn == SIGHUP ) ? "SIGHUP" : "SIGINT";
			char msg[64];
			::snprintf(msg, sizeof msg, "interrupted by %s", nm);
			(*exitCodeOut) = 128 + sn;
			set_agentError(thNEW(pigDataError,(msg, thNULL, PE_FATAL)));
		}
	}
	return TS_BASECLASS::filter(ev);
}


/* ===== #3366: async export レジストリ(planner 所有)と export 族演算子 =====
 * export/export_async/flush は srava 言語固有の I/O シンクなので、データ層(pigData)ではなく
 * srava アプリ層のこの planner に置く。演算子の _start() 定義もここに置く。演算子は
 * caller_planner()(呼び出し元の状態機械 → ptsObject の ptsApp → planner へ d_cast)で
 * 「自分の所属プランナ」へ届く。グローバル無し・per-caller なので**複数 planner 同時実行でも
 * 各演算子が自分の planner を引く**(pigData にメンバは足さない。pigData は planner を知らないまま)。 */

/* 演算子の呼び出し元(状態機械)→ ptsObject の ptsApp を自分の planner へ下方 d_cast して返す。
 * caller が pigf 文脈外 / app が planner でなければ thNULL(= 登録スキップ)。
 * NB: d_cast は **interface 型 cgptsPlanner**(=public ptsApplication)で行う。impl 型 cgptsPlanner_
 *     では sPtr 型システム上 cast できない。is_notNull() は INVERTED(true=有効)。
 *     変数読み出し演算子 pigDataOperatorVariable と同じ caller 経路。 */
static sPtr<cgptsPlanner> caller_planner() {
  sPtr<ptsObject> f = sPtr<ptsObject>::d_cast(sCallSection::key->caller());
  if ( ! f.is_notNull() ) return sPtr<cgptsPlanner>();   /* caller が ptsObject でない(pigf 文脈外) */
  return sPtr<cgptsPlanner>::d_cast(f->ptsApp);          /* ptsApp(=このプランナ)へダウンキャスト */
}

/* export(x) ダミー: 単一引数。エラーはそのまま吸収。継続なら実値(cdr=promise)を、でなければ
 * 引数をそのまま result へ(car()/cdr() は compact ゲートで上流を起動・解決する)。レジストリ不使用。 */
void pigcgOperatorExport::_start() {
  /* ★ #3450 (ひさ規則 2026-08-29): **helper を呼ぶ op はその helper (の FIN) が clean() の責任を
   * 持つ。helper を呼ばない同期 op は _start() の末尾で自分で clean() する。**
   * この op は helper を作らない (result を同期に決めるだけ) ので後者。clean() が無いと args が
   * 誰にも切られず、args[0] の継続 pair から中間結果の pigDataCache までがプログラム終了まで残る
   * (in-proc ではメッシュ実体ごと = 実測で N+3 個の常駐)。result は clean() では触らないので
   * 呼び手の観測は壊れない。 */
  if (args.length() == 0) { result = thNEW(pigDataNull, ()); clean(); return; }
  sPtr<pigData> a = args[0];
  if (a->is_error()) { result = a; clean(); return; }
  if (pig_is_delayed(a))
    result = a->cdr()->cdr();   /* 継続の実値(結果。"begin" 段を飛ばす) */
  else
    result = a;
  clean();
}

/* ---- async 文: 統一プリミティブ(sync 発行順チェーン + drain) ---- */
sPtr<pigData> cgptsPlanner_::sync_tail()                     { return syncTail; }
void          cgptsPlanner_::set_sync_tail(sPtr<pigData> t)  { syncTail = t; }
int           cgptsPlanner_::async_error_total()            { return asyncErrors; }

/* ★★ #3482: 旧 @flush_async@ (planner が asyncList を掃き出す) は撤去した。
 * @flush()@ は **その地点を囲む try の待ちリスト**が空になるまで待つバリアになり、待ちの台帳は
 * 根の見えない try が持つ。ここに残るのは **発行順チェーンの reset** だけ
 * (flush 後の async は独立した発行順になる)。 */
void cgptsPlanner_::reset_sync_chain() {
  syncTail = thNEW(pigDataNull,());
}

/* 主エラー以外に agent が出した理由を列挙する (宣言側にねらいを記載)。 */
/* 表示して「表示済み」に積む。列挙 (show_other_agent_errors) がこれを見て重複を避ける。 */
void cgptsPlanner_::show_error_m(sPtr<stdString> m) {
  if ( ! m.is_notNull() ) return;
  shownErrors.push(m);
  show_error(m->get_str());
}

void cgptsPlanner_::show_other_agent_errors() {
  int n = agent_error_count();
  int shown = 0;
  for ( int i = 0 ; i < n ; ++i ) {
    sPtr<pigData> e = agent_error_at(i);
    if ( ! e.is_notNull() ) continue;
    sPtr<stdString> m = e->get_str();
    if ( ! m.is_notNull() ) continue;
    int dup = 0;
    for ( int j = 0 ; j < shownErrors.length() ; ++j )
      if ( ::strcmp(shownErrors[j]->get_str(), m->get_str()) == 0 ) { dup = 1; break; }
    if ( dup ) continue;                                   /* 既に表示済み */
    if ( shown == 0 )
      ::fprintf(stderr, "[srava] other agents reported:\n");
    ::fprintf(stderr, "  - %s\n", m->get_str());
    ++shown;
  }
  if ( shown > 0 ) ::fprintf(stderr, "\n");
}

/* 末尾 (全 agent 完了後): **根の見えない try に溜まったエラー**を 1 度だけ報告する
 * (continue-and-collect)。★ #3482: 以前は planner の asyncList を compact して集めていたが、
 * async も agent も根の try の待ちリストに入るようになったので、**待つのは WAITAGENTS が済ませ**、
 * ここは溜まった列を読むだけになった。
 * ⚠ 利用者が書いた try の中の async は **その try が持つ** (catch が拾う) のでここには来ない。 */
int cgptsPlanner_::drain_async() {
  int errs = 0;
  sPtr<pigDataTryCatch> rt = root_try();
  if ( rt.is_notNull() ) {
    int n = rt->error_count();
    for ( int i = 0 ; i < n ; ++i ) {
      sPtr<pigData> r = rt->error_at(i);
      /* ★ 畳まれた跡 (PE_DERIVED) は新しい失敗ではないので報告しない (終了コードにも出さない)。 */
      if ( r.is_notNull() && r->is_error() && ! r->is_derived() ) { show_error_m(r->get_str()); ++errs; }
    }
  }
  asyncErrors += errs;
  return errs;
}

/* async { body...; sync: S }: body 文を pigfAsync helper として **非ブロッキング**に起動し、
 * syncTail チェーン(発行順)へ繋いで即 null を返す。_start はトップレベル sequence の直列評価で
 * **ソース順**に走る(print_async と同じ)ので、ここで prev=syncTail を取り新 front を繋げばよい。 */
void pigcgOperatorAsync::_start() {
  sPtr<cgptsPlanner> pl = caller_planner();
  if ( pl.is_notNull() ) {                       /* 有効なプランナが取れた(pigf 文脈内) */
    sPtr<pigDataFunction<pigfAsync> > f = thNEW(pigDataFunction<pigfAsync>,());
    f->pushArg(pl->sync_tail());                 /* args[0] = prev(前 async の done 信号) */
    for ( int i = 0 ; i < args.length() ; ++i )
      f->pushArg(args[i]);                        /* body 文(+ hasSync なら末尾 sync 文) */
    f->set_mode(get_mode());                      /* hasSync を helper へ伝える(front->get_mode) */
    f->set_info(get_info());
    /* ★ #3419 (ひさ設計 2026-08-24): 非ブロック起動 (body が並列に走り出す)。
     * ⚠ かつては `f->trigger()` だった。trigger は pigData の契約の外の意味論なので撤去し、
     * **待つ役の状態機械 (ptsFireAndForget) を 1 個生やして compact させる**形にした。
     * async 文はここで即 null を返して先へ進む = 非ブロックのまま。 */
    /* ★ 0 = ここでは報告しない。直後の register_async で drain 対象に入れ、
     * **末尾の drain_async が continue-and-collect で報告する** (二重報告を避ける)。 */
    (void) thNEW(ptsFireAndForget,(sPtr<ptsObject>::d_cast(sCallSection::key->caller()), f, 0));
    /* ★★ #3482: **async の待ちとエラーの行き先は try**。ptsFireAndForget が待ちリストへ入り、
     * エラーもそこへ渡す ⇒ planner 側の登録簿は要らなくなった (根の下の分は末尾で
     * drain_async が根の try から読んで報告する)。
     * ⚠ 発行順チェーン (syncTail) は待ちや報告とは別の話なので従来どおり繋ぐ。 */
    pl->set_sync_tail(f);                         /* 次の prev = この front */
  }
  result = thNEW(pigDataNull, ());
  /* ★ #3450 (ひさ規則): helper が付くのは上で作った f であって自分ではない = 自分は同期 op。
   * body は f へコピー済みなので、自分の args はここで手放す (でないとパース木の async ノードが
   * body 全体を掴んだままプログラム終了まで残る)。 */
  clean();
}

/* ★★ #3482: @flush()@ は **その地点を囲む try の待ちリストが空になるまで**待つバリア。
 * ★ statement1 の中でも catch 本体の中でも意味は同じ — 「この try で起動した計算 (agent も
 *   async も) が全部終わってから先へ進む」。根の見えない try の下 (= トップレベル) で呼べば
 *   従来どおり「全部」を待つことになる。
 * ⚠ **エラーの報告はしない**。エラーは try の列に残って catch の error() が読む
 *   (根の下の分は末尾で drain_async が報告する)。報告点を 2 つ持たないための決まり。
 * ⚠ 発行順チェーン (syncTail) の reset は従来どおり行う (flush 後の async は独立した発行順)。 */
void pigcgOperatorFlush::_start() {
  sPtr<cgptsPlanner> pl = caller_planner();
  if ( pl.is_notNull() )
    pl->reset_sync_chain();
  sPtr<ptsObject> f = sPtr<ptsObject>::d_cast(sCallSection::key->caller());
  sPtr<pigEnvironment> e = f.is_notNull() ? f->get_env() : sPtr<pigEnvironment>(thNULL);
  /* ★★ #3564: **sPtr で受ける**。生ポインタだった間、下の null 検査が拾えるのは
   *   「一度も入っていない」だけで、**「入ったが畳まれた」は素通り**していた。 */
  sPtr<pigDataTryCatch> t = e.is_notNull() ? e->get_try() : sPtr<pigDataTryCatch>();
  /* ★ #3562: 性質の違う 2 つを同じ袋に入れない。
   *   t が無い          … 根の見えない try が必ず在るので、**引けないのは配線の異常**。
   *                       ⇒ 黙って「完了」に化けさせず落とす (#3482 の「黙って根へ落とさない」)。
   *                       ★ #3564: sPtr になったので「畳まれた try」もここで捕まる。
   *   agent_live() == 0 … **正常**。待つものが無いので即 null を返す。 */
  if ( ! t.is_notNull() )
    stdObject::panic("flush(): no enclosing try (env relay is broken)");
  if ( t->agent_live() == 0 ) {
    result = thNEW(pigDataNull, ());
    clean();   /* ★ #3450 (ひさ規則): helper を呼ばない同期 op は _start 末尾で clean */
    return;
  }
  /* まだ走っている ⇒ **待ちに入る** (error() と同じ作法: result を立てず helper を持つと
   * preprocess が呼び手を listener にして yield し、空になった時点で set_result が起こす)。 */
  sPtr<tinyState> th = t->try_helper();
  /* ★ #3562: 預け先が無いのは **配線の異常** (待ちリストは生きているのに listen 先が無い)。
   * 黙って「完了」を返すと flush がバリアにならないまま素通りする — 今回それが
   * 「テストが間欠的に赤い」以外の痕跡を残さなかった。⇒ 落として core を残す。 */
  if ( ! th.is_notNull() )
    stdObject::panic("flush(): try_helper() is NULL (try helper not published)");
  t->register_drain_waiter(sPtr<pigDataDelay>::d_cast(thThis));
  helper = th;
}


/* ═══ #3556: 撤収の座り込みを名指しする trace (既定 OFF) ═══════════════════
 *
 * tinyState の反応器 fwIO::loop は
 *     read の登録 0 ・ write の登録 0 ・ interval 無し ・ refio 0
 * の **4 つ全部** が揃って初めて返る。1 つでも残れば INFINITE で眠る。MinGW は IOCP なので
 * 「相手が消えた」を誰も post しない = 永久に何も来ない (POSIX の select は EOF を読める側で
 * 教えるので露見しない)。box のフル ctest で毎回別のテストが TIMEOUT し、番犬の撮ったスタックが
 * どれも fwIO::loop → FIN_THREAD_ROOT_LOOP なのがこれ。
 *
 * gdb からは refio / refio_pins が読めない (tinyState は -g 無しの .a を静的リンク)。そこで
 * **ライブラリが既に持っている trace を、こちらから点ける** (ひさ提案 2026-09-19):
 *     stdFrameWork::trace_all   非 0 なら fwIO::loop が毎周 dump(trace_all) を呼ぶ
 *     stdFrameWork::trace_bit   dump が出す節の選択 (READ / WRITE / INTERVAL / ACTIVE)
 *     tinyState::trace_all      全オブジェクトの状態遷移を出す (誰がまだ動いているか)
 * dump は登録ごとに fd と **親の連鎖** を出すので、ptsErrSink / ptsWirePipe のような居残りは
 * クラス名と持ち主で名指しできる。
 *
 *   SRAVA_FIN_TRACE=1            撤収に入った時点から毎周 read/write/interval の登録を出す
 *                  =2            + 配送したイベント (FWTR_ACTIVE)
 *                  =3            + 全状態遷移 (量は多い)
 *   SRAVA_FIN_TRACE_FROM_START=1 撤収を待たず INI から点ける (撤収に入る前に止まる場合)
 *   SRAVA_FIN_TRACE_DIR=<dir>    トレースを <dir>/fwtr-<pid>.txt へ**追い出す** (ctest 用・下記)
 *   SRAVA_FIN_TRACE_STDOUT=1     出力先を stdout のままにする (dir 指定が無いときの既定は stderr)
 *
 * ★★ **ctest のスイートに点けるときは SRAVA_FIN_TRACE_DIR を必ず使う**。
 *   テストの出力にトレースを 1 バイトでも混ぜると、
 *       PASS_REGULAR_EXPRESSION "IVOL 2(\n|.)*DVOL 6"    (CMakeLists.txt に多数ある形)
 *   の `(\n|.)*` が KWSys の後戻り型エンジンで **破滅的バックトラック**を起こし、
 *   **ctest 自身が 100% CPU で空回りする**。2026-09-19 に実際に踏んだ:
 *   末尾に 2051 バイト足しただけで ctest が 85 分進まず、テスト側は 0.1 秒で正常終了して
 *   zombie になっていた (= 固まっているのは srava ではなく ctest)。量ではなく**後続バイト**が効く。
 *   → [[ctest-pass-regex-backtracks]]
 *
 * ★ dir 指定が無いときは **stderr へ逃がす** (点けた時点で dup2(2,1))。dump は ::printf = stdout に
 *   書くので、そのままだと stdout を比較しているテストが落ちる。逃がすのは点けた後だけで、
 *   srava 本来の出力 (結果・サマリ) は撤収より前に出し終えている。
 *   ⚠ FROM_START のときは **起動時から** 逃がすので、srava 自身の stdout も丸ごと道連れになる。
 *     こちらは 1 本を狙い撃ちするときのモード。
 *
 * ⚠⚠ **planner でしか点けない**。dump の出力先は ::printf = stdout だが、agent プロセスの
 *    stdout (fd1) は **pigwire のワイヤそのもの** (ptsAgentApplication が fd0=rio/fd1=wio に
 *    する)。agent 側で点けるとトレース文がレコード列に混ざり、planner が
 *    「agent closed unexpectedly」で落ちる (2026-09-19 に実際に踏んだ)。agent の中を見たければ
 *    ワイヤを畳んでからでないと触れないので、別の道具が要る。
 * ⚠ dump は stdout なので、行バッファのままだと**肝心の最後の 1 周がバッファに残って見えない**
 *   (返ってこないのが症状 = 誰も flush しない)。点けると同時に無バッファにする
 *   (setvbuf は先に fflush してバッファを空にしてから呼ぶ。Linux/MinGW とも効くことを確認済み)。
 * ⚠ 4 つのうち **refio だけは fwIO の private で dump に出ない**。他の 3 つが空なのに抜けないなら
 *   残りは refio、と**引き算で**読む。
 */
static void
cg_fin_trace_arm(const char *where)
{
	int lv = osglue_env_int("SRAVA_FIN_TRACE", 0);
	if ( lv <= 0 || stdFrameWork::trace_all != 0 )
		return;                        /* OFF / もう点いている */
	static char tag[64];               /* trace_all は const char* を持ち回るので静的寿命が要る */
	::snprintf(tag, sizeof tag, "[fwtr %u %s]", (unsigned)osglue_getpid(), where);
	::fflush(stdout);                  /* ★ setvbuf / dup2 の前にバッファを空にしておく */
	const char *tdir = ::getenv("SRAVA_FIN_TRACE_DIR");
	if ( tdir != 0 && tdir[0] != '\0' ) {
		/* ★ テストの出力に 1 バイトも混ぜない = ctest の PASS 正規表現を踏まない。
		 *   ハングを追う道具なので、殺されても残るファイルに置くのが本来正しい。 */
		char path[4096];
		::snprintf(path, sizeof path, "%s/fwtr-%u.txt", tdir, (unsigned)osglue_getpid());
		if ( ::freopen(path, "w", stdout) == 0 ) {
			::fprintf(stderr, "[fwtr] cannot open %s — トレースは出しません\n", path);
			return;
		}
	} else if ( ! osglue_env_int("SRAVA_FIN_TRACE_STDOUT", 0) ) {
		::fflush(stderr);
		::dup2(2, 1);              /* 以降の stdout = stderr。テストの出力比較を汚さない */
	}
	::setvbuf(stdout, 0, _IONBF, 0);   /* ⚠ 眠ったまま返らないので溜めない */
	stdFrameWork::trace_bit = (int8_t)( ( lv >= 2 ) ? FWTR_ALL : FWTR_RWI );
	stdFrameWork::trace_all = tag;     /* ← これで fwIO::loop が毎周 dump する */
	if ( lv >= 3 )
		tinyState::trace_all = tag;
	::printf("%s ON level=%d — 以降 fwIO の登録を毎周出す。"
	         "READ/WRITE/INTERVAL が全部空でも抜けないなら、残っているのは refio。\n",
	         tag, lv);
}

/*******************************************
	STATE MACHINE
********************************************/

TS_STATE(INI_ptsApplication_START)   /* ptsApplication 派生: ptsApp=自分 の後にここで初期化 */
{
	/* ★ #3556: SRAVA_FIN_TRACE_FROM_START=1 のときだけ、撤収を待たずここで点ける。 */
	if ( osglue_env_int("SRAVA_FIN_TRACE_FROM_START", 0) )
		cg_fin_trace_arm("INI");

	/* ★ #3427 ③: srava 言語の VALUE パーサを app 所有レジストリへ登録 (旧: cgptsLemonParser.cpp の
	 *   静的初期化がグローバルスロットへ自己登録)。この planner を持つ実行体 = 言語パーサを持つ実行体。 */
	if ( module_registry != thNULL )
		module_registry->vparser.register_parser("srava", &cg_mk_value_parser);

	/* CACHE_DIR を決定(env SRAVA_CACHE_DIR、未定義なら既定 $PWD/tmp = カレントディレクトリ配下)。
	 * pigfAgent はこの env=CACHE_DIR を引いてキャッシュパスを作る。 */
	const char *cd = ::getenv("SRAVA_CACHE_DIR");
	char cdbuf[4096];
	if ( cd == 0 ) {
		if ( ::getcwd(cdbuf, sizeof cdbuf - 8) == 0 ) ::strcpy(cdbuf, ".");
		::strcat(cdbuf, "/tmp");
		cd = cdbuf;
	}
	cacheDir = thNEW(stdString,(cd));

	env = thNEW(pigEnvironment,(thNULL));
	/* ★★ #3482 段 3: 根の見えない try を立て、**根の env に刺す**。以降、env を作る側
	 * (pigfSequence / pigfAsync / pigfApply) が引き継ぐので、プログラム中のどの地点でも
	 * 「囲む try」が必ず在る。⚠ ここより前に env を作る経路は無い (planner の INI が最初)。 */
	/* ★★ #3482: 根の見えない try は **ptsApplication が INI で作る** (撤収の理由と診断台帳の
	 * 持ち主でもあるため)。planner はそれを **根の env に刺す**役だけ。 */
	rootTry = root_try();
	env->set_try(rootTry);
	env->def_var(thNEW(stdString,("CACHE_DIR")), thNEW(pigDataString,(cd)));
	/* CACHE_RETAIN: 終了時キャッシュ掃除の保持方針。CACHE_DIR 同様 env(SRAVA_CACHE_RETAIN)を初期値に
	 * 事前定義し、プログラムから `CACHE_RETAIN = "14d";` で上書きできる(代入が env より優先)。
	 * 未設定なら空文字 = 即削除(従来既定)。解釈は終了時 parse_cache_retain。 */
	const char *cr = ::getenv("SRAVA_CACHE_RETAIN");
	env->def_var(thNEW(stdString,("CACHE_RETAIN")), thNEW(pigDataString,((cr != 0) ? cr : "")));
	/* ★ EXIT_CODE: プロセスの終了コードを **明示指定** する予約変数 (2026-08-11 ひさ設計)。
	 * `EXIT_CODE = 3;` と代入すると終了コードが 3 になる。CACHE_DIR/CACHE_RETAIN と同じ
	 * 「事前定義 + 代入で上書き」の idiom で、副作用が終了コードだけに閉じる (評価は最後まで走る)。
	 * 即時終了したい場合は組込 exit(n) を使う。既定 0 = 成功 (POSIX 慣行)。
	 * 反映は CLEANUP。**エラー終了時はエラーコード (1 / 128+signum) が優先** する。 */
	env->def_var(thNEW(stdString,("EXIT_CODE")), thNEW(pigDataInteger,((INTEGER64)0)));

	/* ★★ #3555 段2: USE_MODULES — `module::` を **省略したとき**の候補列 (= <変数>::op と同じ意味)。
	 * CACHE_DIR / CACHE_RETAIN と同じ流儀で「環境変数を初期値に事前定義」する。狙いは 3 つとも
	 * この idiom で既に満たされている:
	 *   - 事前定義  ⇒ 未定義変数エラーが起きない
	 *   - 使用地点で env->get_var ⇒ routing (decide_out_module) からそのまま引ける
	 *   - 親チェーンで引ける ⇒ **ブロック / lambda の中だけ差し替えて、抜ければ戻る**
	 * ⚠ 環境変数は配列を持てないので **',' 区切り**を配列として読む (SRAVA_USE_MODULES=occt,cgal)。
	 *   未設定 / 空文字は "" = **指名なし** = 従来どおり priority 順 ⇒ 既存の式は 1 つも変わらない。
	 *   ★ 空配列 [] は「候補ゼロ」= エラーなので、ここで [] を作らないこと。 */
	{
		const char *um = ::getenv("SRAVA_USE_MODULES");
		sPtr<pigData> umv;
		if ( um != 0 && um[0] != '\0' ) {
			sPtr<pigDataArray> a = thNEW(pigDataArray,());
			const char *p = um;
			while ( *p != '\0' ) {
				while ( *p == ' ' || *p == '\t' ) ++p;
				const char *q = p;
				while ( *q != '\0' && *q != ',' ) ++q;
				const char *e = q;
				while ( e > p && ( e[-1] == ' ' || e[-1] == '\t' ) ) --e;
				if ( e > p ) {
					char tb[256];
					size_t tn = (size_t)(e - p);
					if ( tn >= sizeof tb ) tn = sizeof tb - 1;
					::memcpy(tb, p, tn); tb[tn] = '\0';
					a->push_nocheck(thNEW(pigDataString,(tb)));
				}
				p = ( *q == ',' ) ? q + 1 : q;
			}
			if ( a->length() > 0 ) umv = a;
		}
		env->def_var(thNEW(stdString,("USE_MODULES")),
		             umv.is_notNull() ? umv : sPtr<pigData>(thNEW(pigDataString,(""))));
	}

	/* ★ #3419 §17.3 (ひさ案 2026-08-24): **負荷コントロール / ゲート / 実験用の口を srava 変数にする**。
	 * CACHE_DIR / CACHE_RETAIN と同じ流儀: **環境変数を初期値に事前定義**し、プログラムからの
	 * 代入が優先される。`LOAD_CPU = 50;` と書けば env SRAVA_LOAD_CPU=50 と同じ意味になる。
	 *
	 * ★ 空文字を既定にしてある = 「指定なし」。使う側 (ptsApplication::cfg_int) は
	 *   **変数 → 環境変数 → 既定** の順に解決するので、未代入なら従来どおり環境変数が効く。
	 * ⚠ **反映は co_ptsConfigWatch の周期チェック (250ms) 経由**。起動時にしか読まれない
	 *   設定 (LOAD_CPU_MS) に代入すると「効きません」と警告が出る (§17.3)。
	 *   ★ LOAD_RAMP_START は #3451 で「最初の pigfAgent 入場まで」に緩和済み — script 冒頭の
	 *   代入は間に合う。それより後の代入は同様に「効きません」の対象。
	 * ⚠ srava 変数側は SRAVA_ を落とした名前にする (CACHE_DIR の流儀)。 */
	{
		/* ★ 表から **実効値** (環境変数 → 既定) で事前定義する。
		 * ⚠ 空文字で定義すると `print(LOAD_CPU)` が空欄になり「読めるようにする」が
		 *   半分しか満たせない (2026-08-24 に一度そうしてしまった)。実効値を入れる。 */
		const struct pigCfgEntry *t = pigcfg_table();
		for ( int i = 0 ; t[i].var != 0 ; ++i ) {
			const char *e = ::getenv(t[i].env);
			const char *v = ( e != 0 && e[0] != 0 ) ? e : t[i].def;
			env->def_var(thNEW(stdString,(t[i].var)), thNEW(pigDataString,(v)));
		}
	}
	/* ★ 設定の解決に使うので、app に根 env を預ける (§17.3)。 */
	if ( ptsApp.is_notNull() )
		ptsApp->set_root_env(env);
	/* ★ .so 化 Phase 4c: 言語変数 DEFAULT_OUTPUT と env SRAVA_DEFAULT_OUTPUT を撤去した。
	 *   既定カーネルは registry の priority 最大 (default_module_name・既定 cgal)。
	 *   切替は `module("manifold.so", {priority: N})` / 個別指定は `cast("manifold", …)` (docs §2.4)。 */
	/* ARGV: 起動時コマンドライン引数(スクリプト後の argv[2..])の文字列配列。事前定義の読み取り変数。
	 * 例: `srava model.sra a b c` → ARGV = ["a","b","c"]。未指定なら空配列 []。 */
	{
		sPtr<pigDataArray> av = thNEW(pigDataArray,());
		for ( int i = 0 ; i < argvN ; ++i )
			av->push(thNEW(pigDataString,( argvV[i] ? argvV[i] : "" )));
		env->def_var(thNEW(stdString,("ARGV")), av);
	}
	/* キャッシュ dir の作成(mkdir -p)+ 起動時スイープは「最初に動いた pigfAgent の頭」で実行する
	 * (= それまでにプログラムが CACHE_DIR を set_var で変更できる)。機構は pig 層 pigCacheManager。
	 * ここでは「何が変わったらキャッシュ無効か」の版数指紋(srava 固有)を ptsApp に設定するだけ
	 * (pigfAgent が起動時スイープへ渡す)。 */
	char fp[512];
	compute_cache_fingerprint(fp, sizeof fp);
	cache_set_fingerprint(fp);

	/* 終了系シグナル(SIGINT=Ctrl+C / SIGTERM=素の kill / SIGHUP=端末切断)を TSE_SIGNAL イベント化
	 * (self-pipe)。filter() がフラグを立て、各状態が見て撤収する。tsSignal がハンドラを差し替えるので、
	 * これより前に raise すると既定動作で即死する点に注意。3 つとも filter() で同じ撤収経路へ合流する。 */
	sig_int  = thNEW(tsSignal,(ifThis, SIGINT));
	sig_term = thNEW(tsSignal,(ifThis, SIGTERM));
	sig_hup  = thNEW(tsSignal,(ifThis, SIGHUP));

	/* 1.2.2 パージング: ソース文字列(env SRAVA_SOURCE か既定)を lemonc++ パーサに渡し
	 * pigData ツリーを得る。パーサは同期 rDO で完走し TSE_RETURN(tree) を返す。 */
	/* 優先順: コマンドラインのソースファイル(ctor 引数 srcText) > env SRAVA_SOURCE > 既定。 */
	const char *srcEnv = ::getenv("SRAVA_SOURCE");
	const char *srcSel = srcText ? srcText : ( srcEnv ? srcEnv : DEFAULT_SOURCE );
	/* ★ #3452: SRAVA_MODULE_ALL=1 で `include "module/all.sra";` を実ソースの前に合成する。
	 * #3452 で起動時 eager-load を撤去した移行期の便宜口 (旧挙動に近い状態へ一括で戻す)。
	 * 個々のスクリプトを書き換えずに済ませたい既存テスト・env 常設運用向け。
	 * script 本体を書き換えるより「呼び出し環境で全ロードを要求する」方が自然な用途では
	 * こちらを使う (script 内で完結させたいなら include を直接書く)。 */
	std::string srcPrelude;
	if ( osglue_env_int("SRAVA_MODULE_ALL", 0) ) {
		srcPrelude = "include \"module/all.sra\";\n";
		srcPrelude += srcSel;
		srcSel = srcPrelude.c_str();
	}
	sPtr<stdString> src = thNEW(stdString,(srcSel));

	/* テスト用: 自分に終了系シグナルを送る(self-pipe 経由で次の yield 時に TSE_SIGNAL 配送)。
	 * tsSignal 設置後に呼ぶこと(既定動作回避)。PIG_TEST_SLOW と併用で確実に評価中に届く。
	 * PIG_TEST_RAISE_SIGINT=後方互換。PIG_TEST_RAISE_SIGNAL=<番号> で任意シグナル(TERM/HUP 検証)。 */
	if ( osglue_env_int("PIG_TEST_RAISE_SIGINT", 0) )
		::raise(SIGINT);
	const char *rsEnv = ::getenv("PIG_TEST_RAISE_SIGNAL");
	if ( rsEnv != 0 )
		::raise(::atoi(rsEnv));

	/* テスト用: SRAVA_VALUE が在れば VALUE モードでパース→serialize を表示して終了
	 * (値リテラルの serialize↔VALUE パース 往復検証。ワイヤ値表現の共有確認)。 */
	const char *valEnv = ::getenv("SRAVA_VALUE");
	if ( valEnv != 0 ) {
		parser = thNEW(cgptsLemonParser,(ifThis, thNEW(stdString,(valEnv)), 1, thNULL));
		return ACT_cgptsPlanner_VALUE;
	}

	parser = thNEW(cgptsLemonParser,(ifThis, src, 0, srcName));   /* 0=PROGRAM・ファイル名は ctor 由来 */
	return ACT_cgptsPlanner_PARSE;   /* parser の TSE_RETURN(tree)待ち → rDO なし */
}

/* 1.2.2 続き: パーサ結果(tree or pigDataError)を受け取る。 */
TS_STATE(ACT_cgptsPlanner_PARSE)
{
	if ( ev->type == TSE_RETURN && ev->source == parser ) {
		tree = sPtr<pigData>::d_cast(ev->msg_obj);
		/* ★ #3452 で判明した回帰の修正: 可変ソート(可換 op の引数正規化)は旧 tree->normalize() が
		 * parse 直後にここで 1 回だけ行っていたが、その時点ではモジュールが 1 本もロードされて
		 * おらず op_commutative() が常に false を返していた(#3452 で起動時 eager-load を撤去した
		 * ため)。normalize() は撤去し、モジュール登録が済んでいる eval 時の 2 箇所
		 * (pigfModuleAgent::try_decompose / pigfAgent::compute_arg_hash) へソートを移設した。 */
		if ( is_destroyed() )
			return rDO|FIN_START;   /* 撤収中: 評価には進まない */
		return rDO|ACT_cgptsPlanner_EVAL;
	}
	/* ★ destroy の作法 (ひさ指示 2026-08-06): 子へ destroy() を送り、TSE_RETURN が
	 * 戻るのを **待ち続ける**。即 FIN しない。destroy された側が自分の終了処理をするので、
	 * こちらは戻ってくる内容に関知しない。 */
	if ( is_destroyed() ) {
		if ( parser.is_notNull() ) { parser->destroy(); return 0; }
		return rDO|FIN_START;
	}
	return 0;
}

/* テスト用 VALUE モード: パース結果を serialize して表示し終了。 */
TS_STATE(ACT_cgptsPlanner_VALUE)
{
	if ( ev->type == TSE_RETURN && ev->source == parser ) {
		sPtr<pigData> v = sPtr<pigData>::d_cast(ev->msg_obj);
		::printf("[srava] value=%s\n", v->serialize()->get_str());
		::fflush(stdout);   /* VALUE モードは CLEANUP(:660 の flush)を通らず即 FIN_START。
		                     * Windows/Cygwin はパイプ時 stdout フルバッファなので、ここで
		                     * flush しないと value= が終了時に失われる(srava_value_roundtrip)。 */
		(*exitCodeOut) = v->is_error() ? 1 : 0;
		return rDO|FIN_START;
	}
	/* ★ destroy の作法 (ひさ指示 2026-08-06): 子へ destroy() を送り、TSE_RETURN が
	 * 戻るのを **待ち続ける**。即 FIN しない。destroy された側が自分の終了処理をするので、
	 * こちらは戻ってくる内容に関知しない。 */
	if ( is_destroyed() ) {
		if ( parser.is_notNull() ) { parser->destroy(); return 0; }
		return rDO|FIN_START;
	}
	return 0;
}

/* 1.2.4 評価。tree(=export)を観測すると、継続 promise の解決(agent 計算完了)まで compact が
 * yield → 本状態が再走(is_error/get_int は冪等)。評価に使われなかった枝(未解決の pigDataDelay)は
 * 放置する(関数型: 欲しいものが得られればよい)。 */
TS_STATE(ACT_cgptsPlanner_EVAL)
{
	/* ★ #3417 (2026-09-06): 撤収が始まっていたら **評価ツリーへ destroy を撃つ**。
	 *   ⚠ 打ってから `tree->is_error()` の解決を待つ (この順序が本質)。is_error() の答えが
	 *     出た後に destroy しても pigDataError は pigDataError のままで何も起きない。
	 *   ★ なぜ要るか: 正常終了 (exit(msg)) の経路には既に tree->destroy() が在り、そのコメントが
	 *     「**agent 以外の helper (pigfSystem 等) はこの経路でしか止まらない**」と書いている。
	 *     撤収経路に無いのは非対称で、Ctrl+C では system() の子プロセスが止まらなかった。
	 *   ★ 撤収の契機はシグナルに限らない。agent が 1 つ落ちても set_agentError が立つので、
	 *     「1 つ落ちたら全部畳んで planner も終わる」という既存の流れとここで合流する。
	 *   ⚠ 一度だけ打つ (destroy は冪等だが、EVAL は yield で何度も再入する)。 */
	if ( get_agentError() != thNULL && ! treeDestroyed ) {
		treeDestroyed = 1;
		if ( tree != thNULL ) tree->destroy();
	}
	if ( tree->is_error() ) {
		sPtr<pigData> tv = tree->compact();
		if ( tv->control_kind() == CTRL_EXIT ) {   /* exit(msg): 正常終了。メッセージがあれば表示し exit 0。
		                                              先行 export_async は WAITAGENTS 経路で drain される。 */
			sPtr<pigData> mv = tv->control_value();
			sPtr<pigDataNull> isNull = sPtr<pigDataNull>::d_cast(mv);
			if ( ! isNull.is_notNull() )           /* メッセージあり(null でない) */
				::fprintf(stderr, "[srava] exit: %s\n", mv->get_str()->get_str());
			/* ★ 終了コードは 0 固定でなく **予約変数 EXIT_CODE** を見る (2026-08-11)。
			 * `EXIT_CODE = 2; exit "中断";` で 2 を返せる。未設定なら既定 0 のまま = 従来どおり。
			 * 実際の反映は CLEANUP の EXIT_CODE 処理が行う (ここは 0 を置くだけ)。 */
			(*exitCodeOut) = 0;
			/* ★ 全体を終了させるのは planner の役目 (ひさ設計 2026-08-11)。pigDataControl を
			 * 受け取ったら `tree->destroy()` で「もう要らない」を式木の上流へ知らせる
			 * (pigData::destroy = 遅延ノードが helper を destroy し委譲先へ再帰)。
			 * pigfAgent は別途 SHOULD_ABORT/WAITAGENTS 経路でも畳まれるので重複するが、
			 * agent 以外の helper (pigfSystem 等) はこの経路でしか止まらない。 */
			tree->destroy();
			return rDO|ACT_cgptsPlanner_WAITAGENTS;
		}
		show_error_m(tree->get_str());
		/* ★ #3417 (2026-09-06): **既に立っている終了コードを上書きしない**。
		 *   シグナル撤収では filter() が 128+signum を置いてから tree->destroy() で
		 *   評価がエラーに落ちるので、無条件に 1 を書くと 130 が 1 に化ける (実測で踏んだ)。
		 *   CLEANUP の `if ( (*exitCodeOut) == 0 ) (*exitCodeOut) = 1;` と同じ流儀に揃える。 */
		if ( (*exitCodeOut) == 0 )
			(*exitCodeOut) = 1;
		eval_error = 1;   /* キャッシュ掃除を抑止(評価が途中で失敗 → usedCaches 不完全の恐れ) */
		/* 確定的な型/プログラムエラー(fatal: mesh+mesh・未定義変数・引数不一致等)は待つ意味がないので、
		 * SIGINT と同様に set_agentError で **in-flight agent を即撤収**して終了する。幾何の失敗等
		 * (fatal=0)は従来通り WAITAGENTS で drain(走り出した計算は完走させキャッシュ化)。 */
		sPtr<pigDataError> pe = sPtr<pigDataError>::d_cast(tree->compact());
		if ( pe.is_notNull() && pe->is_fatal() && get_agentError() == thNULL )
			set_agentError(thNEW(pigDataError,("aborted: fatal error")));
	} else {
		/* ★ 2026-08-11 修正: ここで **スクリプトの結果値を終了コードにしていた** のは不具合。
		 * 数値結果は値がそのまま漏れ (300 → exit 44 / 256 → exit 0)、文字列など非数値では
		 * get_int() が不定値を返し **同一入力で毎回変わる**。旧 7/24 版は 0 を返していたので退行だった。
		 * 成功 = 0 (POSIX 慣行) に戻す。明示指定は予約変数 EXIT_CODE (CLEANUP で反映)。
		 *
		 * ★★ #3541① (2026-09-15): ⚠ **撤収中はここを通っても 0 を書かない**。
		 *   @c system() を待っている間に SIGINT が来ると、撤収は届くのに *式の評価そのものは
		 *   成功して終わる* (子を殺した status が値として返るだけ)。⇒ この枝に入り、
		 *   filter() が置いた 128+signum を **0 で上書き**していた。CLEANUP はその 0 を見て
		 *   「エラーなら 1」の規則で 1 を書く ⇒ **130 が 1 に化ける**。
		 *   実測 (macMINI 2026-09-15・PIG_DBG_TD):
		 *       system 経路  filter: exitCode=130 → **CLEANUP 入口: exitCode=0** → 終了 1
		 *       agent 経路   filter: exitCode=130 → CLEANUP 入口: exitCode=130 → 終了 130
		 *     ⇒ agent 経路で正しかったのは *評価がエラーに落ちて* 上の guard つきの枝を
		 *       通っていたからで、①が直っていたからではない。
		 *   ★ 指標は agentError 1 本 (#3417 の方針)。撤収 = agentError が立っている、なので
		 *     そこを見れば signum を持ち回らずに済む。⚠ agent 由来のエラーでも 0 は書かない
		 *     方が正しい (CLEANUP の `ae != thNULL` 枝が 1 を立てる)。
		 *   ⚠ 「評価は成功したが中断された」という **両方成り立つ状態**が在ることが要点。
		 *     *成功* を *中断されなかった* の代理にしない。 */
		if ( get_agentError() == thNULL )
			(*exitCodeOut) = 0;
	}
	return rDO|ACT_cgptsPlanner_WAITAGENTS;
}

/* 全 pigfAgent がクリーンになるのを待つ。promise は A_SAVE_BEGIN(本体書込前)で解決されるので、
 * 評価完了時点でも agent はまだ書込中・終了処理中のことがある。countAgent==0 を待って初めて
 *  (a) キャッシュ清掃が安全、(b) 解決後に出た agent エラー(agentError)を漏れなく拾える。
 * 起こし役は最後の pigfAgent の FIN(agent_leave→wakeup)。 */
TS_STATE(ACT_cgptsPlanner_WAITAGENTS)
{
	if ( osglue_env_int("PIG_DBG_TD", 0) ) ::fprintf(stderr, "[td] planner WAITAGENTS count=%d\n", agent_count());
	/* ★ #3417 (2026-09-06): 撤収中なら **生存中の全 agent へ destroy を撃つ**。
	 *   旧実装は `sig_abort_flag && get_agentError()==NULL` を見て INTERRUPT へ戻るだけで、
	 *   agent には何も送っていなかった (set_agentError の wake-all 頼み)。しかも
	 *   agentError が既に立っていれば何も起きないので、**2 回目以降の Ctrl+C が無効**だった。
	 *   ⇒ 指標が 1 本になったので、条件は「撤収中か」だけ。一度だけ撃つ。 */
	if ( get_agentError() != thNULL && ! agentsDestroyed ) {
		agentsDestroyed = 1;
		destroy_agents();
	}
	/* ★★ #3503 (ひさ設計 2026-09-07): **in-proc 居座りの panic はここで撃つ**。
	 *
	 * in-proc の実行体が destroy に応じないと、その agent は永久に畳まれず agent_count() は
	 * 0 にならない = ここで止まる。ptsMediatorInternal が猶予切れで要求を上げてくるので、
	 * **生きている MediatorExternal が 0 になったことを確認してから** abort する。
	 *
	 * ⚠ mediator に撃たせてはいけない — in-proc と process は同居するので、そのとき生きている
	 *   agent プロセスが全部迷子になる (子は setpgid で別プロセスグループに居るので端末の
	 *   シグナルも届かず、#3417 が潰した居残りに戻る)。撃つ前に子が居ないことを確かめられるのは
	 *   全体を見ている planner だけ。
	 * ★ abort にするのは core が残るため — 固まったスレッドのスタックが見えないと直しようがない。
	 * ⚠ 既定では要求そのものが上がらない (SRAVA_INPROC_PANIC_MS 未設定 = panic 無効)。 */
	{
		sPtr<pigData> ae = get_agentError();
		if ( ae != thNULL && ae->is_panic() ) {
			const int nx = ( ptsApp != thNULL ) ? ptsApp->ext_agent_count() : 0;
			if ( nx == 0 ) {
				sPtr<stdString> m = ae->get_str();
				::fprintf(stderr,
				    "\n*** srava: %s. No agent processes remain (nothing will be orphaned), "
				    "so the planner is aborting. "
				    "(SRAVA_INPROC_PANIC_MS sets the wait; exec_default:\"process\" avoids this) ***\n",
				    ( m.is_notNull() ) ? m->get_str() : "in-proc module did not fold");
				::fflush(stderr);
				::abort();
			}
			if ( osglue_env_int("PIG_DBG_TD", 0) )
				::fprintf(stderr, "[td] planner: PE_PANIC pending; %d agent process(es) left\n", nx);
			return 0;   /* 子が畳まれるのを待つ (ext_agent_del が最後の 1 つで wakeup) */
		}
	}
	if ( agent_count() == 0 )
		return rDO|ACT_cgptsPlanner_CLEANUP;
	return 0;   /* wakeup 待ち(最後の agent の agent_leave、または set_agentError) */
}

/* ★ #3417 (2026-09-06): ACT_cgptsPlanner_INTERRUPT は **廃止**した。
 *   やっていたのは set_agentError と exitCodeOut への代入の 2 行だけで、状態を 1 つ使う
 *   価値が無く、しかも「フラグは立っているがエラーは未集約」という中間状態を作るために
 *   WAITAGENTS に専用の分岐が要っていた (それが 2 回目以降の Ctrl+C を無効にしていた)。
 *   ⇒ どちらの仕事も filter() がシグナルを受けたその場で済ませる。
 *      exit code = 128 + signum (INT=130 / TERM=143 / HUP=129) も filter() が畳む。 */

/* SRAVA_CACHE_RETAIN を (mode, cutoff) に解釈する。終了時クリーンアップの方針を決める。
 *   返り = retain_mode: 0=即削除(未使用の完了キャッシュを全削除・既定) / 1=期日保持(cutoff より古い完了のみ削除) /
 *          2=全保持(完了キャッシュは消さない)。*cutoff には mode 1 のとき epoch 秒を入れる。
 *   受理する値:
 *     未設定 / "" / "0" / "now" / "immediate"  → 0(即削除)
 *     "all" / "keep" / "inf" / "-1"            → 2(全保持)
 *     "YYYY-MM-DD"                              → 1(その日 0:00 より前の完了を削除)
 *     "<num>[w|d|h|m]"(素の数値は日)           → 1(now から遡った期間より古い完了を削除)
 *   解釈できない値は安全側に倒して 0(即削除)。 */
static int parse_cache_retain(const char *s, time_t now, INTEGER64 *cutoff)
{
	*cutoff = 0;
	if ( s == 0 ) return 0;
	while ( *s == ' ' || *s == '\t' ) s++;
	if ( s[0] == 0 ) return 0;
	if ( ::strcmp(s,"0")==0 || ::strcasecmp(s,"now")==0 || ::strcasecmp(s,"immediate")==0 )
		return 0;
	if ( ::strcasecmp(s,"all")==0 || ::strcasecmp(s,"keep")==0 || ::strcasecmp(s,"inf")==0
	  || ::strcmp(s,"-1")==0 )
		return 2;
	/* 絶対日付 YYYY-MM-DD ? */
	int Y, M, D; char tail = 0;
	if ( ::sscanf(s, "%d-%d-%d%c", &Y, &M, &D, &tail) == 3
	  && Y >= 1970 && M >= 1 && M <= 12 && D >= 1 && D <= 31 ) {
		struct tm tmv; ::memset(&tmv, 0, sizeof tmv);
		tmv.tm_year = Y - 1900; tmv.tm_mon = M - 1; tmv.tm_mday = D; tmv.tm_isdst = -1;
		time_t t = ::mktime(&tmv);
		if ( t != (time_t)-1 ) { *cutoff = (INTEGER64)t; return 1; }
		return 0;
	}
	/* 相対期間 <num>[w|d|h|m](サフィックスなし=日) */
	char *end = 0;
	double v = ::strtod(s, &end);
	if ( end == s || v < 0 ) return 0;          /* 数値でない → 即削除 */
	while ( *end == ' ' ) end++;
	double mult = 86400.0;                       /* 既定=日 */
	if      ( *end=='w' || *end=='W' ) mult = 604800.0;
	else if ( *end=='d' || *end=='D' || *end==0 ) mult = 86400.0;
	else if ( *end=='h' || *end=='H' ) mult = 3600.0;
	else if ( *end=='m' || *end=='M' ) mult = 60.0;
	else return 0;                               /* 不明サフィックス → 即削除 */
	*cutoff = (INTEGER64)( (double)now - v * mult );
	return 1;
}

/* 1.2.5 終了時クリーンアップ。解決後 agent エラーがあれば全体異常終了。その後、この run で使われなかった
 * キャッシュ + 死体(番兵なし)を掃除(usedCaches 登録簿で「使用済み」を残す)。
 * 掃除方針は SRAVA_CACHE_RETAIN(未設定=即削除・既定 / 期間 or 期日=古いものだけ削除 / all=全保持)。 */
TS_STATE(ACT_cgptsPlanner_CLEANUP)
{
	sPtr<pigData> ae = get_agentError();
	/* 未 flush の async export を掘り起こす(正常終了時のみ)。書き出しエラーは promise の result に
	 * 埋もれているので compact して stderr に出す。flush() で既に出したものは drain 時点でリストが
	 * 空なので二重報告はない。
	 * ★ ただし **abort/eval-error 時は drain しない**: 中断時 agent は promise を解決せず撤収する
	 *   (ABORT→FIN は set_result しない)ので、未解決 promise を compact すると **永久 yield して
	 *   CLEANUP がハングする**(SIGINT で agent が全滅したのに srava が止まらない症状の正体)。
	 *   既にエラー終了するのだから async export の結果可視化は不要。 */
	if ( ae == thNULL && !eval_error ) {
		(void) drain_async();        /* async 文(print_async/export_async 含む): 全 body+sync を待ちエラー集積 */
	}
	int async_err = async_error_total();   /* flush()+drain の累積(async に統一) */

	int had_error = ( ae != thNULL ) || eval_error || ( async_err > 0 );

	/* ★ 予約変数 EXIT_CODE の反映 (2026-08-11)。プログラムが `EXIT_CODE = n;` で明示した値を
	 * 終了コードにする。エラー終了時は下の分岐が 1 / filter() が 128+signum を立てるので、
	 * **エラーコードが優先** (成功時の明示指定という位置づけ)。
	 * 範囲外は **警告して 0-255 にクランプ** する。無言で切り詰めるのは、まさにこの修正で潰した
	 * 「結果値が黙って exit に漏れる」不具合と同じ轍なので避ける。 */
	if ( ! had_error && env.is_notNull() ) {
		sPtr<pigData> ecv = env->get_var(thNEW(stdString,("EXIT_CODE")));
		if ( ecv.is_notNull() && ! ecv->is_error() ) {
			INTEGER64 ec = ecv->get_int();
			if ( ec < 0 || ec > 255 ) {
				::fprintf(stderr, "[srava] warning: EXIT_CODE=%lld は範囲外 (0-255) → %d に丸めました\n",
				          (long long)ec, ( ec < 0 ) ? 0 : 255);
				ec = ( ec < 0 ) ? 0 : 255;
			}
			(*exitCodeOut) = (int)ec;
		}
	}
	if ( ae != thNULL ) {
		/* eval_error 済み(EVAL で実エラーを表示済み)なら、ここで撤収トリガの内部マーカ
		 * ("aborted: fatal error")を二重表示しない。SIGINT 等(eval_error 無し)は表示する。 */
		if ( !eval_error )
			show_error_m(ae->get_str());
		show_other_agent_errors();   /* ★ 主エラー以外に agent が出した理由を列挙 */
		if ( (*exitCodeOut) == 0 )
			(*exitCodeOut) = 1;
	} else if ( eval_error ) {
		/* ★ 評価がエラーで終わったが agentError は無い経路 (promise 連鎖でエラーが伝わった
		 * 場合)。**ここが本題**: 連鎖を取ったのが傍観者だと、落ちた本人の理由はここでしか
		 * 出せない (2026-08-26)。 */
		show_other_agent_errors();
	} else if ( async_err > 0 ) {
		/* エラー本文は flush()/drain が既に出力済み。ここでは終了コードだけ立てる。 */
		show_other_agent_errors();   /* ★ drain が拾えなかった agent 由来の理由を足す */
		if ( (*exitCodeOut) == 0 )
			(*exitCodeOut) = 1;
	} else {
		/* デバッグ表示(単体実行テストの assert 点)。全 agent 完了済みなのでキャッシュは完成
		 * (一発読みで足りる)。tree(=最終文)の解決値はキャッシュハンドルとは限らない —
		 * export/export_vox が out_cache=0 (#3406, 2026-07-30 メモ) になり成功/失敗の値を
		 * 返すようになったため、非キャッシュ値も完走マーカーとして出す。 */
		sPtr<pigData> v = tree->compact();
		if ( v->is_cache() ) {
			/* ★ #3443: かつてここで cache の先頭バイトを読んで "nv=.. nf=.." を表示していたが、
			 *   撤去した。頂点数・面数は **三角形メッシュという幾何モデル固有の語彙** であって、
			 *   planner が持ってよい概念ではない (planner はカーネル中立・型中立が設計)。
			 *   実害も出ていた: 読み方が cg の "MESH" 形式決め打ちで、先頭 1 バイトが形式である
			 *   NEFB では 1 バイトずれ、素の nef の箱が nv=2049 と表示されていた。
             *   → 数が要るときは **モジュールの op** に聞く: nverts(m) / nfaces(m)。 */
			::printf("[srava] result cache=%s\n", v->get_str()->get_str());
		} else if ( ! v->is_error() ) {
			::printf("[srava] result value=%s\n", v->serialize()->get_str());
		}
	}
	/* usedCaches(ptsApplication 登録簿)に載らない完了キャッシュ + 死体を削除。
	 * 掃除先は **現在の** CACHE_DIR(プログラムが set_var で変えていれば実際に書いた dir)を使う。
	 * env が引けなければ INI 時の既定(cacheDir)にフォールバック。 */
	/* ★ エラー終了時はキャッシュ掃除をしない。エラーで評価が途中終了すると usedCaches 登録簿が
	 * 不完全(参照されるはずの完了キャッシュが「未使用」に見える)になり得る。これを掃除すると
	 * 健全なキャッシュまで巻き添えで消えるので、異常時は一切掃除せず温存する(次回再利用)。 */
	int n = usedCaches.length();
	if ( had_error ) {
		::fprintf(stderr, "[srava] exit cleanup: skipped (error: caches preserved)\n");
	} else {
		sPtr<pigData> cdv = env.is_notNull() ? env->get_var(thNEW(stdString,("CACHE_DIR"))) : sPtr<pigData>();
		sPtr<stdString> sweepDir = ( cdv != thNULL && !cdv->is_error() ) ? cdv->get_str() : cacheDir;
		const INTEGER64 *used = ( n > 0 ) ? &usedCaches[0] : (const INTEGER64*)0;
		/* 掃除方針 CACHE_RETAIN: CACHE_DIR と同様、プログラムが set_var で書き換えた env 上の値を優先し、
		 * 無ければ env(SRAVA_CACHE_RETAIN)。未設定=即削除(既定)/ 期間 or 期日=古い完了のみ削除 / all=全保持。
		 * ※ is_notNull() は「有効=true」の反転命名。 */
		const char *retainEnv = ::getenv("SRAVA_CACHE_RETAIN");   /* fallback */
		sPtr<pigData> crv = env.is_notNull() ? env->get_var(thNEW(stdString,("CACHE_RETAIN"))) : sPtr<pigData>();
		if ( crv != thNULL && !crv->is_error() ) {
			sPtr<stdString> crs = crv->get_str();
			if ( crs.is_notNull() && crs->get_str()[0] != 0 )   /* 有効 かつ 非空 → 採用 */
				retainEnv = crs->get_str();
		}
		INTEGER64 cutoff = 0;
		int retainMode = parse_cache_retain(retainEnv, ::time((time_t*)0), &cutoff);
		int swept = pigCacheManager::exit_sweep(sweepDir->get_str(), used, n, retainMode, cutoff);
		if ( retainMode == 0 )
			::fprintf(stderr, "[srava] exit cleanup: %d cache(s) removed (%d used kept)\n", swept, n);
		else if ( retainMode == 2 )
			::fprintf(stderr, "[srava] exit cleanup: %d removed, completed caches all kept "
			          "(retain=%s; %d used touched)\n", swept, retainEnv, n);
		else
			::fprintf(stderr, "[srava] exit cleanup: %d old cache(s) removed, recent kept "
			          "(retain=%s; %d used touched)\n", swept, retainEnv, n);
	}
	/* キャッシュ HIT/MISS サマリ: hit=既存キャッシュ再利用 / miss=agent 起動して計算。
	 * 2 度目の実行で全部 hit なら再利用が効いている(計算は走っていない)。 */
	::fprintf(stderr, "[srava] cache: %d hit(s), %d miss(es)\n", cache_hits(), cache_misses());
	/* ⚠ 2026-08-21: かつてここは「fork 上限により自動調整」と書いていたが、**もう嘘になった**。
	 * §13.7 のランプが入り、セマフォの limit は「緩やかな立ち上がり」でも動くようになったため。
	 * 表示は**事実だけ**にする (何が原因かはここでは分からない)。 */
	/* ★ 2026-08-30: 上限の口が SRAVA_LOAD_CPU 一本になったので文言を合わせた。
	 * 「初期上限」= ランプ開始値 (SRAVA_LOAD_RAMP_START)。天井そのものではない。 */
	/* ★ 2026-08-30: 「実効」を**ピーク同時数**に変えた。以前は gate_cap_dyn() =
	 * **終了時点のセマフォ limit** を「実効」と呼んでおり、走行中の同時数と誤読された
	 * (CGALP が #3456 の切り分けで踏んだ)。終了時の上限も情報として残すが、
	 * 先に出すのは「実際に何本同時に走ったか」。 */
	::fprintf(stderr, "[srava] worker gate: ピーク同時 %d (終了時の上限 %d・初期 %d・"
	                  "上限は SRAVA_LOAD_CPU=%% または SRAVA_LOAD_CPU=0 + SRAVA_LOAD_AGENT=個数)\n",
	          gate_peak(), gate_cap_dyn(), gate_cap());
	/* ★ #3419 (2026-08-23): 走行中の **ピーク実使用** (mallinfo2 uordblks) と **ピーク RSS**。
	 * ⚠ ピーク RSS は allocator の未返却・arena 分散・断片化を含むので、「同時にどれだけ生きていたか」
	 *   の代役としては水増しされる。uordblks は allocator を経由しない実使用そのもの。
	 * ★ 2 つの差がそのまま allocator の水増し (§15.6 の定量化)。
	 * 標本はゲートの入退場ごと (実効上限が小さいので十分に密)。入場順序の実験の主指標の 1 つ。
	 * ⚠⚠ **planner のプロセスだけ**を見ている (mallinfo2 は自プロセス・/proc/self/statm)。
	 *   agent プロセスは 1 バイトも含まれない。**process 実行が主体の構成ではほぼ空を測る**ので、
	 *   「実使用が小さい = メモリを使っていない」と読むと完全に誤る (実体は agent 側にある)。
	 *   → その場合の主指標は **外部サンプラの合計 RSS (srava + srava_agent)**。
	 * ⚠ さらに **uordblks は mmap 済みチャンクを数えない** (それらは hblkhd)。
	 *   MALLOC_MMAP_THRESHOLD_ / MALLOC_TRIM_THRESHOLD_ を設定すると glibc の動的 mmap 閾値
	 *   調整が止まり、大きい確保が mmap へ回って **実使用が実態より桁違いに小さく出る**。
	 *   その条件下で実使用を読むなら uordblks + hblkhd で見ること。 */
	/* ★ #3419 §16.14: 枠を握ったまま入力を待っていた延べ時間。
	 * 「1 引数完了 + 残り begin」でゲートを取る設計 (SRAVA_GATE_WHEN=first) の代償を直接測る。
	 * ⚠ agent の起動 (fork/thread) と C_OP 送信も含む = 「純粋な待ち」ではなく「握ってから使えるまで」。 */
	{
		long long isum = 0, imax = 0; int n = 0, nw = 0;
		gate_idle_stats(&isum, &imax, &n, &nw);
		if ( n > 0 )
			::fprintf(stderr, "[srava] gate idle: 延べ %lld ms (報告 %d agent・うち待った %d・最大 %lld ms)\n",
			          isum, n, nw, imax);
	}
	{
		unsigned long long pl = 0, pr = 0;
		gate_peak_memory(&pl, &pr);
		if ( pr > 0 )
			::fprintf(stderr, "[srava] peak(planner のみ): 実使用 %.0fMB / RSS %.0fMB "
			                  "(差 %.0fMB = allocator 保持)\n",
			          pl/1e6, pr/1e6, (pr > pl ? (pr - pl) : 0)/1e6);
	}
	/* 全結果取得 + sweep 完了 = プランナーの仕事は終わり → 通常 teardown(FIN_START)へ。
	 * 旧: ここで ::_exit(最終保険)していた。理由は ts2System の `sh -c` 経由起動で実 agent が孫
	 * (オーファン)化し pid kill が届かず、中断時に待機中 agent が do_select に残ってハングし得たため
	 * (tinyState #3363)。**#3363 解決済みのため ::_exit は撤去**。起動済み agent は pigfAgent FIN の
	 * wfd close で EOF→self-terminate(グレースフル)、中断経路も #3363 修正で kill が届く。
	 * stdout/stderr はバッファされうるので念のため flush してから通常終了に入る。 */
	::fflush(stdout);
	::fflush(stderr);
	return rDO|FIN_START;
}

TS_STATE(FIN_START)
{
	/* ★ #3556: 撤収の入口で点ける (既定 OFF)。ここから先に残る登録が座り込みの犯人。 */
	cg_fin_trace_arm("FIN");

	/* ★ #3482: 旧 asyncList (async の未解決 front) は撤去した。待ちの台帳は根の見えない try が
	 * 持ち、そこは app (gc 管理下) の持ち物なので、planner の FIN で明示的に空にする必要が無い。
	 * (#3366 の「中断で drain を飛ばしたとき未解決 front を抱えたまま残る」は、台帳が app 側に
	 *  移ったことで planner の終了順とは無関係になった。) */

	/* tsSignal は tsSignalCore の fwIO をイベントループに登録したまま生かす。明示的に destroy しないと
	 * 全状態が終わってもループが終了せずプロセスが残り続ける。3 ハンドラとも閉じる。 */
	if ( sig_int.is_notNull() )  { sig_int->destroy();  sig_int  = thNULL; }
	if ( sig_term.is_notNull() ) { sig_term->destroy(); sig_term = thNULL; }
	if ( sig_hup.is_notNull() )  { sig_hup->destroy();  sig_hup  = thNULL; }
	/* ★ §9: 終了時点で手放す (tree = プログラム木全体・env = 束縛環境が大物)。 */
	parser   = thNULL;
	tree     = thNULL;
	env      = thNULL;
	syncTail = thNULL;
	cacheDir = thNULL;
	srcName  = thNULL;
	return rDO|FIN_ptsApplication_START;
}
