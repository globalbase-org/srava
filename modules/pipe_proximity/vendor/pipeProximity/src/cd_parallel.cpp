// cd の並列プリミティブ実装。**namespace pipe を開かない**(POSIX pipe() 衝突回避のため別 TU)。
#include "pipe/cd_parallel.hpp"
#include <thread>
#include <atomic>
#include <vector>
#include <algorithm>

namespace pipe_par {

void parallel_for(int count, int nthreads, const std::function<void(int)>& fn){
    if(nthreads <= 1 || count <= 1){ for(int i=0;i<count;i++) fn(i); return; }
    int T = std::min(nthreads, count);
    std::atomic<int> next{0};
    std::vector<std::thread> ths; ths.reserve(T);
    for(int t=0;t<T;t++) ths.emplace_back([&]{ int i; while((i=next.fetch_add(1))<count) fn(i); });
    for(auto& th : ths) th.join();
}

unsigned hw_threads(){ return std::thread::hardware_concurrency(); }

} // namespace pipe_par
