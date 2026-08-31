/*
 * modules/occt/manifest.cpp — occt.so の唯一の C エクスポート (#3437 P5)。
 */
#include	"pig/c++/pigModule.h"

extern const srava_module_descriptor octsAgent_descriptor;   /* octsAgent.cpp */

SRAVA_MODULE_EXPORT const srava_module_descriptor* srava_module(void)
{
	return &octsAgent_descriptor;
}
