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
/* ★ プロセスの **起動時刻** (2026-08-26)。pid だけでは同一プロセスを名指せない — OS は pid を
 * 使い回すので、「書きかけのキャッシュの writer」を pid だけで判定すると、一周後に無関係な
 * プロセスを writer と誤認し、**誰も書かないファイルを永久に待つ (沈黙ハング)** ことがある。
 * pid と対にして初めてプロセスの同一性が決まる。
 * 戻り: 不透明な識別値 (同一プロセスなら不変・0 = 取得できなかった)。値の意味は OS 依存で、
 *       **比較にだけ使う** (Linux は起動からの clock tick)。 */
uint64_t osglue_pid_starttime(uint32_t pid);
/* pid と起動時刻の両方が一致するプロセスが生きているか。start=0 は「起動時刻が分からない
 * 古い記録」なので pid だけで判定する (後方互換)。1=alive, 0=別物/居ない, -1=不明。 */
int      osglue_pid_alive_as(uint32_t pid, uint64_t start);
int      osglue_mkdir_p(const char *path, int mode); /* `mkdir -p` 相当(中間 dir も作る)。0=成功/-1=失敗 */

/* pread(2) 相当: fd の offset 位置から count バイト読む(ファイルポインタ非依存)。
 * 戻り=読めたバイト数(>=0)/-1=エラー。POSIX=::pread。Windows=ReadFile+OVERLAPPED(MinGW に pread 無し)。 */
long     osglue_pread(int fd, void *buf, unsigned long count, long long offset);

/* realpath(3) 相当: path を絶対正規化して resolved(>=PATH_MAX)へ。**存在しなければ 0 を返す**
 * (呼び出し側が候補探索の存在判定に使うため)。POSIX=::realpath。Windows=_fullpath + 存在確認。 */
char *   osglue_realpath(const char *path, char *resolved);

/* setenv(name,value,overwrite=1) 相当。0=成功。POSIX=::setenv / Windows=_putenv_s。 */
int      osglue_setenv(const char *name, const char *value);

/* osglue_proc_memory が使う metric を**プロセス全体に一度だけ**決める ("pss" / それ以外=rss)。
 * ★ 設定表 (LOAD_MEM_METRIC) は srava 変数 → 環境変数 → 既定 で解くが、osglue は C レベルで
 *   設定表を見られない。そこで**解決した結果だけ**をここへ押し込む (層を跨がせない)。
 * ⚠ **`osglue_setenv` で渡してはいけない**。osglue_proc_memory はサンプルのたびに別スレッドから
 *   呼ばれるので、`getenv` と `setenv` が並行すると環境配列の張り替えと競合する。
 * ★ 未設定のままなら従来どおり環境変数 SRAVA_LOAD_MEM_METRIC を見る (単体テスト・agent 側)。 */
void     osglue_set_mem_metric(const char *m);

/* `rm -rf path` 相当(ディレクトリ/ファイルを再帰削除)。0=常に(存在しなくても可)。
 * dirent/unlink/rmdir で portable 実装(Windows の cmd.exe に rm が無い問題を回避)。 */
int      osglue_rmrf(const char *path);

/* fd の現在のファイルサイズ(バイト)。-1=エラー。別プロセスが**書き込み中**の最新 EOF を返すのが要件。
 * POSIX=::fstat(st_size)。Windows=GetFileSizeEx(CRT fstat は他プロセスの未 flush 書込を stale に返すため)。
 * キャッシュのストリーミング読み(single-writer/multi-reader・書込中 read)で必須。 */
long long osglue_fsize(int fd);

/* ─────────────────────────────────────────────────────────────────────
 * 負荷コントロールの計測 (#3419・docs/srava_load_control_design.md §4)。
 * ★ 「マシンの資源量」ではなく **「このプロセスが実際に使ってよい量」** を返すのが要件。
 * ───────────────────────────────────────────────────────────────────── */

/* 指定 pid が占有しているメモリ量 (バイト)。0=成功 / -1=取得不能。
 *   Linux   : /proc/PID/status の **VmRSS** (既定・#3419 ひさ判断 2026-08-24)。
 *             SRAVA_LOAD_MEM_METRIC=pss 指定時は /proc/PID/smaps_rollup の Pss
 *             (共有ページを参照プロセス数で按分する) を使い、smaps_rollup が無い環境
 *             (kernel < 4.14) は VmRSS へ縮退する。
 *             ★ 按分を既定にしない理由: agent は **execvp** の子であって fork の子ではないので、
 *               CoW で大半のページを共有するという Pss の前提が成立しない。共有ライブラリぶんは
 *               agent 1 個の見積り (差分) で相殺され、metric の違いが残るのは C_MEM を V_MEM と
 *               直接比べる 1 箇所だけで、そこでは RSS ≥ Pss なので必ず安全側に出る。
 *             ★ Pss は smaps_rollup を読むぶん resident ページ数に比例したコストがかかる。
 *   macOS   : proc_pidinfo(PROC_PIDTASKINFO) の pti_resident_size。
 *             ★ **按分できない** (phys_footprint は他プロセスに task_for_pid が要り権限が必要)。
 *               fork する以上ここは過大評価になる。呼び手はそれを承知で使うこと。
 *   Windows : GetProcessMemoryInfo の PrivateUsage。
 *             ★ Windows に fork は無く親子でページを共有しないので按分の問題は起きない。 */
int osglue_proc_memory(uint32_t pid, unsigned long long *bytes_out);

/* ★ #3419 §12: 単調に増える現在時刻 (ミリ秒)。基準点に意味は無く、**差分だけが意味を持つ**。
 * サンプリング窓 (§12.5①) や配布の間引き (§5.3) のように「前回からどれだけ経ったか」を
 * 見る用途に使う。⚠ 壁時計 (gettimeofday) 由来なので、NTP 補正で**後戻りしうる**。
 * 呼び手は差分が負になる場合を考慮すること。 */
long long osglue_now_ms(void);

/* ★ #3419 §17.2 (2026-08-24・ひさ指摘): 環境変数を整数で読む **共通実装**。
 * 未設定 / 空文字列なら def、それ以外は atoi。**`X=0` は 0 として尊重する**。
 *
 * ⚠ 規約 (このコードベースの真偽値フラグは全部これに揃える):
 *   - **正論理**: 1 以上で「有効」・0 で「無効」。`*_OFF` のような負論理は作らない
 *   - **値で判定**: `if (getenv("X"))` の存在チェックは使わない
 *     (存在チェックだと **`X=0` と書いても有効になる** = 切ったつもりで切れない事故が起きる)
 *   - 既定が「有効」のものは def=1 にして `X=0` で切らせる (例: SRAVA_LOAD_RAMP)
 * ⚠ ログ出力先のパスを取る変数 (PIG_TIMING / PIG_SEP_LOG / PIG_POLISH_LOG /
 *   SRAVA_OCCT_LOG) は真偽値ではないので、この規約の対象外。 */
int osglue_env_int(const char *name, int def);

/* ★ #3419 §17.3: 設定表の 1 行。srava 変数 / 環境変数 / 既定 / 起動時のみか。
 * 実体は ptsApplication.cpp の pigcfg_table()。**既定値の唯一の置き場**。 */
struct pigCfgEntry { const char *var; const char *env; const char *def; int ini_only; };
const struct pigCfgEntry *pigcfg_table(void);

/* ★ #3419 §12 (T6-a): 指定 pid がこれまでに消費した CPU 時間 (マイクロ秒)。0=成功 / -1=取得不能。
 * user + system の合計。**累積値**なので、呼び手は 2 点間の差分を経過時間で割って
 * 「いま何コアぶん使っているか」(C_CPU) を出す。
 *   Linux   : /proc/PID/stat の utime + stime (clock tick 単位 → _SC_CLK_TCK で換算)。
 *             ⚠ comm フィールドは括弧内に空白や括弧を含みうるので、**最後の ')' から**パースする。
 *   macOS   : proc_pidinfo(PROC_PIDTASKINFO) の pti_total_user + pti_total_system (ナノ秒)。
 *             ⚠ 単位はドキュメント上ナノ秒。**実機で要確認** (単位を間違えると C_CPU が桁でずれる)。
 *   Windows : GetProcessTimes の kernel + user (FILETIME = 100ns 単位)。
 * ★ メモリ (osglue_proc_memory) と違い**按分の問題は無い**。CPU 時間は共有されないので、
 *   複数プロセスぶんを単純に合計してよい。 */
int osglue_proc_cputime(uint32_t pid, unsigned long long *usec_out);

/* システム全体の物理メモリ (総量 / 利用可能量) をバイトで。0=成功 / -1=取得不能。
 * 不要な方は 0 を渡してよい。
 *   Linux=/proc/meminfo(MemTotal,MemAvailable) / macOS=sysctl hw.memsize + host_statistics64 /
 *   Windows=GlobalMemoryStatusEx(ullTotalPhys, ullAvailPhys)。 */
int osglue_system_memory(unsigned long long *total_out, unsigned long long *avail_out);

/* このプロセスが実際に使ってよい CPU コア数。0 = 取得不能 (呼び手が既定値へ倒す)。
 * ★ std::thread::hardware_concurrency() は使わない。**OS ごとに挙動が違う**ため
 *   (Linux は affinity を無視して全コア数を返すが、Windows は affinity を反映する)。
 *   Linux   : sched_getaffinity() の CPU_COUNT と cgroup の CPU quota の**小さい方**
 *             (cgroup v2 = /sys/fs/cgroup/cpu.max、v1 = cpu.cfs_quota_us + cpu.cfs_period_us)。
 *   macOS   : sysctl hw.logicalcpu。★ affinity API は無いが **QoS クラスで絞りを検出できる**:
 *             pthread_get_qos_class_np() が QOS_CLASS_BACKGROUND なら taskpolicy 等で
 *             **E コアに閉じ込められている**ので hw.perflevel1.logicalcpu を返す。
 *             ★ P/E の性能比は機種世代で変わるので**係数は決め打ちしない**。
 *               構成は hw.nperflevels + hw.perflevel<N>.* を**ループで読む**。
 *   Windows : GetProcessAffinityMask のビット数。取れなければ
 *             GetActiveProcessorCount(ALL_PROCESSOR_GROUPS)。
 *             ★ GetSystemInfo は現在のプロセッサグループしか数えないので使わない
 *               (論理 64 超のマシンで過小になる)。 */
unsigned osglue_usable_cpus(void);

#endif /* PIG_OSGLUE_H */
