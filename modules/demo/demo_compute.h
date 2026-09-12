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

/* op 名と引数 (idx 順) → 結果 pigData (value)。未知 op / 引数不正は pigDataError。 */
sPtr<pigData> demo_compute(const char *op, sArray<sPtr<pigData> >& args);

#endif
