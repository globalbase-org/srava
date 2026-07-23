#ifndef ___pigPluginSDK_H___
#define ___pigPluginSDK_H___

/* pigPluginSDK — プラグインエージェント用の最小 SDK(pig 層・host 提供)。
 *
 * プラグイン作者は **compute(op, args) -> result** だけ書けばよい。pigwire の handshake・引数の値
 * デコード・結果の値エンコード・キャッシュファイル書き出しは全て serve() が担う。
 *   - CGAL も cgMesh も srava 言語パーサも、tinyState の状態機械もリンクしない。
 *   - リンクは pig(pigData/pigValueCodec) + libtinyState(sPtr) + pigwire.h(ヘッダ) のみ。
 *   - 各エージェントプロセスは **1 リクエストを処理して終了**(planner が op ごとに 1 プロセス起動)。
 *
 * 値の受け渡しは pigData(数=pigDataInteger/Float, 文字列=pigDataString, 配列=pigDataArray,
 * ハッシュ=pigDataHash)。args[i] を get_flt()/get_ix()/length() 等で読み、結果を同じ型で組んで返す。
 * エラーは pigDataError を返す(serve が A_ERROR としてプランナへ伝える)。
 *
 * 使い方(プラグインの main):
 *   #include "pig/c++/pigPluginSDK.h"
 *   static sPtr<pigData> my_compute(const char* op, sArray<sPtr<pigData> >& args) { ... }
 *   int main(){ return pigplugin::serve(&my_compute); }
 */
#include "pig/c++/pigData.h"
#include "ts2/c++/sArray.h"

namespace pigplugin {

/* op 名と引数(idx 順に詰めた配列)を受け取り、結果 pigData を返す。エラーは pigDataError。 */
typedef sPtr<pigData> (*ComputeFn)(const char *op, sArray<sPtr<pigData> >& args);

/* stdin/stdout で 1 リクエストを処理(pigwire)。プロセス終了コードを返す(0=正常 / 1=エラー)。 */
int serve(ComputeFn compute);

} /* namespace pigplugin */

#endif
