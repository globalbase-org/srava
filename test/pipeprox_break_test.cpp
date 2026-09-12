/*
 * pipeprox_break_test — pipe_proximity の走行中中断 (#3502) の単体回帰。
 * occt_break / openvdb_break / manifold_break の姉妹版。
 *
 * ★ pipe_proximity は他の 3 者と違い **上流に中断の口が無かった**ので、こちらで
 *   @c CtrlParams::cancelled (述語) と @c CtrlResult::cancelled (結果) を足した。
 *   ⇒ ここで見るのは「上流の機構が効くか」ではなく **足した機構が正しいか**。
 *
 * ★ このテストは vendor (@c namespace pipe) だけを叩く。pigBreak も tinyState も出てこない —
 *   ライブラリを **ホスト非依存に保った**こと自体が検査項目でもある (srava 側の
 *   pigBreak → 述語の橋は pipe_proximity_compute.cpp が持つ)。
 *
 * 見ているもの:
 *   1. 述語なしなら従来どおり収束する (機構を足したこと自体が結果を変えない)
 *   2. 先に立てた旗で **即座に**戻り、@c res.cancelled が真
 *   3. ★ 走行中に別スレッドから立てると、素の所要よりずっと早く戻る
 *   4. ★★ 中断した結果は「収束が浅いだけの普通の結果」と **見分けがつかない** —
 *      @c iters / @c energy / @c constraintsFeasible のどれも正常値を返す。
 *      @c cancelled だけが違う。⇒ 呼び手がこれを見て捨てる約束を、ここで固定する。
 *
 * 失敗数を exit code で返す (pipeprox_test_pipe と同じ約束)。
 */
#include	"pipe/controller.hpp"
#include	"pipe/radius.hpp"
#include	"pipeprox_break_thread.h"   /* ★ 並行部分は別 TU (理由はそのヘッダ冒頭) */

#include	<chrono>
#include	<cmath>
#include	<optional>
#include	<stdio.h>

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

/* 自己接触するコイル。★ 接触が在ることが条件 — 接触が無いと数反復で収束してしまい、
 * 「走行中に止める」検定にならない (test/srava_parse.sh の pipeprox_fixed_force と同じ理由)。 */
static pipe::ChainDesign
make_coil(int ctr, int turns, double R, double r)
{
	const double PI = 3.14159265358979323846;
	const int n = turns * ctr;
	std::vector<pipe::Vec3> pts;
	pts.reserve((size_t)n);
	for ( int i = 0 ; i < n ; ++i ) {
		const double t = 2.0 * PI * (double)turns * (double)i / (double)(n - 1);
		const double z = -2.0 * r * (double)turns * (double)i / (double)(n - 1);
		pts.push_back(pipe::Vec3{ R * ::cos(t), R * ::sin(t), z });
	}
	pipe::ChainDesign d;
	d.S = pts.front();
	d.E = pts.back();
	for ( size_t i = 1 ; i + 1 < pts.size() ; ++i ) d.C.push_back(pts[i]);
	return d;
}

static pipe::CtrlParams
make_params(int maxIter)
{
	pipe::CtrlParams cp;
	cp.dMin    = 0.5;
	cp.maxIter = maxIter;
	cp.solver  = 1;      /* 座標降下 (parallel_for を使う経路) */
	cp.fZ      = -0.1;
	return cp;
}

int
main()
{
	/* ★ ITER は「走行中に止める」検定が成立する程度に大きく、かつ ctest を長くしない値。
	 *   所要はほぼ ITER に比例する (実機で 150 反復が数秒かかる規模)。
	 *   ⚠ 小さすぎると素が 0.5 秒を切って項目 3 が skip され、**本命の検査が落ちる**。 */
	const int CTR = 6, TURNS = 2, ITER = 150;
	const double R = 12.0, r = 4.0;
	/* 一様半径 r。RadiusFn は std::function<std::optional<double>(double s)> (radius.hpp)。 */
	pipe::RadiusFn Rf = [r](double) -> std::optional<double> { return r; };

	/* ---- 1. 述語なし ---- */
	std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
	pipe::CtrlResult base = pipe::adjust(make_coil(CTR, TURNS, R, r), Rf, make_params(ITER));
	const double bt = secs(t0);
	check(!base.cancelled, "述語なし: cancelled が偽");
	check(base.iters > 0, "述語なし: 反復が回っている");
	::printf("     素の所要 %.2f 秒 (iters=%d energy=%.6f)\n", bt, base.iters, base.energy);

	/* ---- 2. 先に立てた旗 ---- */
	{
		ppbrk_clear();
		ppbrk_set();
		pipe::CtrlParams cp = make_params(ITER);
		cp.cancelled = []{ return ppbrk_get(); };
		t0 = std::chrono::steady_clock::now();
		pipe::CtrlResult res = pipe::adjust(make_coil(CTR, TURNS, R, r), Rf, cp);
		const double el = secs(t0);
		check(res.cancelled, "先に立てた旗: res.cancelled が真");
		check(el < bt * 0.5, "先に立てた旗: 素の所要の半分未満で戻る");
		::printf("     %.3f 秒 (素 %.2f 秒)\n", el, bt);

		/* ---- 4. ★ 中断した結果は「普通の結果」と見分けがつかない ---- */
		check(res.constraintsFeasible == base.constraintsFeasible,
		      "★ 中断しても constraintsFeasible は正常値のまま (見分けがつかない)");
		check(res.maxClearViolation >= 0.0,
		      "★ 中断しても maxClearViolation は普通の数値 (見分けがつかない)");
		::printf("     中断: iters=%d energy=%.6f feasible=%d clearViol=%.6f\n"
		         "     ⇒ cancelled 以外に中断を知る手が無い。呼び手はこれを見て結果を捨てること\n",
		         res.iters, res.energy, (int)res.constraintsFeasible, res.maxClearViolation);
	}

	/* ---- 3. ★ 走行中に別スレッドから ---- */
	if ( bt < 0.5 ) {
		::printf("skip  走行中の中断: 素の所要 %.2f 秒では検定にならない (機械が速い)\n", bt);
	} else {
		ppbrk_clear();
		pipe::CtrlParams cp = make_params(ITER);
		cp.cancelled = []{ return ppbrk_get(); };
		const double at = bt * 0.25;
		ppbrk_arm(at);
		ppbrk_go();
		t0 = std::chrono::steady_clock::now();
		pipe::CtrlResult res = pipe::adjust(make_coil(CTR, TURNS, R, r), Rf, cp);
		const double el = secs(t0);
		ppbrk_join();
		check(res.cancelled, "走行中の中断: res.cancelled が真");
		check(el < bt * 0.75, "走行中の中断: 素の所要より明確に早く戻る");
		::printf("     %.2f 秒で旗 → %.2f 秒で復帰 (素 %.2f 秒・iters=%d)\n",
		         at, el, bt, res.iters);
	}

	::printf("%s (%d fail)\n", fails ? "PP-BREAK-FAIL" : "PP-BREAK-OK", fails);
	return fails;
}
