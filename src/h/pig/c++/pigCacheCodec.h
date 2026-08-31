/*
 * pigCacheCodec — キャッシュ codec の関数ポインタ型 (旧: reader/writer テーブル (#3406, 2026-07-29 メモ 3.)。
 *
 * ★ #3427 ③: 旧 namespace + 可変 static テーブルを廃し、**素の値クラス**にした。
 *   実体は pigModuleRegistry (ハブ) が直接メンバ `codecs` として所有し、ハブは
 *   ptsApplication が INI で thNEW する。登録は pigModuleRegistry::register_descriptor
 *   (記述子 codecs 表・#3427 ①) と pig-ref (ハブ ctor の組込登録) の経路のみ。
 *
 *   - reader 選択: キャッシュファイル先頭の D_META 4CC タグ ("MESH"/"PLY2"=cg, "MFM3"/"MFC2"=mf)。
 *     D_TEXT レコード (値キャッシュ) はコーデック外の既定 = ptsDataCache が ReaderText を使う。
 *   - writer 選択: set_body された本文 pigData の型 (match)。どれにも合わなければ既定 =
 *     WriterText (serialize) = 値キャッシュ。
 */
#ifndef ___pigCacheCodec_H___
#define ___pigCacheCodec_H___

#include	"ts2/c++/sPtr.h"
#include	"ts2/c++/stdString.h"

#include	<string>
#include	<vector>

class tinyState;
class ptsObject;
class pigData;

/* reader/writer 生成子。parent = ptsDataCache (イベントの受け手)。
 * 生成された reader は TSE_RETURN の msg_obj で本文 pigData を返す約束 (既存 ReaderMesh と同じ)。
 * writer は TSE_ASSERT (メタ書込済) → TSE_RETURN (全書込完了) を上げる約束 (既存 WriterMesh と同じ)。 */
typedef sPtr<tinyState> (*pigCacheReaderFn)(sPtr<ptsObject> parent, sPtr<stdString> path);
typedef sPtr<tinyState> (*pigCacheWriterFn)(sPtr<ptsObject> parent, sPtr<stdString> path, sPtr<pigData> body);
typedef int             (*pigCacheMatchFn)(sPtr<pigData> body);

/* ★ #3439 ⑤: class pigCacheCodec (派生テーブル) は削除した。reader/writer の検索は
 * pigModuleRegistry が **記述子 (descriptor.codecs) を is_enabled を見ながら走査**して行う
 * (reader_for / writer_for_body)。このヘッダは上の関数ポインタ型
 * (pigModuleCodec / モジュール側の codec 定義が使う) だけを提供する。 */

#endif
