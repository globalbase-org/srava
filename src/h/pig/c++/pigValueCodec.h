#ifndef ___pigValueCodec_H___
#define ___pigValueCodec_H___

/* pigValueCodec — pig 層の「値」シリアライズ往復(pig 値コーデック)。
 *
 * encode 方向は既存の pigData::serialize()(pig 層・pigData.cpp)。本ヘッダはその **対になる
 * decode**(テキスト→pigData)を pig 層に置く。従来 decode は srava の言語パーサ(cgptsLemonParser
 * VALUE モード)にしか無く「serialize は pig なのに parser は srava」という非対称があった。
 * プラグインエージェント機構(pig 層)は srava 言語に依存せず値を往復させたいので、ここに最小の
 * 再帰下降パーサを置く。文法 = serialize() の出力サブセット:
 *   null | <int> | <float(小数点/指数つき)> | "string(\\ \" \n \t エスケープ)" | [v,..] | {"k":v,..}
 *
 * srava 言語の VALUE 文法(変数参照・式・厳密有理数等)は扱わない(それは srava の仕事)。値だけ。
 * malformed は pigDataError を返す(末尾に余分なトークンがあってもエラー)。 */

#include "pig/c++/pigData.h"

/* text 全体を 1 個の値として parse。成功=その pigData / 失敗=pigDataError。 */
sPtr<pigData> pig_value_parse(const char *text);

#endif
