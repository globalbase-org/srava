/*
 * nfmCacheCodec — nef_mf.so のキャッシュコーデック (akira-project #3499)。
 *   nfcCacheCodec.cpp (nef_cg) / ocmCacheCodec.cpp (occt_mf) のミラー。
 *
 * ★ このモジュールは **自分の型を持たない**が、process 実行では agent プロセスに
 *   nef_mf.so しか load されないので、**入力 (NEF3) を読み、出力 (MFM3) を書く**
 *   codec を自分で申告する必要がある。
 */
#include	"pig/c++/pigCacheCodec.h"
#include	"pig/c++/pigModule.h"
#include	"pig/c++/pigData.h"
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/ptsApplication.h"
#include	"nf/c++/nfMesh.h"
#include	"mf/c++/mfMesh.h"

extern const pigModuleType nef_mf_provides[];
const pigModuleType nef_mf_provides[] = {
	/* 入力: nef_snc の SNC。★ NEFB (hybrid) は名乗らない (nfmtsAgent.cpp 冒頭参照)。 */
	{ &nfGeom::WIRE, "nf-mesh3d", "NEF3" },
	/* 出力: Manifold の raw double mesh。 */
	{ &mfGeom::WIRE, "mf-mesh3d", "MFM3" },
	{ 0, 0, 0 },
};
