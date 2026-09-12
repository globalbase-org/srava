/*
 * demo_compute — 第3モジュール実証の計算本体 (docs §7 Phase 6)。純 value op。
 *   demo_add(a, b)  → a + b            (スカラ)
 *   demo_range(n)   → [0, 1, ..., n-1] (配列)
 *   demo_spin(sec)  → sec 秒回って sec を返す  ★ #3417: **中断できることの最小デモ**
 * どちらも mesh を持たない = キャッシュ codec 不要。ホスト無改修で op を増やせることの最小デモ。
 */
#include "demo_compute.h"

#include "ts2/c++/sCallSection.h"   /* ★ #3417: 現在実行中 tinyState (中断の問い合わせ先) */
#include <string.h>
#include <time.h>
#include	"pig/c++/pigModuleError.h"
/* ★ #3475: このモジュール専用のエラー生成子 (共有ヘッダを持たないので
 *   ここで定義する)。文言は "[TAG] demo/op: message" になる。 */
PIG_DEFINE_MODULE_ERR(dema_err, "demo")


static double num(sPtr<pigData> v) { return ( v != thNULL ) ? v->get_flt() : 0.0; }

static sPtr<pigData> compute_add(sArray<sPtr<pigData> >& args)
{
	if ( args.length() < 2 )
		return dema_err(thNEW(stdString,("demo_add: needs 2 numbers")));
	double a = num(args[0]);
	double b = num(args[1]);
	return thNEW(pigDataFloat,((double)(a + b)));
}

static sPtr<pigData> compute_range(sArray<sPtr<pigData> >& args)
{
	INTEGER64 n = ( args.length() >= 1 && args[0] != thNULL ) ? args[0]->get_int() : 0;
	if ( n < 0 ) n = 0;
	sPtr<pigDataArray> out = thNEW(pigDataArray,());
	for ( INTEGER64 i = 0 ; i < n ; ++i )
		out->push(thNEW(pigDataInteger,((INTEGER64)i)));
	return out;
}

/* ---- ★ #3417: 中断できる「重い op」の最小デモ -------------------------------
 * sec 秒ぶん回るだけ。**10ms 刻みで is_destroyed() を見る**ので、走行中に destroy が来れば
 * そこで中断エラーを返す。実カーネル (manifold の ExecutionContext・openvdb の InterruptT・
 * occt の Message_ProgressRange) を配線したときに起きることの雛形で、
 * **実カーネルに一切触らずに** graceful teardown の経路を端から端まで検証できる。
 *
 * ⚠ **中断を成功として返さない**。中断は「答えが出なかった」であって「答えは空」ではない。
 *   成功値を返すとそれがキャッシュに焼き付き、次回以降 **正しい答えとして引かれる**
 *   (#3489 で cache_version を上げ忘れて古い誤値が返ったのと同じ形の事故になる)。 */
static sPtr<pigData> compute_spin(sArray<sPtr<pigData> >& args)
{
	double sec = ( args.length() >= 1 && args[0] != thNULL ) ? args[0]->get_flt() : 0.0;
	if ( sec < 0.0 ) sec = 0.0;
	const double SLICE = 0.01;                        /* 10ms 刻みで中断を見る */
	const long slices = (long)(sec / SLICE + 0.5);
	for ( long i = 0 ; i < slices ; ++i ) {
		/* ★ 既存の作法どおり素で引く (ptsWirePipe.cpp:200 / ptsApplication.cpp:647 と同じ形)。
		 *   sThreadKey::operator-> がスレッドごとの実体を返すので、TS_THREAD の中でも効く。 */
		sPtr<tinyState> me = sCallSection::key->caller();
		if ( me.is_notNull() && me->is_destroyed() )
			return dema_err(thNEW(stdString,("demo_spin: aborted (destroyed)")));
		struct timespec ts;
		ts.tv_sec  = 0;
		ts.tv_nsec = (long)(SLICE * 1e9);
		::nanosleep(&ts, 0);
	}
	return thNEW(pigDataFloat,((double)sec));
}

sPtr<pigData>
demo_compute(const char *op, sArray<sPtr<pigData> >& args)
{
	if ( op && ::strcmp(op, "demo_range") == 0 ) return compute_range(args);
	if ( op && ::strcmp(op, "demo_add")   == 0 ) return compute_add(args);
	if ( op && ::strcmp(op, "demo_spin")  == 0 ) return compute_spin(args);
	return dema_err(thNEW(stdString,("demo: unknown op")));
}
