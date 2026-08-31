/*
 * modules/cgal/manifest.cpp — cgal.so の唯一の C エクスポート (.so 化 Phase 3b・docs §3.1)。
 * 記述子本体 (make_agent + OPS) は cgatsAgent.cpp が持つ (単一ソース)。ここは公開点だけ。
 * この TU は cgal.so にだけリンクし、静的実行体 (srava_agent) には入れない → srava_module
 * シンボルが実行ファイルに現れず衝突しない (§8 フォールバック並走の前提)。
 */
#include "pig/c++/pigModule.h"

extern const srava_module_descriptor cgatsAgent_descriptor;   /* cgatsAgent.cpp */

/* ★ rev4 Phase A: cgal が I/O する実装型を型軸レジストリへ登録 (dlopen 時の静的初期化)。
 *   pig 層にハードコードせず kernel .so が申告する (rev4 思想)。cgMesh3D=MESH / cgMesh2D=PLY2 と 1:1。
 *   ABI 不変 (descriptor は無改変。型シグネチャの descriptor 化は Phase B)。 */
/* ★ #3427: 型登録・ソルト申告の静的自己登録は撤去。記述子 (codecs の tags×types と
 *   hash_salt) から pigModuleRegistry::register_descriptor が登録する。 */

SRAVA_MODULE_EXPORT const srava_module_descriptor* srava_module(void)
{
	return &cgatsAgent_descriptor;
}
