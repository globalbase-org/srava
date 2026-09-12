/*
 * demo_compute — 第3モジュール実証の計算本体 (docs §7 Phase 6)。純 value op。
 *   demo_add(a, b)  → a + b            (スカラ)
 *   demo_range(n)   → [0, 1, ..., n-1] (配列)
 * どちらも mesh を持たない = キャッシュ codec 不要。ホスト無改修で op を増やせることの最小デモ。
 */
#include "demo_compute.h"

#include <string.h>
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

sPtr<pigData>
demo_compute(const char *op, sArray<sPtr<pigData> >& args)
{
	if ( op && ::strcmp(op, "demo_range") == 0 ) return compute_range(args);
	if ( op && ::strcmp(op, "demo_add")   == 0 ) return compute_add(args);
	return dema_err(thNEW(stdString,("demo: unknown op")));
}
