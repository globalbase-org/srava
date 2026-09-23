/*
 * modules/geomutils/manifest.cpp — geomutils.so の唯一の C エクスポート (#3527)。
 *   記述子本体 (make_agent + OPS) は gutsAgent.cpp が持つ (単一ソース)。ここは公開点だけ。
 */
#include	"pig/c++/pigModule.h"

extern const srava_module_descriptor gutsAgent_descriptor;   /* gutsAgent.cpp */

SRAVA_MODULE_EXPORT const srava_module_descriptor* srava_module(void)
{
	return &gutsAgent_descriptor;
}
