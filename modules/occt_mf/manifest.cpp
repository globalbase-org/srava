/*
 * modules/occt_mf/manifest.cpp — occt_mf.so の唯一の C エクスポート (akira-project #3452)。
 * 記述子本体 (make_agent + OPS) は ocmtsAgent.cpp が持つ (単一ソース)。ここは公開点だけ。
 */
#include	"pig/c++/pigModule.h"

extern const srava_module_descriptor ocmtsAgent_descriptor;   /* ocmtsAgent.cpp */

SRAVA_MODULE_EXPORT const srava_module_descriptor* srava_module(void)
{
	return &ocmtsAgent_descriptor;
}
