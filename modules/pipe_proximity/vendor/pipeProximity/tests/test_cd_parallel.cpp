// 並列プリミティブ (pipe_par::parallel_for) の回帰。
// ★ **namespace pipe を開かない別 TU**。<thread> が <unistd.h> を引き込み、グローバル pipe() が
//   namespace pipe と衝突するため (cd_parallel.cpp と同じ理由)。test_pipe.cpp から呼ばれる。
#include "pipe/cd_parallel.hpp"
#include <atomic>
#include <cstdio>
#include <stdexcept>
#include <thread>
#include <vector>

static int g_fail = 0;
#define CHECK(cond, msg) do{ if(!(cond)){ \
    printf("  FAIL: %s  (%s:%d)\n", msg, __FILE__, __LINE__); ++g_fail; } \
    else printf("  ok  : %s\n", msg); }while(0)

int test_cd_parallel(){
    printf("\n== cd_parallel (常駐プール) ==\n");

    // ★ 決定性の土台: どの添字も**ちょうど 1 回**。呼び出し側はこの性質に依存して
    //   試行結果を添字ごとに書き分けている (最良選択は直列)。
    {
        bool once = true;
        for(int rep=0; rep<200 && once; ++rep){
            std::vector<std::atomic<int> > hit(97);
            for(size_t i=0;i<hit.size();++i) hit[i].store(0);
            pipe_par::parallel_for(97, 6, [&](int i){ hit[i].fetch_add(1); });
            for(size_t i=0;i<hit.size();++i) if(hit[i].load()!=1) once = false;
        }
        CHECK(once, "each index runs exactly once (200 reps)");
    }

    // 直列に落ちる条件 (プール化で変えていない)
    { int n=0; pipe_par::parallel_for(5, 1, [&](int){ ++n; }); CHECK(n==5, "nthreads<=1 runs serially"); }
    { int n=0; pipe_par::parallel_for(1, 8, [&](int){ ++n; }); CHECK(n==1, "count<=1 runs serially"); }
    { int n=0; pipe_par::parallel_for(0, 8, [&](int){ ++n; }); CHECK(n==0, "count==0 does nothing"); }
    { std::atomic<int> n(0); pipe_par::parallel_for(3, 16, [&](int){ ++n; }); CHECK(n.load()==3, "count < nthreads"); }

    // ★ ワーカー上の throw を呼び出し元へ送り直す。素通しさせると std::terminate になり、
    //   in-proc (既定 EXEC_THREAD) では planner ごと落ちる。
    {
        bool caught=false;
        try { pipe_par::parallel_for(64, 6, [](int i){ if(i==31) throw std::runtime_error("x"); }); }
        catch(const std::runtime_error&){ caught=true; }
        CHECK(caught, "exception reaches the caller");
        std::atomic<int> n(0);
        pipe_par::parallel_for(40, 6, [&](int){ ++n; });
        CHECK(n.load()==40, "pool still usable after an exception");
    }

    // ★ プールは thread_local。in-proc では複数の agent スレッドが同時に入る。
    {
        std::atomic<long> tot(0);
        std::vector<std::thread> ts;
        for(int t=0;t<8;t++) ts.push_back(std::thread([&]{
            for(int r=0;r<200;r++) pipe_par::parallel_for(24, 4, [&](int){ tot.fetch_add(1); });
        }));
        for(size_t i=0;i<ts.size();++i) ts[i].join();
        CHECK(tot.load()==8L*200*24, "concurrent callers do not interfere");
    }

    return g_fail;
}
