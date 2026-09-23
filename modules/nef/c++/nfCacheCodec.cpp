/*
 * nfCacheCodec — Nef カーネルのキャッシュコーデック定義 TU (#3433 P1)。
 * descriptor.codecs (nf_codecs) を extern 公開し、ローダが owner=nef id で登録する。
 * cgCacheCodec.cpp / mfCacheCodec.cpp のミラー。
 *
 * ★★ #3559: **この TU はモジュール側** (nef_snc.so / nef_hybrid.so) に残る。
 *   表の中身が変種ごとに違う (自分の型名 1 本) ためで、幾何ライブラリ (libsrava_cg) には
 *   持てない。⇒ WIRE の *定義* と reader/writer の生成子は幾何ライブラリ側 (nfWire.cpp) に
 *   在り、ここはそれを **並べるだけ**。
 */
#include	"pig/c++/pigCacheCodec.h"
#include	"pig/c++/pigModule.h"
#include	"pig/c++/pigData.h"
#include	"nf/c++/nfMesh.h"

/* ★ 2026-08-28 (ひさ設計・ABI v16): このモジュールが提供するもの。
 *   1 行 = (本体クラス階層, その階層について名乗る型名, 扱う 4CC)。
 *   ⚠ **types と tags は位置対応しない** (独立した 2 本・個数も一致しない)。どのタグがどの型に
 *     なるかは申告せず、wire->create に通して訊く (pigModule.h の pigModuleType 参照)。
 *   ⚠ tags は **診断専用** — 読めるかを答えるのは wire->create 一本で、この欄は
 *     `srava --module-info` が列挙するための候補にすぎない (実行時の判断に使わない)。 */
extern const pigModuleType nef_provides[];
const pigModuleType nef_provides[] = {
	{ &NF_MESH::WIRE, NF_TYPE,
	  NF_TAG "," NF_OTHER_TAG ",MESH,MFM3" },
	{ 0, 0, 0 },
};
