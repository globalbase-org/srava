/*
 * modules/cherchi/manifest.cpp — cherchi.so の唯一の C エクスポート (#3438 P6)。
 * 記述子本体 (make_agent + OPS) は chtsAgent.cpp が持つ (単一ソース)。ここは公開点だけ。
 */
#include	"pig/c++/pigModule.h"

extern const srava_module_descriptor chtsAgent_descriptor;   /* chtsAgent.cpp */

/* ★ SRAVA_MODULE_EXPORT = extern "C" + エクスポート属性 (MinGW の dllexport / ELF の
 * visibility("default"))。Windows で occt.dll だけ引けなかった件の対処として導入されたので、
 * 新しいモジュールもこのマクロを使う (ピア 5368acb・2026-08-27)。 */
SRAVA_MODULE_EXPORT const srava_module_descriptor* srava_module(void)
{
	return &chtsAgent_descriptor;
}
