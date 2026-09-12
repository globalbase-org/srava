/*
 * modules/nef_mf/manifest.cpp — nef_mf.so の唯一の C エクスポート (akira-project #3499)。
 * 記述子本体 (make_agent + OPS) は nfmtsAgent.cpp が持つ (単一ソース)。ここは公開点だけ。
 */
#include	"pig/c++/pigModule.h"

extern const srava_module_descriptor nfmtsAgent_descriptor;   /* nfmtsAgent.cpp */

SRAVA_MODULE_EXPORT const srava_module_descriptor* srava_module(void)
{
	return &nfmtsAgent_descriptor;
}
