/*
 * osglue — OS 依存 API のグルー実装(POSIX 版)。
 * tinyState アキラへ移管後は、このファイルをプラットフォーム別に差し替える。
 *
 * 現状 POSIX: Linux / macOS。
 * Windows は TODO(後で tinyState 側でクロスプラットフォーム化)。
 */
#include "pig/c++/osglue.h"

#ifdef _WIN32
#  include <windows.h>
#  include <string.h>   /* strlen / memcpy / memset */
#  include <io.h>       /* _get_osfhandle */
#  include <psapi.h>    /* GetProcessMemoryInfo (#3419) */
#  include <stdlib.h>   /* _fullpath */
#  include <limits.h>   /* PATH_MAX */
#  include <stdio.h>    /* snprintf (dl shim のエラー整形。下の共通 include より前に要る) */
#else
#  include <unistd.h>   /* getpid / pread / unlink / rmdir */
#  include <stdlib.h>   /* realpath */
#  include <signal.h>   /* kill(pid, 0) */
#  include <stdio.h>    /* snprintf (osglue_dlopen/dlsym のエラー整形・#3427 ③) */
#  include <errno.h>
#  include <sys/stat.h> /* mkdir */
#  include <sys/time.h> /* gettimeofday (osglue_now_ms・#3419 §12) */
#  ifdef __APPLE__
#    include <mach-o/dyld.h>   /* _NSGetExecutablePath (macOS には /proc が無い) */
#    include <sys/sysctl.h>    /* hw.logicalcpu / hw.nperflevels (#3419) */
#    include <libproc.h>       /* proc_pidinfo (#3419) */
#    include <sys/qos.h>       /* QoS クラスで絞りを検出 (#3419) */
#    include <pthread.h>       /* pthread_get_qos_class_np */
#    include <mach/mach.h>     /* host_statistics64 */
#  else
#    include <sched.h>         /* sched_getaffinity (#3419) */
#  endif
#  include <string.h>
#endif

/* 実行中バイナリのパス (2026-08-12)。OS ごとに取得手段が違う。 */
int
osglue_exe_path(char *buf, unsigned long bufsz)
{
	if ( buf == 0 || bufsz < 2 )
		return -1;
#if defined(_WIN32)
	DWORD n = ::GetModuleFileNameA(NULL, buf, (DWORD)bufsz);
	if ( n == 0 || n >= bufsz )
		return -1;
	buf[n] = '\0';
	return 0;
#elif defined(__APPLE__)
	uint32_t n = (uint32_t)bufsz;
	if ( _NSGetExecutablePath(buf, &n) != 0 )
		return -1;   /* バッファ不足 (n に必要量が入る) */
	buf[bufsz-1] = '\0';
	return 0;
#else
	ssize_t r = ::readlink("/proc/self/exe", buf, (size_t)bufsz - 1);
	if ( r <= 0 )
		return -1;
	buf[r] = '\0';
	return 0;
#endif
}

/* ─────────────────────────────────────────────────────────────────────
 * 動的ロード (モジュール .so/.dll) — 2026-08-12。
 * ───────────────────────────────────────────────────────────────────── */
#ifdef _WIN32
/* Windows native (MinGW): dlfcn.h が無いので LoadLibraryEx 系で実装する。
 * ★ #3427 ③: 失敗理由は呼び手提供の errbuf へ (旧 static g_dlerr = プロセス唯一の可変 static を廃止)。 */

/* 直近の GetLastError() を人間可読にして errbuf へ。ERROR_MOD_NOT_FOUND(126) は
 * 「依存 DLL のどれかが見つからない」の意味だが **どの DLL かは教えてくれない** ので、
 * 切り分け方をヒントとして添える (srava --modules の failed 節に出る)。 */
static void
dl_set_error(const char *what, const char *path, char *errbuf, unsigned long errlen)
{
	DWORD e = ::GetLastError();
	if ( errbuf == 0 || errlen == 0 )
		return;
	char msg[256];
	msg[0] = '\0';
	::FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
	                 NULL, e, 0, msg, (DWORD)sizeof msg, NULL);
	/* 末尾の CR/LF を落とす */
	for ( size_t i = ::strlen(msg) ; i > 0 && ( msg[i-1] == '\n' || msg[i-1] == '\r' ) ; --i )
		msg[i-1] = '\0';
	if ( e == ERROR_MOD_NOT_FOUND )
		::snprintf(errbuf, (size_t)errlen,
		           "%s failed: %s: error %lu (%s) — このモジュール自身か、それが必要とする "
		           "依存 DLL のいずれかが見つかりません (PATH / モジュールと同じ dir を確認)",
		           what, path ? path : "(null)", (unsigned long)e, msg);
	else
		::snprintf(errbuf, (size_t)errlen, "%s failed: %s: error %lu (%s)",
		           what, path ? path : "(null)", (unsigned long)e, msg);
}

void *
osglue_dlopen(const char *path, int lazy, char *errbuf, unsigned long errlen)
{
	(void) lazy;   /* Windows に RTLD_LAZY 相当は無い (常に即時解決) */
	if ( path == 0 || path[0] == '\0' ) {
		if ( errbuf != 0 && errlen > 0 )
			::snprintf(errbuf, (size_t)errlen, "empty module path");
		return 0;
	}
	/* LOAD_WITH_ALTERED_SEARCH_PATH: 依存 DLL を **モジュール自身の dir** からも探す
	 * (絶対パス指定時のみ有効)。カーネル .so の隣に依存 DLL を置く配置を可能にする。 */
	HMODULE h = ::LoadLibraryExA(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
	if ( h == NULL ) {
		dl_set_error("LoadLibraryEx", path, errbuf, errlen);
		return 0;
	}
	return (void*)h;
}

void *
osglue_dlsym(void *handle, const char *symbol, char *errbuf, unsigned long errlen)
{
	if ( handle == 0 || symbol == 0 ) {
		if ( errbuf != 0 && errlen > 0 )
			::snprintf(errbuf, (size_t)errlen, "null handle/symbol");
		return 0;
	}
	FARPROC p = ::GetProcAddress((HMODULE)handle, symbol);
	if ( p == NULL ) {
		dl_set_error("GetProcAddress", symbol, errbuf, errlen);
		return 0;
	}
	return (void*)p;
}

int
osglue_dlclose(void *handle)
{
	if ( handle == 0 )
		return -1;
	return ::FreeLibrary((HMODULE)handle) ? 0 : -1;
}

#else   /* POSIX (Linux / macOS / Cygwin) */
#include <dlfcn.h>

void *
osglue_dlopen(const char *path, int lazy, char *errbuf, unsigned long errlen)
{
	/* RTLD_GLOBAL: 以降ロードする他モジュール/host からこの .so のシンボルを見えるように。 */
	void *h = ::dlopen(path, ( lazy ? RTLD_LAZY : RTLD_NOW ) | RTLD_GLOBAL);
	if ( h == 0 && errbuf != 0 && errlen > 0 ) {
		const char *e = ::dlerror();   /* libc 内部の (スレッド毎) バッファ → 呼び手へ複写 */
		::snprintf(errbuf, (size_t)errlen, "%s", e ? e : "dlopen failed");
	}
	return h;
}

void *
osglue_dlsym(void *handle, const char *symbol, char *errbuf, unsigned long errlen)
{
	(void) ::dlerror();   /* 既存エラーをクリア (dlsym は正当に 0 を返しうるため) */
	void *p = ::dlsym(handle, symbol);
	if ( p == 0 && errbuf != 0 && errlen > 0 ) {
		const char *e = ::dlerror();
		::snprintf(errbuf, (size_t)errlen, "%s",
		           e ? e : "symbol not found");
	}
	return p;
}

int
osglue_dlclose(void *handle)
{
	return ::dlclose(handle);
}
#endif

/* osglue_rmrf 用(POSIX/MinGW 共通)。MinGW も dirent/sys/stat を提供する。 */
#include <dirent.h>    /* DIR / opendir / readdir / closedir */
#include <sys/stat.h>  /* stat / S_ISDIR */
#include <stdio.h>     /* snprintf */
#include <string.h>    /* strcmp */
#ifdef _WIN32
#  include <unistd.h>  /* MinGW: unlink / rmdir */
#endif

uint32_t
osglue_getpid()
{
#ifdef _WIN32
    return (uint32_t)GetCurrentProcessId();
#else
    return (uint32_t)getpid();
#endif
}

int
osglue_mkdir_p(const char *path, int mode)
{
    if ( path == 0 || path[0] == 0 )
        return -1;
#ifdef _WIN32
    /* `mkdir -p` 相当: CreateDirectory を区切りごとに。'/' '\\' 両対応。既存(ERROR_ALREADY_EXISTS)は無視。
     * "C:" のようなドライブ prefix 単独は作らない。mode は Windows では無視。 */
    (void)mode;
    char buf[1024];
    size_t len = ::strlen(path);
    if ( len >= sizeof buf )
        return -1;
    ::memcpy(buf, path, len + 1);
    for ( char *p = buf ; *p ; ++p ) {
        if ( (*p == '/' || *p == '\\') && p != buf ) {
            if ( p == buf + 2 && buf[1] == ':' )   /* "C:\" のドライブ区切りはスキップ */
                continue;
            char sep = *p;
            *p = 0;
            if ( !CreateDirectoryA(buf, NULL) && GetLastError() != ERROR_ALREADY_EXISTS ) {
                *p = sep;
                return -1;
            }
            *p = sep;
        }
    }
    if ( !CreateDirectoryA(buf, NULL) && GetLastError() != ERROR_ALREADY_EXISTS )
        return -1;
    return 0;
#else
    /* `mkdir -p` 相当: 中間ディレクトリを順に作る(::mkdir は親が無いと失敗するため)。
     * 各 prefix で mkdir、既存(EEXIST)は無視。最後まで作れたら 0、途中で EEXIST 以外なら -1。 */
    char buf[1024];
    size_t len = ::strlen(path);
    if ( len >= sizeof buf )
        return -1;
    ::memcpy(buf, path, len + 1);
    while ( len > 1 && buf[len-1] == '/' )   /* 末尾スラッシュ除去 */
        buf[--len] = 0;
    for ( char *p = buf + 1 ; *p ; ++p ) {   /* buf+1: 先頭 '/'(絶対パス)はスキップ */
        if ( *p == '/' ) {
            *p = 0;
            if ( ::mkdir(buf, (mode_t)mode) != 0 && errno != EEXIST )
                return -1;
            *p = '/';
        }
    }
    if ( ::mkdir(buf, (mode_t)mode) != 0 && errno != EEXIST )
        return -1;
    return 0;
#endif
}

int
osglue_pid_exists(uint32_t pid)
{
#ifdef _WIN32
    /* TODO: tinyState アキラへ移管 */
    HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, (DWORD)pid);
    if ( !h ) return 0;
    DWORD code = STILL_ACTIVE;
    GetExitCodeProcess(h, &code);
    CloseHandle(h);
    return (code == STILL_ACTIVE) ? 1 : 0;
#else
    /* POSIX: kill(pid, 0) は存在チェック専用(シグナルを実際には送らない) */
    if ( kill((pid_t)pid, 0) == 0 ) return 1;    /* alive */
    if ( errno == ESRCH )           return 0;    /* no such process */
    return -1;                                   /* EPERM 等(存在するが権限なし → alive 扱いで良い場合 1 を返してもよい) */
#endif
}

/* プロセスの起動時刻。pid の使い回しを見分けるためだけに使う不透明値。
 * ★ Linux: /proc/<pid>/stat の 22 番目 (starttime・起動からの clock tick)。
 *   ⚠ **括弧で囲まれた comm フィールドに空白や ')' が入りうる**ので、フィールドを頭から
 *     数えてはいけない。**最後の ')' を探して**そこから数えるのが定石。
 * ★ 取れなければ 0 を返す = 「分からない」。呼び手は pid だけの判定へ落とす。 */
uint64_t
osglue_pid_starttime(uint32_t pid)
{
#ifdef _WIN32
    HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, (DWORD)pid);
    if ( !h ) return 0;
    FILETIME ct, et, kt, ut;
    uint64_t v = 0;
    if ( GetProcessTimes(h, &ct, &et, &kt, &ut) )
        v = ((uint64_t)ct.dwHighDateTime << 32) | (uint64_t)ct.dwLowDateTime;
    CloseHandle(h);
    return v;
#elif defined(__APPLE__)
    /* ★ macOS: proc_pidinfo(PROC_PIDTBSDINFO) の pbi_start_tvsec/usec。
     *   libproc.h は #3419 で既に include している (sysctl KERN_PROC_PID でも取れる)。
     *   ⚠ **実機未検証** (このツリーは Linux でビルドしている)。 */
    struct proc_bsdinfo bi;
    ::memset(&bi, 0, sizeof bi);
    if ( ::proc_pidinfo((int)pid, PROC_PIDTBSDINFO, 0, &bi, (int)sizeof bi) != (int)sizeof bi )
        return 0;
    return (uint64_t)bi.pbi_start_tvsec * 1000000ull + (uint64_t)bi.pbi_start_tvusec;
#else
    /* Linux: /proc/<pid>/stat の 22 番目 (starttime・起動からの clock tick)。
     * ★ ここを `#elif defined(__linux__)` にして `#else` に「取れないので 0」を置いていたが、
     *   **このファイルは非 Apple POSIX = Linux を既に前提**にしている (/proc/self/exe /
     *   /proc/<pid>/smaps_rollup / sched_getaffinity)。到達しない分岐に静かなフォールバックを
     *   置くと、守りが効いていないことに誰も気づけない → 分岐を作らない (ひさ指摘 2026-08-26)。
     * ⚠ **comm フィールドは括弧の中に空白や ')' が入りうる**ので、フィールドを頭から数えては
     *   いけない。**最後の ')' を探して**そこから数えるのが定石。 */
    char path[64];
    ::snprintf(path, sizeof path, "/proc/%u/stat", (unsigned)pid);
    FILE *f = ::fopen(path, "rb");
    if ( f == 0 ) return 0;
    char buf[4096];
    size_t n = ::fread(buf, 1, sizeof buf - 1, f);
    ::fclose(f);
    if ( n == 0 ) return 0;
    buf[n] = '\0';
    char *rp = ::strrchr(buf, ')');          /* comm の閉じ括弧 (最後の ')' ) */
    if ( rp == 0 ) return 0;
    /* rp の次は " S ..." = 3 番目のフィールド。starttime は 22 番目なので 19 個読み飛ばす。 */
    char *p = rp + 1;
    for ( int i = 0 ; i < 19 ; ++i ) {
        while ( *p == ' ' ) ++p;
        while ( *p && *p != ' ' ) ++p;
        if ( *p == '\0' ) return 0;
    }
    while ( *p == ' ' ) ++p;
    return (uint64_t)::strtoull(p, 0, 10);
#endif
}

int
osglue_pid_alive_as(uint32_t pid, uint64_t start)
{
    int a = osglue_pid_exists(pid);
    if ( a != 1 )
        return a;                       /* 居ない / 不明はそのまま */
    if ( start == 0 )
        return 1;                       /* 起動時刻を記録していない古い形式 → pid だけで判定 */
    uint64_t now = osglue_pid_starttime(pid);
    if ( now == 0 )
        return 1;                       /* こちらで取れない → 従来どおり alive 扱い (安全側) */
    return ( now == start ) ? 1 : 0;    /* ★ 違えば **pid を使い回した別プロセス** */
}

long
osglue_pread(int fd, void *buf, unsigned long count, long long offset)
{
#ifdef _WIN32
    /* MinGW に pread が無い。CRT fd → HANDLE 化し、ReadFile に OVERLAPPED(offset)を渡す。
     * OVERLAPPED でオフセット指定するとその位置から読む(pread 相当)。EOF/部分読みは got を返す。 */
    HANDLE h = (HANDLE)_get_osfhandle(fd);
    if ( h == INVALID_HANDLE_VALUE ) return -1;
    OVERLAPPED ov;
    ::memset(&ov, 0, sizeof ov);
    ov.Offset     = (DWORD)((unsigned long long)offset & 0xFFFFFFFFULL);
    ov.OffsetHigh = (DWORD)(((unsigned long long)offset >> 32) & 0xFFFFFFFFULL);
    DWORD got = 0;
    if ( !ReadFile(h, buf, (DWORD)count, &got, &ov) ) {
        if ( GetLastError() == ERROR_HANDLE_EOF ) return (long)got;  /* 末尾: 読めた分だけ返す */
        return -1;
    }
    return (long)got;
#else
    return (long)::pread(fd, buf, (size_t)count, (off_t)offset);
#endif
}

char *
osglue_realpath(const char *path, char *resolved)
{
#ifdef _WIN32
    /* _fullpath は存在チェックしない。realpath は存在しないと NULL を返すので、存在確認を付ける。 */
    if ( _fullpath(resolved, path, PATH_MAX) == 0 )
        return 0;
    if ( GetFileAttributesA(resolved) == INVALID_FILE_ATTRIBUTES )
        return 0;
    return resolved;
#else
    return ::realpath(path, resolved);
#endif
}

int
osglue_setenv(const char *name, const char *value)
{
#ifdef _WIN32
    return _putenv_s(name, value);   /* 0=成功 */
#else
    return ::setenv(name, value, 1);
#endif
}

/* dirent/unlink/rmdir で再帰削除(POSIX/MinGW 共通)。Windows の cmd.exe に rm が無い問題を回避。 */
int
osglue_rmrf(const char *path)
{
    DIR *d = ::opendir(path);
    if ( d != 0 ) {
        struct dirent *e;
        while ( (e = ::readdir(d)) != 0 ) {
            if ( ::strcmp(e->d_name, ".") == 0 || ::strcmp(e->d_name, "..") == 0 )
                continue;
            char child[1024];
            ::snprintf(child, sizeof child, "%s/%s", path, e->d_name);
            struct stat st;
            if ( ::stat(child, &st) == 0 && S_ISDIR(st.st_mode) )
                osglue_rmrf(child);
            else
                ::unlink(child);
        }
        ::closedir(d);
    }
    ::rmdir(path);
    return 0;
}

long long
osglue_fsize(int fd)
{
#ifdef _WIN32
    /* GetFileSizeEx は他プロセスが書き込み中でも現在の EOF を即返す。CRT fstat は stale なことがある。 */
    HANDLE h = (HANDLE)_get_osfhandle(fd);
    if ( h == INVALID_HANDLE_VALUE ) return -1;
    LARGE_INTEGER sz;
    if ( !GetFileSizeEx(h, &sz) ) return -1;
    return (long long)sz.QuadPart;
#else
    struct stat st;
    if ( ::fstat(fd, &st) != 0 ) return -1;
    return (long long)st.st_size;
#endif
}


/* ═══════════════════════════════════════════════════════════════════════
 * 負荷コントロールの計測 (#3419)。仕様と根拠は osglue.h のコメントを参照。
 * ★ 可変 static は持たない (#3427 の方針)。
 * ═══════════════════════════════════════════════════════════════════════ */

#if defined(__linux__)
/* /proc の "Key:  <数値> kB" 形式の行から値を拾う。見つからなければ -1。 */
static long long
proc_kv_kb(const char *path, const char *key)
{
	FILE *f = ::fopen(path, "r");
	if ( f == 0 ) return -1;
	char line[256];
	size_t klen = ::strlen(key);
	long long val = -1;
	while ( ::fgets(line, sizeof line, f) != 0 ) {
		if ( ::strncmp(line, key, klen) != 0 ) continue;
		const char *p = line + klen;
		while ( *p == ':' || *p == ' ' || *p == '\t' ) p++;
		val = ::atoll(p);
		break;
	}
	::fclose(f);
	return val;
}
/* 裸の数値が 1 行だけ入ったファイル (cgroup v1 の cpu.cfs_*) を読む。-1=読めない。 */
static long long
read_ll(const char *path)
{
	FILE *f = ::fopen(path, "r");
	if ( f == 0 ) return -1;
	long long v = -1;
	if ( ::fscanf(f, "%lld", &v) != 1 ) v = -1;
	::fclose(f);
	return v;
}
#endif

/* ★ #3419: -1 = 未確定 (環境変数を見る) / 0 = rss / 1 = pss。**起動時に一度だけ**書く。
 * 書き手は ptsLoadControl の latch 1 箇所きり (最初の agent 入場時) で、以降は読み専用。 */
static int osglue_mem_metric_pss = -1;

void
osglue_set_mem_metric(const char *m)
{
	osglue_mem_metric_pss = ( m != 0 && ::strcmp(m, "pss") == 0 );
}

int
osglue_proc_memory(uint32_t pid, unsigned long long *bytes_out)
{
	if ( bytes_out == 0 ) return -1;
#if defined(__linux__)
	char path[64];
	/* ★ 既定は VmRSS (#3419・ひさ判断 2026-08-24)。SRAVA_LOAD_MEM_METRIC=pss で Pss へ切替。
	 *   ⚠ Pss を既定にしていた前提「agent は fork の子なので CoW でページを共有する」は
	 *     **成立していない** — agent は execvp の子で、共有は共有ライブラリぶんだけ。
	 *     しかも agent 1 個の見積りは差分 (C_MEM − 基準線) で取るのでその分は相殺され、
	 *     metric の違いが効くのは C_MEM を V_MEM と直接比べる 1 箇所だけ (必ず安全側)。
	 *   ★ Pss は smaps_rollup を読むので resident ページ数に比例したコストがかかり、
	 *     mesh を planner 自身が抱える in-proc 実行でだけ計装が測定対象を乱していた。 */
	/* ★ #3419: metric は起動時に一度だけ確定する (osglue_set_mem_metric)。未確定なら env。
	 *   設定表を解いた結果を押し込む形にしてあるので、ここでは表を見ない (層を跨がせない)。 */
	int want_pss = osglue_mem_metric_pss;
	if ( want_pss < 0 ) {
		const char *m = ::getenv("SRAVA_LOAD_MEM_METRIC");
		want_pss = ( m != 0 && ::strcmp(m, "pss") == 0 );
	}
	if ( want_pss ) {
		::snprintf(path, sizeof path, "/proc/%lu/smaps_rollup", (unsigned long)pid);
		long long kb = proc_kv_kb(path, "Pss");
		if ( kb >= 0 ) { *bytes_out = (unsigned long long)kb * 1024ULL; return 0; }
		/* smaps_rollup が無い (kernel < 4.14) → VmRSS へ縮退 */
	}
	::snprintf(path, sizeof path, "/proc/%lu/status", (unsigned long)pid);
	long long kb = proc_kv_kb(path, "VmRSS");
	if ( kb < 0 ) return -1;
	*bytes_out = (unsigned long long)kb * 1024ULL;
	return 0;
#elif defined(__APPLE__)
	struct proc_taskinfo ti;
	int n = ::proc_pidinfo((int)pid, PROC_PIDTASKINFO, 0, &ti, sizeof ti);
	if ( n < (int)sizeof ti ) return -1;
	*bytes_out = (unsigned long long)ti.pti_resident_size;   /* ★按分できない (osglue.h 参照) */
	return 0;
#elif defined(_WIN32)
	HANDLE h = ::OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, (DWORD)pid);
	if ( h == NULL ) return -1;
	PROCESS_MEMORY_COUNTERS_EX pmc;
	::memset(&pmc, 0, sizeof pmc);
	pmc.cb = sizeof pmc;
	BOOL ok = ::GetProcessMemoryInfo(h, (PROCESS_MEMORY_COUNTERS *)&pmc, sizeof pmc);
	::CloseHandle(h);
	if ( ! ok ) return -1;
	*bytes_out = (unsigned long long)pmc.PrivateUsage;
	return 0;
#else
	(void)pid; return -1;
#endif
}

/* ★ #3419 §12: 単調に増える現在時刻 (ミリ秒)。差分だけが意味を持つ。 */
int
osglue_env_int(const char *name, int def)
{
	const char *e = ::getenv(name);
	if ( e == 0 || e[0] == 0 ) return def;
	return ::atoi(e);
}

long long
osglue_now_ms(void)
{
#if defined(_WIN32)
	return (long long)::GetTickCount64();
#else
	struct timeval tv;
	::gettimeofday(&tv, 0);
	return (long long)tv.tv_sec * 1000LL + (long long)(tv.tv_usec / 1000);
#endif
}

/* ★ #3419 §12 (T6-a): pid の消費 CPU 時間 (マイクロ秒・累積)。仕様は osglue.h を参照。 */
int
osglue_proc_cputime(uint32_t pid, unsigned long long *usec_out)
{
	if ( usec_out == 0 ) return -1;
#if defined(__linux__)
	char path[64];
	::snprintf(path, sizeof path, "/proc/%lu/stat", (unsigned long)pid);
	FILE *f = ::fopen(path, "r");
	if ( f == 0 ) return -1;
	char buf[4096];
	size_t n = ::fread(buf, 1, sizeof buf - 1, f);
	::fclose(f);
	if ( n == 0 ) return -1;
	buf[n] = 0;
	/* ⚠ comm は "(my prog) (x)" のように空白も括弧も含みうる。**最後の ')' から**読む。 */
	char *p = ::strrchr(buf, ')');
	if ( p == 0 ) return -1;
	p++;
	/* ここから先はトークン列。index 0 = state(3 番目のフィールド) なので
	 * utime(14) は index 11・stime(15) は index 12。 */
	unsigned long long ut = 0, st = 0;
	int idx = 0;
	for ( char *tok = ::strtok(p, " \t\n") ; tok != 0 ; tok = ::strtok(0, " \t\n"), idx++ ) {
		if ( idx == 11 ) ut = ::strtoull(tok, 0, 10);
		else if ( idx == 12 ) { st = ::strtoull(tok, 0, 10); break; }
	}
	long hz = ::sysconf(_SC_CLK_TCK);
	if ( hz <= 0 ) hz = 100;
	*usec_out = (ut + st) * 1000000ULL / (unsigned long long)hz;
	return 0;
#elif defined(__APPLE__)
	struct proc_taskinfo ti;
	int n = ::proc_pidinfo((int)pid, PROC_PIDTASKINFO, 0, &ti, sizeof ti);
	if ( n < (int)sizeof ti ) return -1;
	/* pti_total_user / pti_total_system はナノ秒 (⚠ 実機で要確認・osglue.h 参照)。 */
	*usec_out = (unsigned long long)(ti.pti_total_user + ti.pti_total_system) / 1000ULL;
	return 0;
#elif defined(_WIN32)
	HANDLE h = ::OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, (DWORD)pid);
	if ( h == NULL ) h = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pid);
	if ( h == NULL ) return -1;
	FILETIME cre, ex, ker, usr;
	BOOL ok = ::GetProcessTimes(h, &cre, &ex, &ker, &usr);
	::CloseHandle(h);
	if ( ! ok ) return -1;
	ULARGE_INTEGER k, u;
	k.LowPart = ker.dwLowDateTime; k.HighPart = ker.dwHighDateTime;
	u.LowPart = usr.dwLowDateTime; u.HighPart = usr.dwHighDateTime;
	*usec_out = (unsigned long long)(k.QuadPart + u.QuadPart) / 10ULL;   /* 100ns → us */
	return 0;
#else
	(void)pid; return -1;
#endif
}

int
osglue_system_memory(unsigned long long *total_out, unsigned long long *avail_out)
{
#if defined(__linux__)
	long long t = proc_kv_kb("/proc/meminfo", "MemTotal");
	long long a = proc_kv_kb("/proc/meminfo", "MemAvailable");
	if ( t < 0 ) return -1;
	if ( total_out != 0 ) *total_out = (unsigned long long)t * 1024ULL;
	if ( avail_out != 0 ) *avail_out = ( a >= 0 ) ? (unsigned long long)a * 1024ULL : 0ULL;
	return 0;
#elif defined(__APPLE__)
	if ( total_out != 0 ) {
		uint64_t mem = 0; size_t sz = sizeof mem;
		if ( ::sysctlbyname("hw.memsize", &mem, &sz, 0, 0) != 0 ) return -1;
		*total_out = (unsigned long long)mem;
	}
	if ( avail_out != 0 ) {
		vm_size_t page = 0;
		vm_statistics64_data_t vm;
		mach_msg_type_number_t cnt = HOST_VM_INFO64_COUNT;
		*avail_out = 0;
		if ( ::host_page_size(mach_host_self(), &page) == KERN_SUCCESS &&
		     ::host_statistics64(mach_host_self(), HOST_VM_INFO64,
		                         (host_info64_t)&vm, &cnt) == KERN_SUCCESS )
			*avail_out = (unsigned long long)(vm.free_count + vm.inactive_count) * (unsigned long long)page;
	}
	return 0;
#elif defined(_WIN32)
	MEMORYSTATUSEX ms;
	ms.dwLength = sizeof ms;
	if ( ! ::GlobalMemoryStatusEx(&ms) ) return -1;
	if ( total_out != 0 ) *total_out = (unsigned long long)ms.ullTotalPhys;
	if ( avail_out != 0 ) *avail_out = (unsigned long long)ms.ullAvailPhys;
	return 0;
#else
	(void)total_out; (void)avail_out; return -1;
#endif
}

unsigned
osglue_usable_cpus(void)
{
#if defined(__linux__)
	unsigned n = 0;
	cpu_set_t set;
	CPU_ZERO(&set);
	if ( ::sched_getaffinity(0, sizeof set, &set) == 0 ) n = (unsigned)CPU_COUNT(&set);
	if ( n == 0 ) {
		long c = ::sysconf(_SC_NPROCESSORS_ONLN);
		n = ( c > 0 ) ? (unsigned)c : 0;
	}
	/* cgroup の CPU quota があれば小さい方を採る (コンテナ / systemd の CPUQuota=)。 */
	{
		FILE *f = ::fopen("/sys/fs/cgroup/cpu.max", "r");     /* v2: "<quota|max> <period>" */
		if ( f != 0 ) {
			char q[32] = {0}; long long period = 0;
			if ( ::fscanf(f, "%31s %lld", q, &period) == 2 &&
			     ::strcmp(q, "max") != 0 && period > 0 ) {
				long long quota = ::atoll(q);
				if ( quota > 0 ) {
					unsigned lim = (unsigned)((quota + period - 1) / period);   /* 切り上げ */
					if ( lim < 1 ) lim = 1;
					if ( n == 0 || lim < n ) n = lim;
				}
			}
			::fclose(f);
		} else {                                              /* v1: 裸の数値が 1 行 */
			long long quota  = read_ll("/sys/fs/cgroup/cpu/cpu.cfs_quota_us");
			long long period = read_ll("/sys/fs/cgroup/cpu/cpu.cfs_period_us");
			if ( quota > 0 && period > 0 ) {
				unsigned lim = (unsigned)((quota + period - 1) / period);
				if ( lim < 1 ) lim = 1;
				if ( n == 0 || lim < n ) n = lim;
			}
		}
	}
	return n;
#elif defined(__APPLE__)
	/* ★ affinity API は無いが QoS クラスで絞りを検出できる。BACKGROUND なら E コアに
	 *   閉じ込められているので E コア数を返す (実測で裏づけ・設計 §4.6.1c)。 */
	qos_class_t q; int rel;
	if ( ::pthread_get_qos_class_np(::pthread_self(), &q, &rel) == 0 &&
	     q == QOS_CLASS_BACKGROUND ) {
		long nlev = 0; size_t sz = sizeof nlev;
		if ( ::sysctlbyname("hw.nperflevels", &nlev, &sz, 0, 0) == 0 && nlev >= 2 ) {
			/* ★ レベル数を固定しない: 最後の (=最も効率寄りの) レベルを採る。 */
			char key[64];
			::snprintf(key, sizeof key, "hw.perflevel%ld.logicalcpu", nlev - 1);
			long e = 0; sz = sizeof e;
			if ( ::sysctlbyname(key, &e, &sz, 0, 0) == 0 && e > 0 ) return (unsigned)e;
		}
	}
	{
		long c = 0; size_t sz = sizeof c;
		if ( ::sysctlbyname("hw.logicalcpu", &c, &sz, 0, 0) == 0 && c > 0 ) return (unsigned)c;
	}
	return 0;
#elif defined(_WIN32)
	DWORD_PTR pmask = 0, smask = 0;
	if ( ::GetProcessAffinityMask(::GetCurrentProcess(), &pmask, &smask) && pmask != 0 ) {
		unsigned c = 0;
		for ( DWORD_PTR m = pmask ; m ; m >>= 1 ) c += (unsigned)(m & 1);
		if ( c > 0 ) return c;
	}
	{
		DWORD c = ::GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
		return ( c > 0 ) ? (unsigned)c : 0;
	}
#else
	return 0;
#endif
}
