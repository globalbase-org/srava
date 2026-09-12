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
#include <exception>
#include <string>

/* 現在の予算 (vdGrid.cpp が持つ)。0 以下 = 指定なし = TBB の既定 (コア数)。 */
int vd_op_thread_budget();

/* 予算が指定されていれば task_arena の中で、無ければそのまま f を実行する。
 * ⚠ これは **例外を素通しする**。op から直接使わないこと (下の vd_arena_guard を使う)。 */
template<class F>
inline void
vd_in_arena(F&& f)
{
	int n = vd_op_thread_budget();
	if ( n <= 0 ) { f(); return; }          /* 指定なし = 従来どおり */
	tbb::task_arena arena(n);
	arena.execute(f);
}

/* ★★ #3474 続き (2026-09-05): **例外境界つき**の arena 実行。op はこちらを使う。
 *
 *   ⚠ openvdb 系 3 モジュール (openvdb / openvdb_mf / openvdb_cg / openvdb_gg) には
 *     catch が **1 つも無かった**。ライブラリが投げると受け手が居ないまま伝播し、
 *     ワーカースレッド由来なら agent ごと死ぬ (geogram で実際に踏んだ形・occt は
 *     Standard_Failure 専用の catch を、cherchi は ch_guard を持って対処済み)。
 *     実例: 活性ボクセル 0 の格子に levelSetVolume を掛けると
 *     "LevelSetMeasure does not support empty grids" を throw する。
 *
 *   ★ TBB は **ワーカースレッドで投げられた例外を捕まえて execute() の呼び出し元で
 *     rethrow する**ので、この層に境界を置けば op 内並列からの throw も受けられる。
 *
 *   ★ 捕まえたら **黙って握り潰さない** — 理由を why に書いて 0 を返し、呼び出し側が
 *     自分のモジュール名つきのエラーにする (out/mesh は未設定のままなので、
 *     get_result() が result を優先して返す)。
 *
 *   使い方 (op の compute() は丸ごとこれで包む):
 *       std::string why;
 *       if ( ! vd_arena_guard("union", [&]{ … }, why) )
 *           result = vda_err(thNEW(stdString,(why.c_str())));
 */
template<class F>
inline int
vd_arena_guard(const char *op, F&& f, std::string &why)
{
	try {
		vd_in_arena(f);
		return 1;
	} catch ( const std::exception &e ) {
		/* openvdb の例外は std::exception 派生で、what() に型名が入る
		 * (例 "RuntimeError: LevelSetMeasure does not support empty grids") */
		why = std::string(op) + ": openvdb failed (" + e.what() + ")";
	} catch ( ... ) {
		why = std::string(op) + ": openvdb failed (unknown exception)";
	}
	return 0;
}

#endif
