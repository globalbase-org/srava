/*
 * modules/demo/manifest.cpp — demo.so の唯一の C エクスポート (docs §3.1・第3モジュール実証)。
 *   記述子本体 (make_agent + OPS) は dematsAgent.cpp が持つ。この TU は demo.so にだけリンクする。
 */
#include "pig/c++/pigModule.h"

extern const srava_module_descriptor dematsAgent_descriptor;   /* dematsAgent.cpp */

SRAVA_MODULE_EXPORT const srava_module_descriptor* srava_module(void)
{
	return &dematsAgent_descriptor;
}
