#ifndef ___vdArena_H___
#define ___vdArena_H___
/*
 * vdArena — **op あたり**の TBB スレッド予算で計算を走らせるスコープヘルパ (#3419 / #3441)。
 *
 * ★★ なぜ global_control ではないか (vdGrid.cpp 冒頭の意味論そのもの):
 *   予算の意味は「**1 つの op** が op 内並列に使ってよいスレッド数の上限」。
 *     - process 実行 … 1 プロセス = 1 op なので global_control でも一致する
 *     - in-proc 実行 … 1 プロセスに op が N 個同居するので、プロセス全体に張る global_control は
 *                      「同居する全 op の合計」に化けてしまう
 *   tbb::task_arena は **スコープごと**に同時実行数を決めるので、in-proc でも意味が保たれる。
 *   ⚠ oneTBB の global_control は複数生存すると **最小値が勝つ** (加算ではない) ので、
 *     モジュールが各自持つと配分そのものが表現できない。arena にはその問題が無い。
 *
 * ⚠ arena は **スレッドを予約しない**。同時実行の上限を決めるだけなので、arena を複数立てれば
 *   合計は共有プールを超えうる。「op あたりの上限」であって「全体の上限」ではない。
 *
 * ⚠⚠ **プールの本数そのものは arena では縮まない**。絞れるのは同時実行数で、スレッド自体は
 *   TBB が既定どおり立てる。⇒ 効果を確かめるときは **スレッド数ではなく CPU 時間**を見ること。
 *   process 実行では agent ごとに TBB プールが立つので、総スレッド数はコア数を大きく超える。
 * ★ **これは容認する** (ひさ方針 2026-08-26): スレッド増発のオーバヘッドは srava では管理せず
 *   **OS のスレッドスケジューリングに任せる**。他プロセスへの影響が心配なときは
 *     ・loadControl の CPU 項 (モジュールによっては限定的)
 *     ・taskset など OS 規定の方法
 *   でガードする。⇒ **global_control は導入しない** (プロセス全体を縛る手段は持たない)。
 *
 * ★ TBB を触るのは **モジュール側だけ**。pig (カーネル非依存層) は TBB をリンクしていないので、
 *   ptsCalcBody 等の共通経路には置けない (置くと層構造が壊れる)。
 */
#include <tbb/task_arena.h>

/* 現在の予算 (vdGrid.cpp が持つ)。0 以下 = 指定なし = TBB の既定 (コア数)。 */
int vd_op_thread_budget();

/* 予算が指定されていれば task_arena の中で、無ければそのまま f を実行する。 */
template<class F>
inline void
vd_in_arena(F&& f)
{
	int n = vd_op_thread_budget();
	if ( n <= 0 ) { f(); return; }          /* 指定なし = 従来どおり */
	tbb::task_arena arena(n);
	arena.execute(f);
}

#endif
