/*
 * modules/nef_cg/manifest.cpp — nef_cg.so の唯一の C エクスポート (akira-project #3499)。
 * 記述子本体 (make_agent + OPS) は nfctsAgent.cpp が持つ (単一ソース)。ここは公開点だけ。
 */
#include	"pig/c++/pigModule.h"

extern const srava_module_descriptor nfctsAgent_descriptor;   /* nfctsAgent.cpp */

SRAVA_MODULE_EXPORT const srava_module_descriptor* srava_module(void)
{
	return &nfctsAgent_descriptor;
}
