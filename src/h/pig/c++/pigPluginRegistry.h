#ifndef ___pigPluginRegistry_H___
#define ___pigPluginRegistry_H___

/* pigPluginRegistry — プラグイン op の登録簿(pig 層・プロセス global・immutable = read-once config)。
 *
 * planner が起動時に load() でマニフェスト(*.plugin)を読み込み、parser(mk_call)が is_op() で op 名を
 * 受理判定し、pigfPluginAgent::agent_cmd() が op_bin() で起動バイナリを得る。
 *
 * 探索路(後勝ち = 後のディレクトリが同名 op を上書き):
 *   1. $PIG_PLUGIN_PATH (':' 区切り複数可)
 *   2. ~/.config/srava/plugins/
 *   3. PIG_PLUGIN_SYSDIR (= $PREFIX/share/srava/plugins、compile 時 define)
 *
 * マニフェスト形式(行ベース・'#' コメント・1 ファイル 1 op):
 *   op   = <name>
 *   bin  = <absolute path>
 *   out  = value | mesh
 *   args = <arity>            # 任意(検証用・現状未使用)
 *
 * 「read-once config」なので process global で持つ(可変 per-planner 状態ではない=マルチプランナ安全)。 */

/* マニフェストを読み込む(冪等。2 回目以降は no-op)。戻り = 登録 op 数。 */
int         pigplugin_registry_load();

/* op 名がプラグイン op として登録済みか(1/0)。未 load なら自動で load する。 */
int         pigplugin_is_op(const char *op);

/* op の起動バイナリ絶対パス(未登録は 0)。 */
const char* pigplugin_op_bin(const char *op);

/* op の出力が mesh か(1=mesh / 0=value)。未登録は 0。 */
int         pigplugin_op_out_mesh(const char *op);

#endif
