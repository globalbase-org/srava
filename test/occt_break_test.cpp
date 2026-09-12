/*
 * occt_break_test — OCCT の中断機構 (#3498) がライブラリ層で本当に効くかの単体回帰。
 *
 * ★ なぜ planner 経由の SIGINT テストでは足りないのか
 *   occt は EXEC_PROCESS 専用なので、planner から SIGINT を撃つと #3417 の撤収 (6.2
 *   DM_CONT_KILL) が agent プロセスごと殺してしまう。**配線が無くても 0.3 秒で終わる**ので、
 *   あの経路では「OCCT が本当に止まったのか、殺されただけなのか」が区別できない。
 *   ⇒ ライブラリを直接叩き、*算法が自分で戻ってくる*ことをここで見る。
 *
 * 見ているもの:
 *   1. 旗を立てずに走らせると成功する (中断機構を繋いだこと自体が結果を変えない)
 *   2. 走行中に別スレッドから旗を立てると、**素の所要よりずっと早く**戻り、しかも失敗
 *      (IsDone()==false) として返る = ブール本体が実際に畳まれている
 *   3. 最初から立っている旗を渡すと即座に戻る
 *
 * ⚠ 2 は時間を見る検査なので、閾値は「素の所要の半分」という**相対**にしてある。
 *   絶対秒だと機械の速さで嘘になる。素の所要が短すぎて検定にならない場合は skip する
 *   (機械が速すぎて中断する隙が無いのは失敗ではない)。
 * 失敗数を exit code で返す (pipeprox_test_pipe と同じ約束)。
 */
#include	"oc/c++/ocShape.h"
#include	"pig/c++/pigBreak.h"

#include	<BRepPrimAPI_MakeBox.hxx>
#include	<gp_Pnt.hxx>

#include	<atomic>
#include	<chrono>
#include	<stdio.h>
#include	<thread>

static int fails = 0;

static void
check(int ok, const char *what)
{
	::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
	if ( ! ok ) ++fails;
}

static double
seconds_since(std::chrono::steady_clock::time_point t0)
{
	return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

/* n^3 個の箱の n 項 union を 1 回。brk を渡す。戻り値は所要秒。 */
static double
run_union(int n, const pigBreak *brk, int *ok_out)
{
	sArray<sPtr<ocShape> > ops;
	ops.length(n * n * n);
	int m = 0;
	for ( int i = 0 ; i < n ; ++i )
	for ( int j = 0 ; j < n ; ++j )
	for ( int k = 0 ; k < n ; ++k ) {
		gp_Pnt p(i * 0.7, j * 0.7, k * 0.7);
		sPtr<ocShape> b = thNEW(ocShape,());
		b->set_shape(BRepPrimAPI_MakeBox(p, 1.0, 1.0, 1.0).Shape());
		ops[m++] = b;
	}
	std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
	char why[512];
	why[0] = '\0';
	sPtr<ocShape> r = ocShape::op_bool_nary(ops, "union", why, (int)sizeof why, brk);
	double el = seconds_since(t0);
	*ok_out = r.is_notNull() ? 1 : 0;
	return el;
}

int
main()
{
	ocShape::ensure_init();

	/* ---- 1. 旗を立てなければ従来どおり成功する ---- */
	const int N = 5;   /* 125 箱。実機で数秒かかる規模 */
	int ok = 0;
	double base = run_union(N, 0, &ok);
	check(ok, "旗なし: n 項 union が成功する");
	::printf("     素の所要 %.2f 秒\n", base);

	/* ---- 3. 最初から立っている旗 ---- */
	{
		pigBreak b;
		b.cancel();
		int ok2 = 1;
		double el = run_union(N, &b, &ok2);
		check(!ok2, "先に立てた旗: union が失敗として返る (成功値を返さない)");
		check(el < base * 0.5, "先に立てた旗: 素の所要の半分未満で戻る");
		::printf("     所要 %.2f 秒 (素 %.2f 秒)\n", el, base);
	}

	/* ---- 2. 走行中に別スレッドから立てる ---- */
	if ( base < 0.5 ) {
		::printf("skip  走行中の中断: 素の所要 %.2f 秒では検定にならない (機械が速い)\n", base);
	} else {
		pigBreak b;
		std::atomic<int> started(0);
		/* 素の所要の 1/4 の時点で旗を立てる。 */
		double at = base * 0.25;
		std::thread th([&]{
			while ( ! started.load() ) std::this_thread::yield();
			std::this_thread::sleep_for(std::chrono::duration<double>(at));
			b.cancel();
		});
		started.store(1);
		int ok3 = 1;
		double el = run_union(N, &b, &ok3);
		th.join();
		check(!ok3, "走行中の中断: union が失敗として返る");
		check(el < base * 0.75, "走行中の中断: 素の所要より明確に早く戻る");
		::printf("     %.2f 秒で旗 → %.2f 秒で復帰 (素 %.2f 秒)\n", at, el, base);
	}

	::printf("%s (%d fail)\n", fails ? "OCCT-BREAK-FAIL" : "OCCT-BREAK-OK", fails);
	return fails;
}
