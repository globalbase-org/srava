/*
 * modules/openvdb_cg/manifest.cpp — openvdb_cg.so の唯一の C エクスポート (#3434・2026-08-22)。
 * 記述子本体 (make_agent + OPS) は vctsAgent.cpp が持つ (単一ソース)。ここは公開点だけ。
 */
#include	"pig/c++/pigModule.h"

extern const srava_module_descriptor vctsAgent_descriptor;   /* vctsAgent.cpp */

SRAVA_MODULE_EXPORT const srava_module_descriptor* srava_module(void)
{
	return &vctsAgent_descriptor;
}
