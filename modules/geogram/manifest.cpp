/*
 * modules/geogram/manifest.cpp — geogram.so の唯一の C エクスポート (#3435 P3)。
 * 記述子本体 (make_agent + OPS) は ggtsAgent.cpp が持つ (単一ソース)。ここは公開点だけ。
 */
#include	"pig/c++/pigModule.h"

extern const srava_module_descriptor ggtsAgent_descriptor;   /* ggtsAgent.cpp */

SRAVA_MODULE_EXPORT const srava_module_descriptor* srava_module(void)
{
	return &ggtsAgent_descriptor;
}
