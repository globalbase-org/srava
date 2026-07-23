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
#else
#  include <unistd.h>   /* getpid / pread / unlink / rmdir */
#  include <stdlib.h>   /* realpath */
#  include <signal.h>   /* kill(pid, 0) */
#  include <errno.h>
#  include <sys/stat.h> /* mkdir */
#  include <string.h>
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
