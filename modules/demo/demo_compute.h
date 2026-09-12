#ifndef ___demo_compute_H___
#define ___demo_compute_H___

/*
 * demo_compute — 「第3モジュール」実証 (docs §7 Phase 6) の計算本体。純 value op のみ。
 *   ホスト (planner/srava_agent) を一切改修せず、demo.so を探索路に置き
 *   `module("demo.so",{priority:99})` で既定カーネル化するだけで新 op (demo_add / demo_range) が
 *   使えるようになることを示す最小デモ。CGAL も Manifold も srava 言語も参照しない (pigData のみ)。
 */
#include "pig/c++/pigData.h"
#include "ts2/c++/sArray.h"

/* ★ #3417 (2026-09-06): demo_spin は途中で **中断できる**。
 *
 * 中断の問い合わせ先は @c sCallSection::key->caller() — 「現在実行中の tinyState」を
 * スレッドローカルに持つ仕組み (ts2/c++/sCallSection.h) で、**TS_THREAD の中でも効く**
 * (ひさ確認 2026-09-06)。よって計算本体へ余分な引数もコールバックも渡さなくてよい。
 *
 * ⚠ tinyState は destroy() が **呼び手のスレッドで直にフラグを立てる**ので、
 *   計算スレッドから is_destroyed() を引けば即座に真になる。
 *   一方 TSE_DESTROY イベントの方はキューに溜まるだけで、計算中は配送されない
 *   ⇒ **is_destroyed() を引くのが計算中に中断を知る唯一の手**。 */

/* op 名と引数 (idx 順) → 結果 pigData (value)。未知 op / 引数不正は pigDataError。 */
sPtr<pigData> demo_compute(const char *op, sArray<sPtr<pigData> >& args);

#endif
