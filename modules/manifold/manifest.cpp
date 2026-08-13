/*
 * modules/manifold/manifest.cpp — manifold.so の唯一の C エクスポート (.so 化 Phase 3b・docs §3.1)。
 * 記述子本体 (make_agent + OPS) は mfatsAgent.cpp が持つ (単一ソース)。ここは公開点だけ。
 * この TU は manifold.so にだけリンクし、静的実行体 (srava_agent_mf / planner) には入れない。
 */
#include "pig/c++/pigModule.h"
#include "pig/c++/pigTypeRegistry.h"
#include "pig/c++/pigModuleRegistry.h"   /* rev4 Phase D: salt 自己申告 (register_module/register_hash_salt) */

extern const srava_module_descriptor mfatsAgent_descriptor;   /* mfatsAgent.cpp */

/* ★ rev4 Phase A: manifold が I/O する実装型を型軸レジストリへ登録 (dlopen 時の静的初期化)。
 *   mfMesh=MFM3 / mfCross=MFC2 と 1:1。ABI 不変。詳細は modules/cgal/manifest.cpp の同構造を参照。
 * ★ rev4 Phase D: キャッシュキーソルトも manifold.so が自己申告 (旧: pigModuleRegistry の
 *   init が id 2 決め打ちで s[2]="\x01MFM" を持っていた = registry の manifold 予約)。これで pig 層から
 *   manifold の名指し/id 決め打ちが消える。値 "\x01MFM" は既存 manifold キャッシュとの byte 互換用。 */
/* ★ #3427: 型登録・ソルト申告の静的自己登録は撤去。記述子 (codecs の tags×out_types と
 *   hash_salt) から pigModuleRegistry::register_descriptor が登録する。 */

extern "C" const srava_module_descriptor* srava_module(void)
{
	return &mfatsAgent_descriptor;
}
