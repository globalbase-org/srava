#pragma once
// 座標降下(cd)の並列実行プリミティブ。
// 注意: <thread> は <unistd.h> を引き込み、グローバル関数 pipe() が namespace pipe と衝突する。
//   そのため実装(cd_parallel.cpp)は **namespace pipe を開かない** 別 TU に隔離し、
//   ここでは <functional> だけで宣言する(controller.cpp は <thread> を含めずに使える)。
#include <functional>

namespace pipe_par {

// [0,count) を nthreads で並列に消化(atomic work-steal)。nthreads<=1 / count<=1 は直列実行。
void parallel_for(int count, int nthreads, const std::function<void(int)>& fn);

// std::thread::hardware_concurrency()(0 なら不明)。スレッド上限の自動決定用。
unsigned hw_threads();

} // namespace pipe_par
