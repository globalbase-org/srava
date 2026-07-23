#ifndef PIG_OSGLUE_H
#define PIG_OSGLUE_H
/*
 * osglue — OS 依存 API のグルー。
 * pid 取得・存在確認など Linux/macOS/Windows で挙動が異なるもの。
 * 現状は POSIX(Linux/macOS)実装。Windows は TODO。
 * 後で tinyState アキラへ移管し、ライブラリ側で吸収する予定。
 */
#include <stdint.h>

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
