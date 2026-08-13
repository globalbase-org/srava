#ifndef PIG_OSGLUE_H
#define PIG_OSGLUE_H
/*
 * osglue — OS 依存 API のグルー。
 * pid 取得・存在確認など Linux/macOS/Windows で挙動が異なるもの。
 * 現状は POSIX(Linux/macOS)実装。Windows は TODO。
 * 後で tinyState アキラへ移管し、ライブラリ側で吸収する予定。
 */
#include <stdint.h>

/* ─────────────────────────────────────────────────────────────────────
 * 動的ロード (モジュール .so/.dll) の OS 差 — 2026-08-12。
 *   POSIX(Linux/macOS/Cygwin): dlopen/dlsym/dlclose/dlerror。
 *   Windows native(MinGW):     LoadLibraryEx/GetProcAddress/FreeLibrary/FormatMessage。
 *     MinGW には dlfcn.h が無いのでコンパイルすら通らない → ここで吸収する。
 * ★ モジュールの拡張子も OS で違う。CMake の MODULE ライブラリは
 *     Linux/macOS = .so / MinGW = .dll / Cygwin = .dll
 *   なので、Cygwin は dlopen が使える (POSIX 実装) が拡張子は .dll という組み合わせになる。
 * ───────────────────────────────────────────────────────────────────── */
#if defined(_WIN32) || defined(__CYGWIN__)
#define OSGLUE_MODULE_SUFFIX  ".dll"
#else
#define OSGLUE_MODULE_SUFFIX  ".so"
#endif

/* lazy: 関数解決を呼出時まで遅延 (POSIX の RTLD_LAZY)。Windows に相当概念は無く無視される。
 * 失敗は 0 を返し、理由を errbuf (呼び手提供・errlen バイト) へ書く (不要なら errbuf=0 可)。
 * ★ #3427 ③: 旧「static バッファ + osglue_dlerror()」API を廃止 (プロセス唯一の可変 static で
 *   リエントラントでなかった)。失敗理由はその場で呼び手のバッファへ受け取る。
 * ★ Windows は依存 DLL 欠けを ERROR_MOD_NOT_FOUND(126) としか言わない (どの DLL が欠けたかは
 *   教えてくれない) ので、その場合は「依存 DLL が見つからない」旨のヒントを付けて返す。 */
void *      osglue_dlopen(const char *path, int lazy, char *errbuf, unsigned long errlen);
void *      osglue_dlsym(void *handle, const char *symbol, char *errbuf, unsigned long errlen);
int         osglue_dlclose(void *handle);

/* 実行中のバイナリ自身のパスを取得する。0=成功/-1=失敗。
 * ★ これまで pigModuleLoader は /proc/self/exe を直接読んでいたが、これは **Linux 専用** で、
 *   macOS (procfs 無し) と Windows では失敗する = 「実行体と同じ dir」の探索路が丸ごと効かない。
 *   Linux=/proc/self/exe / macOS=_NSGetExecutablePath / Windows=GetModuleFileNameA。 */
int  osglue_exe_path(char *buf, unsigned long bufsz);

/* $SRAVA_MODULE_PATH 等の「パスリスト」の区切り文字。
 * ★ Windows native は ';'。':' にすると "C:/..." のドライブレターで誤分割する
 *   (PIG_PLUGIN_PATH で実際に踏んだ)。Cygwin は POSIX パスなので ':'。 */
#ifdef _WIN32
#define OSGLUE_PATHLIST_SEP  ';'
#else
#define OSGLUE_PATHLIST_SEP  ':'
#endif

uint32_t osglue_getpid();                 /* 現プロセスの pid */
int      osglue_pid_exists(uint32_t pid); /* 1=alive, 0=dead/gone, -1=unknown(errno 等) */
int      osglue_mkdir_p(const char *path, int mode); /* `mkdir -p` 相当(中間 dir も作る)。0=成功/-1=失敗 */

/* pread(2) 相当: fd の offset 位置から count バイト読む(ファイルポインタ非依存)。
 * 戻り=読めたバイト数(>=0)/-1=エラー。POSIX=::pread。Windows=ReadFile+OVERLAPPED(MinGW に pread 無し)。 */
long     osglue_pread(int fd, void *buf, unsigned long count, long long offset);

/* realpath(3) 相当: path を絶対正規化して resolved(>=PATH_MAX)へ。**存在しなければ 0 を返す**
 * (呼び出し側が候補探索の存在判定に使うため)。POSIX=::realpath。Windows=_fullpath + 存在確認。 */
char *   osglue_realpath(const char *path, char *resolved);

/* setenv(name,value,overwrite=1) 相当。0=成功。POSIX=::setenv / Windows=_putenv_s。 */
int      osglue_setenv(const char *name, const char *value);

/* `rm -rf path` 相当(ディレクトリ/ファイルを再帰削除)。0=常に(存在しなくても可)。
 * dirent/unlink/rmdir で portable 実装(Windows の cmd.exe に rm が無い問題を回避)。 */
int      osglue_rmrf(const char *path);

/* fd の現在のファイルサイズ(バイト)。-1=エラー。別プロセスが**書き込み中**の最新 EOF を返すのが要件。
 * POSIX=::fstat(st_size)。Windows=GetFileSizeEx(CRT fstat は他プロセスの未 flush 書込を stale に返すため)。
 * キャッシュのストリーミング読み(single-writer/multi-reader・書込中 read)で必須。 */
long long osglue_fsize(int fd);

#endif /* PIG_OSGLUE_H */
