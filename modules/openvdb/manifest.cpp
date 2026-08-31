/*
 * modules/openvdb/manifest.cpp — openvdb.so の唯一の C エクスポート (#3434 P2)。
 * 記述子本体 (make_agent + OPS) は vdtsAgent.cpp が持つ (単一ソース)。ここは公開点だけ。
 */
#include	"pig/c++/pigModule.h"

extern const srava_module_descriptor vdtsAgent_descriptor;   /* vdtsAgent.cpp */

SRAVA_MODULE_EXPORT const srava_module_descriptor* srava_module(void)
{
	return &vdtsAgent_descriptor;
}
