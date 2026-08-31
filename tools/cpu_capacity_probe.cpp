/*
 * cpu_capacity_probe — 「このマシンで srava が使える CPU 能力」を調べる診断ツール。
 *   docs/srava_load_control_design.md §4.6 の V_CPU の基準値を決めるために使う。
 *
 *   ① 使えるコア数を OS ごとの正しい方法で表示する
 *      (std::thread::hardware_concurrency() は affinity を無視するので参考値として併記)
 *   ② コア構成が不均質な機 (Apple Silicon の P/E コア等) では各 perflevel の内訳を表示する
 *   ③ スレッド数を振って **実効スケーリング**を測る
 *      → 「論理コア N 個は実質いくつ分か」が分かる
 *
 * ★ P/E の性能比は機種世代で大きく変わる (E コアの比率も 1:1 〜 4:1 と幅がある) ので、
 *   係数を埋め込まずに **実行時に構成を発見し、必要なら実測する** のが方針。
 *
 * ビルド: c++ -O2 -std=c++17 -o cpu_capacity_probe cpu_capacity_probe.cpp -lpthread
 */
#include <thread>
#include <vector>
#include <chrono>
#include <cstdio>
#include <cmath>

#if defined(__linux__)
#  include <sched.h>
#  include <unistd.h>
#  include <cstdio>
#elif defined(__APPLE__)
#  include <sys/sysctl.h>
#  include <sys/qos.h>
#  include <pthread.h>
#  include <unistd.h>
#elif defined(_WIN32)
#  include <windows.h>
#endif

#if defined(__APPLE__)
static long sysctl_long(const char *name) {
    long v = 0; size_t sz = sizeof v;
    return (sysctlbyname(name, &v, &sz, nullptr, 0) == 0) ? v : -1;
}
static void sysctl_str(const char *name, char *buf, size_t bufsz) {
    size_t sz = bufsz; if (sysctlbyname(name, buf, &sz, nullptr, 0) != 0) buf[0] = 0;
}
#endif

static void report_topology()
{
    printf("hardware_concurrency()      = %u   %s\n",
           std::thread::hardware_concurrency(),
#if defined(__linux__)
           "(★ Linux では affinity を無視する = 参考値)"
#elif defined(_WIN32)
           "(★ Windows/MinGW では affinity を反映する — 実測確認済み)"
#else
           "(参考値)"
#endif
           );
#if defined(__linux__)
    printf("_SC_NPROCESSORS_ONLN        = %ld\n", sysconf(_SC_NPROCESSORS_ONLN));
    cpu_set_t set; CPU_ZERO(&set);
    if (sched_getaffinity(0, sizeof set, &set) == 0)
        printf("sched_getaffinity CPU_COUNT = %d   ← ★ これが「使えるコア数」\n", CPU_COUNT(&set));
    if (FILE *f = fopen("/sys/fs/cgroup/cpu.max", "r")) {   /* cgroup v2 */
        char q[64] = {0}; long period = 0;
        if (fscanf(f, "%63s %ld", q, &period) == 2)
            printf("cgroup cpu.max              = %s / %ld\n", q, period);
        fclose(f);
    } else {
        printf("cgroup cpu.max              = (無し・非コンテナ)\n");
    }
#elif defined(__APPLE__)
    printf("hw.logicalcpu               = %ld\n", sysctl_long("hw.logicalcpu"));
    printf("hw.physicalcpu              = %ld\n", sysctl_long("hw.physicalcpu"));
    {   /* ★ affinity API は無いが QoS クラスで絞りを検出できる (§4.6.1c) */
        qos_class_t q; int rel;
        if (pthread_get_qos_class_np(pthread_self(), &q, &rel) == 0) {
            const char *n = q==QOS_CLASS_USER_INTERACTIVE?"USER_INTERACTIVE":
                            q==QOS_CLASS_USER_INITIATED  ?"USER_INITIATED":
                            q==QOS_CLASS_DEFAULT         ?"DEFAULT":
                            q==QOS_CLASS_UTILITY         ?"UTILITY":
                            q==QOS_CLASS_BACKGROUND      ?"BACKGROUND":"?";
            printf("QoS クラス                  = %s%s\n", n,
                   q == QOS_CLASS_BACKGROUND
                     ? "   ← ★ E コアに閉じ込められている (taskpolicy -c background 等)"
                     : "");
        }
    }
    long n = sysctl_long("hw.nperflevels");
    if (n > 0) {
        printf("hw.nperflevels              = %ld   ← 不均質コア。内訳:\n", n);
        for (long i = 0; i < n; i++) {
            char key[64], name[64] = {0};
            snprintf(key, sizeof key, "hw.perflevel%ld.name", i);        sysctl_str(key, name, sizeof name);
            snprintf(key, sizeof key, "hw.perflevel%ld.logicalcpu", i);  long lc = sysctl_long(key);
            snprintf(key, sizeof key, "hw.perflevel%ld.l2cachesize", i); long l2 = sysctl_long(key);
            printf("   perflevel%ld  %-12s logical=%-3ld  L2=%ld MB\n",
                   i, name[0] ? name : "(名前無し)", lc, l2 > 0 ? l2 / (1024 * 1024) : -1);
        }
    }
#elif defined(_WIN32)
    printf("GetActiveProcessorCount(ALL)= %lu   ← ★ GetSystemInfo は現グループのみなので使わない\n",
           (unsigned long)GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
    DWORD_PTR pmask = 0, smask = 0;
    if (GetProcessAffinityMask(GetCurrentProcess(), &pmask, &smask)) {
        int c = 0; for (DWORD_PTR m = pmask; m; m >>= 1) c += (int)(m & 1);
        printf("GetProcessAffinityMask bits = %d   ← ★ ただし単一プロセッサグループ内のみ\n", c);
    }
#endif
}

static double busy(long n) { double a = 0; for (long i = 1; i <= n; i++) a += std::sqrt((double)i) * 1.0000001; return a; }

int main(int argc, char **argv)
{
    long total = (argc > 1) ? atol(argv[1]) : 4000000000L;
    printf("=== トポロジ ===\n"); report_topology();

    unsigned hw = std::thread::hardware_concurrency(); if (!hw) hw = 4;
    printf("\n=== 実効スケーリング (総仕事量 %ld を T スレッドで分担) ===\n", total);
    double base = 0, sink = 0;
    for (unsigned t = 1; t <= hw; t = (t < 4 ? t * 2 : t + (hw > 8 ? hw / 4 : 2))) {
        auto t0 = std::chrono::steady_clock::now();
        std::vector<std::thread> th; std::vector<double> r(t);
        for (unsigned i = 0; i < t; i++) th.emplace_back([&, i] { r[i] = busy(total / t); });
        for (auto &x : th) x.join();
        double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        for (double v : r) sink += v;
        if (t == 1) base = s;
        printf("  threads=%-3u  %7.2f s   加速比 %5.2fx\n", t, s, base / s);
        fflush(stdout);
    }
    printf("\n★ 最大スレッド数での加速比が「論理コア数は実質いくつ分か」。\n"
           "   V_CPU の基準値を決めるとき、この差を %% 指定で吸収するか判断する。\n"
           "⚠ このループは compute-bound。memory-bound な実ワークロードでは値が下がりうる。\n");
    if (sink < 0) printf("%f\n", sink);
    return 0;
}
