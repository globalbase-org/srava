/* srava — プランナープロセス本体。tsApplication 直下に cgptsPlanner を起動する。
 * 起動時スイープ → パーズ(スタブ手組みツリー) → 評価(agent と往復) → 終了時クリーンアップ →
 * exit_code を返す。CGAL は非リンク(プランナーは CGAL 非依存)。
 * agent は SRAVA_AGENT(pigfSravaAgent)、キャッシュ dir は SRAVA_CACHE_DIR(cgptsPlanner が参照)。
 *
 * 使い方:
 *   srava file.sra     ソースファイルを実行(先頭の #! シェバング行は読み飛ばす)
 *   srava                env SRAVA_SOURCE か既定ソースを実行
 * シェバング例:  #!/usr/bin/env srava   をスクリプト先頭に置き chmod +x すれば ./file.sra で実行可。 */
#include	"ts2/c++/tsApplication.h"
#include	"cg/c++/cgptsPlanner.h"

#include	<signal.h>
#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<errno.h>
#ifndef _WIN32
#include	<sys/resource.h>   /* get/setrlimit(RLIMIT_NOFILE) — 自前で open-files 上限を上げる */
#endif

/* tsApplication の bootstrap コールバックは**素の関数ポインタ(キャプチャ不可)**なので、main の
 * ローカル(ソース/ファイル名/終了コードのアドレス)を bootstrap へ渡す手段がこの file-static しかない。
 * これは**実行体(main)内だけの受け渡し**で、ライブラリ(cgptsPlanner/cgptsLemonParser)側のグローバルは
 * これで全廃(planner ごとに ctor 引数で渡せる=マルチプランナ安全)。 */
static struct {
	const char *src;      /* 実行ソース(ファイル内容。NULL=env SRAVA_SOURCE/既定) */
	const char *fname;    /* ファイル名(エラー ERROR[file,line] 表示用。NULL="<source>") */
	int        *exitCode; /* 終了コードの書き込み先(main のローカル変数のアドレス) */
	int         argvN;    /* ARGV: スクリプト後のコマンドライン引数(個数) */
	char      **argvV;    /* ARGV: 同・文字列配列(argv の途中を指す・プロセス寿命) */
} g_boot;

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

int
main(int argc, char** argv)
{
	::signal(SIGPIPE, SIG_IGN);   /* agent が先に閉じた後の write を EPIPE 化(即死回避) */
	unsigned long nofile = raise_fd_limit();   /* シェルの低い fd 上限(macOS 256)を自前で引き上げる */
	if ( ::getenv("PIG_FD_VERBOSE") != 0 && nofile > 0 )
		::fprintf(stderr, "[pig] open files limit (RLIMIT_NOFILE) = %lu\n", (unsigned long)nofile);

	int exitCode = 0;   /* planner が *g_boot.exitCode 経由で書き込む(グローバルではない・main のローカル) */
	g_boot.src = 0; g_boot.fname = 0; g_boot.exitCode = &exitCode;
	g_boot.argvN = 0; g_boot.argvV = 0;

	/* 第 1 引数があればソースファイルパスとして読み込む(srava file.sra / シェバング経由)。
	 * 第 2 引数以降は srava プログラムの ARGV(コマンドライン引数)として渡す。 */
	if ( argc >= 2 ) {
		char *text = read_source_file(argv[1]);
		if ( text == 0 )
			return 1;
		g_boot.src   = text;
		g_boot.fname = argv[1];   /* エラー表示にファイル名("-"=stdin も含む) */
		g_boot.argvN = ( argc > 2 ) ? argc - 2 : 0;   /* script の後ろの引数 */
		g_boot.argvV = ( argc > 2 ) ? argv + 2 : 0;
	}

	thNEW(tsApplication, (thNULL, [](sPtr<tsApplication> app) {
		thNEW(cgptsPlanner, (app, g_boot.src, g_boot.fname, g_boot.exitCode,
		                     g_boot.argvN, g_boot.argvV));
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
