/*
 * manifold_break_test — Manifold の中断機構 (#3498) がライブラリ層で効くかの単体回帰。
 * occt_break_test / openvdb_break_test の姉妹版。
 *
 * ★ manifold は 3 つの中で **形が違う**ので、確かめる項目も違う:
 *   ① 中断の口は @c ExecutionContext::Cancel() = *こちらから撃つ* 関数。⇒ 別スレッドから
 *     撃てること (pigBreak の押し出し口 pigBreakHook) が動くこと自体が検査対象。
 *   ② 中断を観測できるのは @c Status() だけ。@c Volume / @c GetMeshGL は ctx を見ない。
 *     ⇒ force_eval が Status() を通っていること = 「Volume を呼んでも止まらない」という
 *     上流の性質を、こちらが取り違えていないこと。
 *   ③ 粒度は **sub-boolean ごと**。⇒ 1 個の巨大なブールは止まらない。木か BatchBoolean が要る。
 *
 * 見ているもの:
 *   1. 旗なしで BatchBoolean が評価できる
 *   2. 先に立てた旗で force_eval が失敗として返り、"cancelled" を理由に持つ
 *   3. ★ 走行中に **別スレッドから** 旗を立てると、素の所要よりずっと早く戻る
 *      (= pigBreakHook が ctx.Cancel() を撃てている)
 *   4. force_eval のあと、同じ mesh の GetMeshGL64 は **ブールを再評価しない** (木の cache_ を
 *      共有する)。= 評価点を compute() へ移しても **総仕事量は増えない**、という主張の検証
 *
 * 失敗数を exit code で返す。
 */
#include	"mf/c++/mfMesh.h"
#include	"pig/c++/pigBreak.h"

#include	<atomic>
#include	<chrono>
#include	<stdio.h>
#include	<string.h>
#include	<thread>
#include	<vector>

static int fails = 0;

static void
check(int ok, const char *what)
{
	::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
	if ( ! ok ) ++fails;
}

static double
secs(std::chrono::steady_clock::time_point t0)
{
	return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

/* ずらした球 n 個の BatchBoolean。★ 中断の粒度は sub-boolean ごとなので、
 * **1 個の大きなブールではなく n 個**にすること (1 個だと止まらない = 検定にならない)。 */
static sPtr<mfMesh>
make_tree(int n, int seg)
{
	std::vector<manifold::Manifold> v;
	v.reserve((size_t)n);
	for ( int i = 0 ; i < n ; ++i ) {
		sPtr<mfMesh> s = mfMesh::sphere(1.0, seg);
		const double t = 0.55 * (double)i;
		double e[12] = { 1,0,0,t,  0,1,0,0,  0,0,1,0 };
		v.push_back(sPtr<mfMesh>::d_cast(s->apply_affine(e))->manifold());
	}
	return thNEW(mfMesh,(manifold::Manifold::BatchBoolean(v, manifold::OpType::Add)));
}

int
main()
{
	/* ★ 走行中の中断を検定できるだけの仕事量が要る (実機で数秒かかる規模)。
	 *   小さすぎると「素が速すぎて検定にならない」で skip され、**押し出し口の検査が
	 *   丸ごと落ちる** — そこがこのテストの本命なので、余裕を持たせてある。 */
	const int N = 64, SEG = 384;

	/* ---- 1. 旗なし ---- */
	std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
	sPtr<mfMesh> m = make_tree(N, SEG);
	const char *why = 0;
	const int ok1 = m->force_eval(0, &why);
	const double base = secs(t0);
	check(ok1, "旗なし: BatchBoolean の木が評価できる");
	::printf("     素の所要 %.2f 秒\n", base);

	/* ---- 4. 評価は共有される (encode の GetMeshGL64 がブールを再評価しない) ---- */
	{
		t0 = std::chrono::steady_clock::now();
		const int nf = m->op_nfaces();   /* 内部で GetMeshGL64 */
		const double el = secs(t0);
		check(nf > 0, "評価後: 面数が取れる");
		/* ★ 木の CsgOpNode が結果を cache_ に持つので、2 度目は抽出だけ。 */
		check(el < base * 0.5, "評価後の GetMeshGL64 はブールを再評価しない (総仕事量は増えない)");
		::printf("     2 度目 %.3f 秒 (素 %.2f 秒・三角形 %d)\n", el, base, nf);
	}

	/* ---- 2. 先に立てた旗 ---- */
	{
		sPtr<mfMesh> m2 = make_tree(N, SEG);
		pigBreak b;
		b.cancel();
		t0 = std::chrono::steady_clock::now();
		const char *w = 0;
		const int ok = m2->force_eval(&b, &w);
		const double el = secs(t0);
		check(!ok, "先に立てた旗: force_eval が失敗として返る");
		check(w != 0 && ::strcmp(w, "cancelled") == 0, "先に立てた旗: 理由が cancelled");
		check(el < base * 0.5, "先に立てた旗: 素の所要の半分未満で戻る");
		::printf("     %.3f 秒 / 理由 \"%s\"\n", el, w ? w : "(none)");
	}

	/* ---- 3. ★ 走行中に別スレッドから (押し出し口 pigBreakHook の検査) ---- */
	if ( base < 0.5 ) {
		::printf("skip  走行中の中断: 素の所要 %.2f 秒では検定にならない (機械が速い)\n", base);
	} else {
		sPtr<mfMesh> m3 = make_tree(N, SEG);
		pigBreak b;
		std::atomic<int> go(0);
		const double at = base * 0.25;
		std::thread th([&]{
			while ( ! go.load() ) std::this_thread::yield();
			std::this_thread::sleep_for(std::chrono::duration<double>(at));
			b.cancel();   /* ★ ここが pigBreakHook 経由で ctx.Cancel() を撃つ */
		});
		go.store(1);
		t0 = std::chrono::steady_clock::now();
		const char *w = 0;
		const int ok = m3->force_eval(&b, &w);
		const double el = secs(t0);
		th.join();
		check(!ok, "走行中の中断: force_eval が失敗として返る");
		check(w != 0 && ::strcmp(w, "cancelled") == 0, "走行中の中断: 理由が cancelled");
		check(el < base * 0.75, "走行中の中断: 素の所要より明確に早く戻る");
		::printf("     %.2f 秒で旗 → %.2f 秒で復帰 (素 %.2f 秒)\n", at, el, base);
	}

	::printf("%s (%d fail)\n", fails ? "MF-BREAK-FAIL" : "MF-BREAK-OK", fails);
	return fails;
}
