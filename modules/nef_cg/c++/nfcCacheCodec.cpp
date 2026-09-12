/*
 * nfcCacheCodec — nef_cg.so のキャッシュコーデック (akira-project #3499)。
 *   ocmCacheCodec.cpp (occt_mf) のミラー。
 *
 * ★ このモジュールは **自分の型を持たない**が、process 実行では agent プロセスに
 *   nef_cg.so しか load されないので、**入力 (NEF3) を読み、出力 (MESH) を書く**
 *   codec を自分で申告する必要がある。
 * ★★ 実体は **libsrava_nf_snc / libsrava_cg の公開クラスをそのまま使う** —
 *   新しい wire 形式もクラスも作らない。
 */
#include	"pig/c++/pigCacheCodec.h"
#include	"pig/c++/pigModule.h"
#include	"pig/c++/pigData.h"
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/ptsApplication.h"
#include	"nf/c++/nfMesh.h"
#include	"cg/c++/cgMesh.h"

/* ★ 型 → wire の対応は各共有ライブラリの WIRE が持つ。ここは「何を扱うか」の申告だけ。
 *   ⚠ **types と tags は位置対応しない** (独立した 2 本)。tags は診断専用で、読めるかを
 *     答えるのは wire->create 一本 (pigModule.h の pigModuleType 参照)。 */
extern const pigModuleType nef_cg_provides[];
const pigModuleType nef_cg_provides[] = {
	/* 入力: nef_snc の SNC。★ NEFB (hybrid) は名乗らない — 実体が別ライブラリなので
	 *   受けても d_cast が通らない (nfctsAgent.cpp の冒頭参照)。 */
	{ &nfGeom::WIRE, "nf-mesh3d", "NEF3" },
	/* 出力: cg の厳密境界。 */
	{ &cgMesh::WIRE, "cg-mesh3d", "MESH" },
	{ 0, 0, 0 },
};
