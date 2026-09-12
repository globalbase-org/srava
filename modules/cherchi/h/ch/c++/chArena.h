#ifndef ___chArena_H___
#define ___chArena_H___
/*
 * chArena — **op あたり**の TBB スレッド予算で計算を走らせるスコープヘルパ (#3481)。
 *   openvdb の vdArena.h と同型。設計の理屈はそちらの冒頭コメントが本文。
 *
 * ★★ なぜ task_arena か (cherchi 固有の事情):
 *   IRMB (InteractiveAndRobustMeshBooleans) には **op 内並列を切るスイッチが無い**。
 *   srava が渡している TBB_PARALLEL マクロは Cinolib の octree にしか効かず、
 *   ブール本体 (code/booleans.cpp・code/foctree.cpp) は @tbb::parallel_for@ を
 *   マクロに囲まれず直に呼んでいる。manifold の MANIFOLD_PAR のような
 *   「コンパイル時に丸ごと切る」手段が上流に存在しない。
 *   ⇒ @tbb::parallel_for@ が **カレント arena** で走ることを使い、呼び出し側で囲む。
 *      上流を 1 行も触らずに絞れる。
 *
 * ★ 包む場所は **@booleanPipeline@ の呼び出し 1 箇所だけで網羅できる** (chMesh.cpp)。
 *   cherchi モジュール内で TBB に触れるのはそこだけで、プリミティブ生成 (common/solids.h)
 *   や codec は素の逐次コード。⚠ IRMB の入口を増やしたら **必ずここで包むこと** —
 *   openvdb では包み漏れた箇所があって「効かない」と誤診した (#3441)。
 *
 * ⚠⚠ **arena はスレッドプールを縮めない**。絞れるのは同時実行数だけなので、
 *   効いたかを **スレッド本数や wall で確かめてはいけない**。CPU 時間 (または avg_cores)
 *   で見ること (#3441 で踏んだ罠)。
 */
#include	"pig/c++/pigData.h"
#include	<tbb/task_arena.h>

/* 現在の予算 (chMesh.cpp がモジュール専用スロットに持つ)。0 以下 = 指定なし = TBB の既定。 */
int ch_op_thread_budget();

/* 予算が指定されていれば task_arena の中で、無ければそのまま f を実行する。
 * ⚠ 例外は素通しする (呼び手の ch_guard が受ける)。TBB は **ワーカースレッドで投げられた
 *   例外を execute() の呼び出し元で rethrow する**ので、guard を外側に置けば op 内並列
 *   からの throw も受けられる。 */
template<class F>
inline void
ch_in_arena(F&& f)
{
	int n = ch_op_thread_budget();
	if ( n <= 0 ) { f(); return; }          /* 指定なし = 従来どおり */
	tbb::task_arena arena(n);
	arena.execute(f);
}

#endif
