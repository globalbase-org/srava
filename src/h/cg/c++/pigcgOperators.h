#ifndef ___pigcgOperators_H___
#define ___pigcgOperators_H___

/* pigcgOperators — srava(CGAL processor)固有の演算子ノード。命名規約(coding_plan.txt):
 *   pigcg... = 非 tinyState 系で pigData を祖先とするクラス(プランナ/言語固有)。
 * ここに置くのは srava 言語の **I/O シンク**(export / export_async / flush)。これらは「ファイルへ
 * 書き出す」「未完了 export を待つ」という srava アプリ固有の機能で、汎用データ層(pigData)の関心事
 * ではないため分離する(以前は pigData.h にクラス宣言だけ同居していた・#3366 の名残)。
 *   - _start() の実体は cgptsPlanner.cpp(srava アプリ層)。export 系は planner の async export
 *     レジストリ(caller_planner 経由)を叩くため、planner と同じ翻訳単位で定義する。
 *   - print は「値の表示」で言語非依存寄りなので pig 側(pigDataOperatorPrint)に残す。 */

#include	"pig/c++/pigData.h"   /* 基底 pigDataOperator / pigInfo / sPtr / copy_to */

/* export(x) — ルート観測点。継続("delayed".promise)を実値まで解決して result へ(get_int 等が
 * agent 計算の完了を待てるようにする)。書き出し agent op 自体は pigDataFunction<pigfModuleAgent>。 */
class pigcgOperatorExport : public pigDataOperator {
public:
  pigcgOperatorExport(sPtr<pigInfo> i = thNULL) : pigDataOperator(i) {}
  virtual sPtr<pigData> clone() { return copy_to(thNEW(pigcgOperatorExport,())); }
protected:
  virtual void _start();
};

/* (export_async / print_async の専用 op は撤去 — どちらも grammar で async へ desugar され、
 *  下記 pigcgOperatorAsync(統一プリミティブ)に畳まれた。docs/srava_async_design.md §5。) */

/* async { body...; sync: S } — 並列な制御文。body を非ブロッキングに起動して即 null を返し、
 * planner の syncTail チェーン(発行順)へ繋ぐ。実体は tinyState helper pigfAsync。
 * args は [body0, body1, ..., (sync)]。get_mode()==1 のとき末尾が sync 文。_start は cgptsPlanner.cpp。
 * print_async / export_async / par を畳む統一プリミティブ(docs/srava_async_design.md)。 */
class pigcgOperatorAsync : public pigDataOperator {
public:
  pigcgOperatorAsync(sPtr<pigInfo> i = thNULL) : pigDataOperator(i), op_mode(0) {}
  virtual sPtr<pigData> clone() {
    sPtr<pigcgOperatorAsync> n = thNEW(pigcgOperatorAsync,());
    n->set_mode(get_mode());   /* hasSync フラグを複製 */
    return copy_to(n);
  }
  void set_mode(int m) { op_mode = m; }   /* 1=末尾文が sync 文 */
  int  get_mode()      { return op_mode; }
protected:
  virtual void _start();
  int op_mode;
};

/* flush() — 未完了の export_async をその地点で全部待つ明示バリア(file を観測する system()/import() の
 * 直前にプログラマが置く。srava は依存を知り得ないので自動化しない)。 */
class pigcgOperatorFlush : public pigDataOperator {
public:
  pigcgOperatorFlush(sPtr<pigInfo> i = thNULL) : pigDataOperator(i) {}
  virtual sPtr<pigData> clone() { return copy_to(thNEW(pigcgOperatorFlush,())); }
protected:
  virtual void _start();
};

#endif
