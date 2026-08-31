/*
 * modules/openvdb_mf/manifest.cpp — openvdb_mf.so の唯一の C エクスポート (#3434・2026-08-22)。
 * 記述子本体 (make_agent + OPS) は vmtsAgent.cpp が持つ (単一ソース)。ここは公開点だけ。
 */
#include	"pig/c++/pigModule.h"

extern const srava_module_descriptor vmtsAgent_descriptor;   /* vmtsAgent.cpp */

SRAVA_MODULE_EXPORT const srava_module_descriptor* srava_module(void)
{
	return &vmtsAgent_descriptor;
}
