/*
 * modules/d3/manifest.cpp — d3.so の唯一の C エクスポート (rev4 Phase D-3・manifold/manifest.cpp のミラー)。
 *   記述子本体 (make_agent + OPS) は d3atsAgent.cpp が持つ (単一ソース)。この TU は d3.so にだけリンクする。
 */
#include "pig/c++/pigModule.h"
#include "pig/c++/pigModuleRegistry.h"

extern const srava_module_descriptor d3atsAgent_descriptor;   /* d3atsAgent.cpp */

/* ★ rev4 Phase A: d3 が I/O する実装型を型軸レジストリへ登録 (dlopen 時の静的初期化)。
 *   d3Mesh=D3M3 と 1:1。キャッシュキーソルトも d3.so が自己申告 (registry の予約に依存しない)。 */
/* ★ #3427: 型登録・ソルト申告の静的自己登録は撤去。型は記述子 (provides) から
 *   pigModuleRegistry::register_descriptor が登録する。
 * ★ #3466 (ABI v17): キャッシュキーのソルトは **記述子から撤去**した。レジストリが
 *   「モジュール名 + その .so の指紋」から作る (申告するものではなくなった)。 */

SRAVA_MODULE_EXPORT const srava_module_descriptor* srava_module(void)
{
	return &d3atsAgent_descriptor;
}
