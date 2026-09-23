/*
 * modules/points/manifest.cpp — points.so の唯一の C エクスポート (#3528)。
 *   記述子本体 (make_agent + OPS) は pttsAgent.cpp が持つ (単一ソース)。ここは公開点だけ。
 */
#include	"pig/c++/pigModule.h"

extern const srava_module_descriptor pttsAgent_descriptor;   /* pttsAgent.cpp */

SRAVA_MODULE_EXPORT const srava_module_descriptor* srava_module(void)
{
	return &pttsAgent_descriptor;
}
