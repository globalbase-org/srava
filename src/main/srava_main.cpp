/* srava — プランナープロセス本体。tsApplication 直下に cgptsPlanner を起動する。
 * 起動時スイープ → パーズ(スタブ手組みツリー) → 評価(agent と往復) → 終了時クリーンアップ →
 * exit_code を返す。CGAL は非リンク(プランナーは CGAL 非依存)。
 * agent は SRAVA_AGENT(pigfModuleAgent)、キャッシュ dir は SRAVA_CACHE_DIR(cgptsPlanner が参照)。
 *
 * 使い方:
 *   srava file.sra     ソースファイルを実行(先頭の #! シェバング行は読み飛ばす)
 *   srava                env SRAVA_SOURCE か既定ソースを実行
 * シェバング例:  #!/usr/bin/env srava   をスクリプト先頭に置き chmod +x すれば ./file.sra で実行可。 */
#include	"ts2/c++/tsApplication.h"
#include	"cg/c++/cgptsPlanner.h"
#include	"pig/c++/pigModule.h"           /* --modules: 記述子 (priority/exec_default/n_ops) */
#include	"pig/c++/pigModuleRegistry.h"   /* --modules: 診断用ローカルレジストリ (#3427 ③) */

#include	<string>
#include	<vector>
#include	<signal.h>
#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<errno.h>
#ifndef _WIN32
#include	<sys/resource.h>   /* get/setrlimit(RLIMIT_NOFILE) — 自前で open-files 上限を上げる */
#endif

/* ★ #3427 ③: 旧 file-static g_boot は撤去。tsApplication の bootstrap は
 * TS_APPLICATION_FUNC = std::function (キャプチャ可) なので、main のローカルは
 * ラムダのキャプチャで渡す (thNEW(tsApplication,...) は app 完走までブロックする =
 * 参照キャプチャのローカルは生存)。旧コメントの「素の関数ポインタ(キャプチャ不可)」は誤認だった。 */

/* path のファイル全体を malloc 済み NUL 終端バッファに読み込む。失敗で 0。
 * "-" は標準入力。プロセス終了まで使うので解放しない(leak は意図的)。 */
static char *
read_source_file(const char *path)
{
	FILE *f = ( ::strcmp(path, "-") == 0 ) ? stdin : ::fopen(path, "rb");
	if ( f == 0 ) {
		::fprintf(stderr, "srava: cannot open '%s': %s\n", path, ::strerror(errno));
		return 0;
	}
	size_t cap = 4096, len = 0;
	char *buf = (char*)::malloc(cap);
	for ( ;; ) {
		if ( len + 4096 + 1 > cap ) { cap *= 2; buf = (char*)::realloc(buf, cap); }
		size_t n = ::fread(buf + len, 1, 4096, f);
		len += n;
		if ( n < 4096 ) break;
	}
	buf[len] = 0;
	if ( f != stdin )
		::fclose(f);
	return buf;
}

/* 自分の open-files(NOFILE)ソフト上限を引き上げる。多数 agent との pipe で fd を使うため、
 * シェルの低い既定(macOS は 256)に縛られないよう起動時に自前で上げる(DB/サーバ常套手段)。
 * 目標は PIG_MAX_FILES(既定 16384)。macOS は hard=unlimited(RLIM_INFINITY)だと setrlimit が
 * 拒否することがあるので、その場合は max も有限値(=目標)にして設定する(hard を下げる方向は許可)。
 * カーネル上限(kern.maxfilesperproc)超過等で失敗したら段階的に下げて再試行。返り=確定した soft 上限。 */
#ifdef _WIN32
/* Windows(MinGW)には RLIMIT_NOFILE 概念が無い(CRT の _setmaxstdio は stdio ストリーム数で別物)。
 * fd 上限の引き上げは不要 → no-op。 */
static unsigned long
raise_fd_limit(void)
{
	return 0;
}
#else
static unsigned long
raise_fd_limit(void)
{
	struct rlimit rl;
	if ( ::getrlimit(RLIMIT_NOFILE, &rl) != 0 ) return 0;
	rlim_t target = 16384;
	const char *e = ::getenv("PIG_MAX_FILES");
	if ( e != 0 && ::atoi(e) > 0 ) target = (rlim_t)::atoi(e);
	if ( rl.rlim_cur >= target ) return (unsigned long)rl.rlim_cur;   /* 既に十分 */
	struct rlimit nl;
	for ( rlim_t t = target ; t >= 512 ; t = ( t > 2048 ? t/2 : t-512 ) ) {
		if ( t <= rl.rlim_cur ) break;
		nl.rlim_cur = t;
		nl.rlim_max = ( rl.rlim_max != RLIM_INFINITY && rl.rlim_max >= t ) ? rl.rlim_max : t;
		if ( ::setrlimit(RLIMIT_NOFILE, &nl) == 0 )
			return (unsigned long)t;
	}
	return (unsigned long)rl.rlim_cur;   /* 上げられず(現状維持) */
}
#endif

/* `srava --modules` の出力。探索路 → ロード済み → 失敗、の 3 節。
 * 「どこを見たか」を (存在しない dir も含めて) 出すのが要点で、ユーザが探索路を誤解している
 * ケースがこれで一発で分かる。 */
static void
print_module_report(const sPtr<pigModuleRegistry> &reg)
{
	const std::vector<pigModuleSearchDir> &dirs = reg->search_dirs();
	::printf("search path (走査順・後にロードしたものが優先):\n");
	for ( size_t i = 0 ; i < dirs.size() ; ++i )
		::printf("  %-18s %-56s %s\n", dirs[i].origin, dirs[i].dir.c_str(),
		         dirs[i].exists ? "" : "(無し)");

	::printf("\nloaded:\n");
	::printf("  %-16s %5s %-8s %5s  %s\n", "name", "prio", "exec", "ops", "path");
	const std::vector<pigModuleLoadEvent> &log = reg->load_log();
	int nload = 0;
	for ( int id = 0 ; id < reg->count() ; ++id ) {
		const srava_module_descriptor *d = reg->descriptor(id);
		if ( d == 0 )
			continue;   /* "delayed"(id 0) 等の記述子を持たない予約枠 */
		/* 同名が複数の dir から読まれた場合、**後にロードした方が有効** (docs §1.3 の後勝ち)。
		 * 有効なパス = 最後の一致。それ以外は下の shadowed 節に出す。 */
		const char *path = "(不明)";
		int ndup = 0;
		for ( size_t i = 0 ; i < log.size() ; ++i )
			if ( log[i].ok && log[i].name == d->name ) { path = log[i].path.c_str(); ++ndup; }
		const char *ex = ( d->exec_default & EXEC_THREAD ) ? "thread"
		               : ( d->exec_default & EXEC_PROCESS ) ? "process" : "-";
		::printf("  %-16s %5d %-8s %5d  %s%s\n",
		         d->name ? d->name : "(null)", d->priority, ex, d->n_ops, path,
		         ( ndup > 1 ) ? "   ★他を上書き" : "");
		++nload;
	}
	if ( nload == 0 )
		::printf("  (なし — モジュールが 1 つもロードされていません)\n");

	/* 同名の勝者が後の探索路に在ったため **読まなかった** 候補。ビルドツリーで作った .so が
	 * install 済みに負けている(逆も)、という取り違えがここで見える。
	 * ★ 2026-08-13: 以前は「読んだうえで上書き」だったが、同名 2 つを dlopen すると静的自己登録が
	 *   混ざる (記述子=後勝ち / 実行体・codec=先勝ち → 新しい ops 表で古いコードを実行) ため、
	 *   勝者だけを dlopen する方式に変えた。ここは「読まれなかった候補」の一覧。 */
	int nshadow = 0;
	for ( size_t i = 0 ; i < log.size() ; ++i ) {
		if ( ! log[i].shadowed ) continue;
		if ( nshadow++ == 0 )
			::printf("\nshadowed (同名の勝者が在るため読み込まなかったもの):\n");
		::printf("  %-52s → %s\n", log[i].path.c_str(), log[i].err.c_str());
	}

	int nfail = 0, nskip = 0;
	for ( size_t i = 0 ; i < log.size() ; ++i ) {
		if ( log[i].ok || log[i].shadowed ) continue;   /* shadowed は「読まなかった」= 失敗ではない */
		if ( log[i].not_a_module ) ++nskip; else ++nfail;
	}
	if ( nfail > 0 ) {
		::printf("\nfailed (モジュールなのに読めなかったもの):\n");
		for ( size_t i = 0 ; i < log.size() ; ++i ) {
			if ( log[i].ok || log[i].not_a_module || log[i].shadowed )
				continue;
			::printf("  %s\n      %s\n", log[i].path.c_str(), log[i].err.c_str());
		}
	}
	if ( nskip > 0 )
		::printf("\n(モジュールでない .so を %d 個スキップ)\n", nskip);
}

int
main(int argc, char** argv)
{
	::signal(SIGPIPE, SIG_IGN);   /* agent が先に閉じた後の write を EPIPE 化(即死回避) */
	unsigned long nofile = raise_fd_limit();   /* シェルの低い fd 上限(macOS 256)を自前で引き上げる */
	if ( ::getenv("PIG_FD_VERBOSE") != 0 && nofile > 0 )
		::fprintf(stderr, "[pig] open files limit (RLIMIT_NOFILE) = %lu\n", (unsigned long)nofile);

	/* ★ .so 化 Phase 3c: カーネル .so を探索路 (docs §1.3) からロードする。これで planner は
	 *   カーネルを静的リンクせず、記述子 (decide_out_module/default_module) と in-proc 実行体
	 *   (thread 可能カーネルの make_agent) を .so から得る。RTLD_LAZY なので EXEC_PROCESS のみの
	 *   cgal (293MB) を読んでも関数解決コストは発生しない (メタ取得のみ)。 */
	/* ★ #3427 ③: モジュールのロードは **ptsApplication::INI_ptsObject_START** が行う
	 *   (planner が基底 ctor に PIG_MODLOAD_SEARCH を渡す)。--modules だけは app を起こさず
	 *   ローカルレジストリで答える。 */

	/* ★ 2026-08-12: `srava --modules` = モジュール診断ダンプ。探索路・ロード成功/失敗を出して終了する。
	 *   モジュールが効かないときの切り分けは ①ファイルが見つからない ②見つけたが load 失敗
	 *   (依存ライブラリ欠け等) ③load できたが routing 候補から外れている、の 3 つ。①②は load_log()、
	 *   ③は registry の priority/ops で読む。Windows は LoadLibrary が依存 DLL 欠けを
	 *   ERROR_MOD_NOT_FOUND(126) としか言わないため、この出力の価値が Linux より高い。
	 *   ※ argv[1] は本来スクリプト名。'--' 始まりは従来どうせ "ファイルが開けない" で落ちていたので
	 *      ここで拾っても後方互換は壊れない。 */
	if ( argc >= 2 && ::strcmp(argv[1], "--modules") == 0 ) {
		/* ★ #3427 ③: レジストリは app 所有になったが、--modules 診断は app を起こしたくない。
		 * 診断専用のローカルレジストリを作って探索路を読ませ、記録を印字して捨てる
		 * (実行系と同じ経路 = 実走時と同じ勝者/失敗が見える)。 */
		sPtr<pigModuleRegistry> reg = thNEW(pigModuleRegistry,());
		std::string mlerr;
		(void) reg->load_search_path(&mlerr);
		print_module_report(reg);
		return 0;
	}

	int exitCode = 0;      /* planner が exitCode ポインタ経由で書き込む (main のローカル) */
	const char *src = 0;   /* 実行ソース(ファイル内容。NULL=env SRAVA_SOURCE/既定) */
	const char *fname = 0; /* ファイル名(エラー ERROR[file,line] 表示用。NULL="<source>") */
	int argvN = 0;         /* ARGV: スクリプト後のコマンドライン引数(個数) */
	char **argvV = 0;      /* ARGV: 同・文字列配列(argv の途中を指す・プロセス寿命) */

	/* 第 1 引数があればソースファイルパスとして読み込む(srava file.sra / シェバング経由)。
	 * 第 2 引数以降は srava プログラムの ARGV(コマンドライン引数)として渡す。 */
	if ( argc >= 2 ) {
		char *text = read_source_file(argv[1]);
		if ( text == 0 )
			return 1;
		src   = text;
		fname = argv[1];   /* エラー表示にファイル名("-"=stdin も含む) */
		argvN = ( argc > 2 ) ? argc - 2 : 0;   /* script の後ろの引数 */
		argvV = ( argc > 2 ) ? argv + 2 : 0;
	}

	/* ★ #3427 ②改→③: モジュールのロードは **ptsApplication の INI_ptsObject_START** へ移った
	 * (planner の ctor が PIG_MODLOAD_SEARCH を基底へ渡す)。bootstrap は planner 生成だけ。
	 * ラムダは std::function なのでキャプチャで main のローカルを渡す (file-static g_boot 全廃)。 */
	thNEW(tsApplication, (thNULL, [&, src, fname, argvN, argvV](sPtr<tsApplication> app) {
		thNEW(cgptsPlanner, (app, src, fname, &exitCode, argvN, argvV));
	}));
	/* Cygwin: main の return(=通常 exit() 経路の atexit + C++ 静的デストラクタ)が
	 * プロセス終了コードを 0 に落としてしまう(エラー時に planner が exitCode=1 を書いても
	 * シェルには 0 が返る = srava_async_err 等の失敗の正体)。ここに来る時点で app teardown は
	 * 完了しているので、stdout/stderr を flush してから _exit で終了コードを確実に反映する。
	 * 他環境(Linux/MinGW)は return が正しく反映されるので従来どおり。 */
	::fflush(stdout);
	::fflush(stderr);
#ifdef __CYGWIN__
	::_exit(exitCode);
#endif
	return exitCode;
}
