/*
 * modules/d2/manifest.cpp — d2.so の唯一の C エクスポート (rev4 次元分担デモ・d3/manifest.cpp のミラー)。
 *   記述子本体は d2atsAgent.cpp が持つ (単一ソース)。この TU は d2.so にだけリンクする。
 */
#include "pig/c++/pigModule.h"
#include "pig/c++/pigTypeRegistry.h"
#include "pig/c++/pigModuleRegistry.h"

extern const srava_module_descriptor d2atsAgent_descriptor;   /* d2atsAgent.cpp */

/* ★ #3427: 型登録・ソルト申告の静的自己登録は撤去。記述子 (codecs の tags×out_types と
 *   hash_salt) から pigModuleRegistry::register_descriptor が登録する。 */

extern "C" const srava_module_descriptor* srava_module(void)
{
	return &d2atsAgent_descriptor;
}
