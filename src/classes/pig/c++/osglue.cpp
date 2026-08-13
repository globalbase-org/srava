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
#  ifdef __APPLE__
#    include <mach-o/dyld.h>   /* _NSGetExecutablePath (macOS には /proc が無い) */
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
