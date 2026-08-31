/*
 * modules/nef/manifest.cpp — nef.so の唯一の C エクスポート (#3433 P1)。
 * 記述子本体 (make_agent + OPS) は nftsAgent.cpp が持つ (単一ソース)。ここは公開点だけ。
 * 型登録・hash_salt は記述子 (types/type_tags/hash_salt) から
 * pigModuleRegistry::register_descriptor が行う (#3427)。
 */
#include	"pig/c++/pigModule.h"

extern const srava_module_descriptor nftsAgent_descriptor;   /* nftsAgent.cpp */

/* ★ visibility=hidden でビルドするので、**公開点だけ**を明示 export する (#3433 2 変種化)。
 * 同一ソースから nef_snc.so / nef_hybrid.so を作るため、内部シンボルを隠さないと
 * RTLD_GLOBAL で衝突し、後から読まれた方が先の記述子を返してしまう。 */
SRAVA_MODULE_EXPORT const srava_module_descriptor* srava_module(void)
{
	return &nftsAgent_descriptor;
}
