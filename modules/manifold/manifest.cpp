/*
 * modules/manifold/manifest.cpp — manifold.so の唯一の C エクスポート (.so 化 Phase 3b・docs §3.1)。
 * 記述子本体 (make_agent + OPS) は mfatsAgent.cpp が持つ (単一ソース)。ここは公開点だけ。
 * この TU は manifold.so にだけリンクし、静的実行体 (srava_agent_mf / planner) には入れない。
 */
#include "pig/c++/pigModule.h"
#include "pig/c++/pigModuleRegistry.h"

extern const srava_module_descriptor mfatsAgent_descriptor;   /* mfatsAgent.cpp */

/* ★ rev4 Phase A: manifold が I/O する実装型を型軸レジストリへ登録 (dlopen 時の静的初期化)。
 *   mfMesh=MFM3 / mfCross=MFC2 と 1:1。ABI 不変。詳細は modules/cgal/manifest.cpp の同構造を参照。
 * ★ キャッシュキーソルトの歴史: pigModuleRegistry の init が id 2 決め打ちで "\x01MFM" を
 *   持っていた (registry の manifold 予約) → rev4 Phase D で .so の自己申告へ → #3427 で記述子へ →
 *   ★ #3466 (ABI v17) で **記述子からも撤去**し、レジストリが名前 + .so 指紋から作る形に落ち着いた。 */
/* ★ #3427: 型登録・ソルト申告の静的自己登録は撤去。型は記述子 (provides) から
 *   pigModuleRegistry::register_descriptor が登録する。
 * ★ #3466 (ABI v17): キャッシュキーのソルトは **記述子から撤去**した。レジストリが
 *   「モジュール名 + その .so の指紋」から作る (申告するものではなくなった)。 */

SRAVA_MODULE_EXPORT const srava_module_descriptor* srava_module(void)
{
	return &mfatsAgent_descriptor;
}
