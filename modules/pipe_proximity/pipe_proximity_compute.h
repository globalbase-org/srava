#ifndef ___pipe_proximity_compute_H___
#define ___pipe_proximity_compute_H___

/*
 * pipe_proximity_compute — pipe_proximity の計算本体 (op ディスパッチ + pigData ⇄ plain 型の
 * marshaling)。純 pigData 境界 (`pp_compute(op, args) -> pigData`) を公開し、
 *   - process 版:  main() が pigplugin::serve(&pp_compute) で駆動 (pipe_proximity_agent.cpp)
 *   - in-proc 版:  planner 内 thread の ptsAgent 派生 (pipe_proximity_ts_agent) が直接呼ぶ
 * の両方から共有する (.so 化 Phase 5・docs §5)。SDK (serve) にも srava/CGAL にも依存しない。
 */
#include	"pig/c++/pigBreak.h"   /* #3502: 中断の旗 */
#include "pig/c++/pigData.h"
#include "ts2/c++/sArray.h"

/* op 名と引数 (idx 順) を受け取り結果 pigData を返す。エラーは pigDataError。
 * 型は pigplugin::ComputeFn と一致 (serve() にそのまま渡せる)。 */
sPtr<pigData> pp_compute(const char *op, sArray<sPtr<pigData> >& args);

/* ★ #3502: 中断できる入口。in-proc 版 (ppaCompute) はこちらを呼び、基底 ptsCalcBody の
 *   brk_ を渡す。brk=0 なら pp_compute と同じ。
 * ⚠ pp_compute のほうに引数を足せないのは、process 版が @c pigplugin::serve に
 *   **関数ポインタ**として渡しているため (既定引数では関数の型が変わってしまう)。 */
sPtr<pigData> pp_compute_brk(const char *op, sArray<sPtr<pigData> >& args, const pigBreak *brk);

#endif
