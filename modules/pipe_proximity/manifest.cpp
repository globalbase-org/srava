/*
 * plugins/pipe_proximity/manifest.cpp — pipe_proximity.so の唯一の C エクスポート
 *   (.so 化 Phase 5・docs §3.1)。記述子本体 (make_agent + OPS) は ppatsAgent.cpp が持つ。
 *   この TU は pipe_proximity.so にだけリンクし、process 版 (pipe_proximity_agent) には入れない。
 */
#include "pig/c++/pigModule.h"

extern const srava_module_descriptor ppatsAgent_descriptor;   /* ppatsAgent.cpp */

SRAVA_MODULE_EXPORT const srava_module_descriptor* srava_module(void)
{
	return &ppatsAgent_descriptor;
}
