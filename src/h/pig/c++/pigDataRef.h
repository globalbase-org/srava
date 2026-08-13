/*
 * pigDataRef — D_REF(外部ファイル参照レコード)の pigData 表現 (#3406, 2026-07-31 メモ 2.)。
 *
 * pigDataCache::body は 3 種を区別して保持する必要がある:
 *   - D_CHUNK 系 : cgMesh / mfGeom …… agent の処理対象データ (元クラスが pigData 派生)
 *   - D_TEXT 系  : 上記以外の pigData (スカラ/配列/hash。serialize テキストで保存)
 *   - D_REF 系   : 外部ファイルへの参照。**pigData で表せる型が無かった** ← ここを埋める
 *
 * 表現 (ひさ案):
 *   pigDataPair("D_REF", pigDataHash{ ref_kind, path, size, mtime, content_hash })
 *
 * これにより export の calcBody は「他の演算子と同じように結果 pigData を返す」だけでよくなり、
 * キャッシュ書き込みは agent の set_body → ptsDataCache → pigCacheCodec が WriterRef を選ぶ、
 * という **WriterText と同じ一本道**に載る (calc 内で writer を起こす escape hatch が不要)。
 *
 * car に置くタグ文字列がそのまま codec の match 条件 (pigRefCacheCodec.cpp)。
 */
#ifndef ___pigDataRef_H___
#define ___pigDataRef_H___

#include	"pig/c++/pigData.h"

/* pigDataPair の car に置く判別タグ。キャッシュファイル側の 4CC "REF " と対。 */
#define	PIG_DREF_TAG	"D_REF"

/* ref_kind の値 (catalog §7: 1=INPUT 2=OUTPUT)。 */
enum { PIG_DREF_INPUT = 1, PIG_DREF_OUTPUT = 2 };

/* 構築。path は必須 (thNULL は空文字列扱い)。 */
sPtr<pigData> pig_data_ref_make(int kind, sPtr<stdString> path,
                                INTEGER64 size, INTEGER64 mtime, pHashKeyType chash);

/* 判定。body が D_REF 表現なら 1。 */
int pig_data_ref_is(sPtr<pigData> body);

/* 取り出し。D_REF 表現でなければ 0 を返し出力は触らない。出力ポインタは不要なら 0 可。
 * NB: content_hash は u64 だが pigDataInteger(=INTEGER64) に収めているので、
 *     最上位ビットが立つ値は負数として往復する (ビットパターンは保存される)。 */
int pig_data_ref_get(sPtr<pigData> body, int *kind, sPtr<stdString> *path,
                     INTEGER64 *size, INTEGER64 *mtime, pHashKeyType *chash);

#endif
