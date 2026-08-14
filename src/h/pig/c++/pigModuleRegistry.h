#ifndef ___pigModuleRegistry_H___
#define ___pigModuleRegistry_H___
/*
 * pigModuleRegistry — モジュール (幾何カーネル / 解析プラグイン) の一元レジストリ **ハブ**。
 *   docs/agent_so_design.md の設計 + #3427 ③ (レジストリの app 所有化・2026-08-13)。
 *
 * ★ #3427 ③: 旧「namespace + プロセス全体の可変 static テーブル」を廃し、stdObject 派生の
 *   **1 クラス**にした。実体は **ptsApplication が INI_ptsObject_START で thNEW し所有する**
 *   (`ptsApp->module_registry`)。これで
 *     - プロセス内に複数 app (マルチプランナ) が同居してもレジストリが混ざらない (リエントラント)
 *     - PE (Windows) のイメージ跨ぎ static 複製問題の影響面が消える
 *   参照経路は 3 通り:
 *     - pts 系オブジェクト (TS_STATE 内): `ptsApp->module_registry->...`
 *     - 素の pigData 層 (TS_STATE から呼ばれる): `pig_current_registry()` (sCallSection TLS 経由)
 *     - app を起こさない main (--modules 診断 / probe): ローカルに thNEW して使う
 *
 * サブレジストリは**直接メンバ**で所有する (ユーザ設計案):
 *   types    — 型軸 (型名 ↔ 4CC)                       (旧 pigTypeRegistry namespace)
 *   agents   — KERNEL 名 → in-proc 実行体生成子         (旧 pigAgentRegistry namespace)
 *   codecs   — キャッシュ reader/writer 表              (旧 pigCacheCodec namespace)
 *   backends — 起動方式 (thread/process) → Mediator     (旧 pigExecBackend namespace)
 *   vparser  — 値パーサ生成子 (言語パーサ)              (旧 pigValueParser namespace)
 * ロード記録 (旧 pigModuleLoader の g_log/g_dirs) もここが持つ。
 * 登録経路は register_descriptor の 1 本 (#3427 ①) + ctor の組込 (pig-ref codec / exec backend)。
 *
 * ★ K4 (hash_salt): 同一 op でもカーネルが違えば結果 byte が異なるため、キャッシュキーに
 *   弁別ソルトを混ぜる。**基準カーネル (混ぜない) は登録しない** → その op のキャッシュキーは
 *   従来のまま byte 不変。非基準カーネルだけ salt を持つ (manifold="\x01MFM")。
 */
#include "ts2/c++/stdObject.h"         /* 基底 (thNEW で app 所有にするため) */
#include "pig/c++/pigData.h"           /* pigDataCacheHelperFn (pdc フック・旧 g_pdcHelperFn) */
#include "pig/c++/pigTypeRegistry.h"
#include "pig/c++/pigAgentRegistry.h"
#include "pig/c++/pigCacheCodec.h"
#include "pig/c++/pigExecBackend.h"
#include "pig/c++/pigValueParser.h"
#include "pig/c++/pigModuleLoader.h"   /* pigModuleLoadEvent / pigModuleSearchDir (ロード記録の型) */

#include <string>
#include <vector>

struct srava_module_descriptor;   /* 完全型は pigModule.h (global scope) */

/* ★ #3427 ③: ptsApplication の INI が実行するモジュールロードの指示 (ctor 引数)。 */
#define PIG_MODLOAD_NONE    0   /* ロードしない (テスト / smoke) */
#define PIG_MODLOAD_SEARCH  1   /* 探索路を走査 (planner・RTLD_LAZY) */
#define PIG_MODLOAD_FILE    2   /* 単一 .so (agent / probe・RTLD_NOW) */

class pigModuleRegistry : public stdObject {
public:
	pigModuleRegistry();   /* 組込登録 (pig-ref codec。backends は自 ctor で thread/process) */

	/* ---- サブレジストリ (直接メンバ所有) ---- */
	pigTypeRegistry   types;
	pigAgentRegistry  agents;
	pigCacheCodec     codecs;
	pigExecBackend    backends;
	pigValueParser    vparser;

	/* codecs.reader_for_tag は自型解決に types が要るのでハブが合成する (呼び側はこれを使う)。 */
	pigCacheReaderFn  reader_for_tag(const unsigned char tag[4]) const
		{ return codecs.reader_for_tag(tag, types); }

	/* ---- pigDataCache の I/O helper 生成フック (旧 pigData.cpp の g_pdcHelperFn) ----
	 * pig 静的層 (pigDataCache) は codegen クラス (ptsDataCache) へ直依存できないためフック経由。
	 * ptsApplication の INI が ptsDataCache_helper() を登録する。未登録 (単体テスト) は
	 * pigDataCache が「素の入れ物」として振る舞う (従来のフォールバックと同じ)。 */
	void                 set_pdc_helper(pigDataCacheHelperFn f) { pdcHelper_ = f; }
	pigDataCacheHelperFn pdc_helper() const { return pdcHelper_; }

	/* ---- モジュール名 ↔ id ----
	 * "delayed"(=id 0 = MODULE_NONE 番兵) だけ予約。cgal/manifold は記述子駆動で登録される。 */
	/* モジュール名 → id (登録順・0 起点)。既存名は既存 id を返す (冪等)。 */
	int         register_module(const char *name);
	/* 名前 → id。未登録 = -1。 */
	int         id_of_name(const char *name) const;
	/* id → 名前。範囲外は "delayed" (= id 0 の名前)。 */
	const char* name_of_id(int id) const;
	/* 登録済みモジュール数 ("delayed" を含む)。 */
	int         count() const;

	/* ---- 記述子 (srava_module_descriptor) の登録とクエリ ----
	 * 記述子を登録 (name で module id を確定/取得し、メタ部を保存)。後勝ち (docs §1.3)。
	 * ★ #3427 ①: agent (make_agent) / 型 (types×type_tags) / codec / salt もここで一括登録
	 *   (登録経路 1 本 = 先勝ち/後勝ち不統一 #3425 の構造的解消)。 */
	void        register_descriptor(const srava_module_descriptor *d);
	/* module id の記述子 (未登録 = 0)。 */
	const srava_module_descriptor* descriptor(int module_id) const;

	/* module id がその op を持つか。ops 未登録 (= 万能フォールバック扱い) は **-1 (不明)**。 */
	int         supports_op(int module_id, const char *op) const;
	/* 登録済みのどれかのカーネルがその op を持つか (1/0)。mk_call の generic 受理用。 */
	int         any_supports_op(const char *op) const;
	/* module の op の型シグネチャ (pigOpEntry.sig)。未登録/未注釈 = 0。 */
	const char* op_sig(int module_id, const char *op) const;
	/* op の出力型 (mesh=1 / value=0 / どの記述子にも無い=-1)。generic 受理の out_cache 決定用。 */
	int         op_out_is_mesh(const char *op) const;
	/* module id の exec_caps (未登録 = 0)。 */
	unsigned    exec_caps(int module_id) const;
	/* module id が拡張子を import/export できるか。exts 未登録は -1 (不明・万能扱い)。 */
	int         can_import_ext(int module_id, const char *ext) const;
	int         can_export_ext(int module_id, const char *ext) const;
	/* module id の実効 priority (agent() 上書きがあればそれ・無ければ記述子・未登録 = 0)。 */
	int         priority(int module_id) const;
	/* module id の実効 exec_default (EXEC_THREAD/EXEC_PROCESS。agent() 上書き優先・未登録 = 0)。 */
	int         exec_default(int module_id) const;
	/* 言語 agent(so,{priority/exec_default}) からの上書き (docs §2.4)。
	 * set_priority は「今ロードした扱い」= 後勝ちの tie-break も更新する。 */
	void        set_priority(int module_id, int p);
	void        set_exec_default(int module_id, int exec);

	/* agent("so","off"/"on") 実行時無効化。disabled = codec/descriptor は生きるが候補選択から除外。 */
	void        set_enabled(int module_id, bool on);
	bool        is_enabled(int module_id) const;
	/* 既定カーネル = 登録済みモジュールの **priority 最大** (同値は後勝ち)。無ければ ""。 */
	const char* default_module_name() const;

	/* キャッシュキー弁別ソルト (K4)。 */
	void        register_hash_salt(int module_id, const char *salt);
	/* module_id のソルト (無ければ 0 = 基準カーネル)。 */
	const char* hash_salt(int module_id) const;

	/* ---- .so 動的ロード (旧 pigModuleLoader・docs §3.1) ----
	 * .so を 1 個ロードして自分へ配線する。成功で記述子 (.so 内の静的寿命) を返す。失敗は 0 で、
	 * err != 0 なら理由を格納する。成功時は dlclose しない (登録した実行体が生き続けるため)。
	 *   lazy=false: RTLD_NOW  — 未解決シンボルを即解決 (agent 側・fail-fast)。
	 *   lazy=true : RTLD_LAZY — 関数解決を呼び出し時まで遅延 (planner 側・メタ取得のみ)。 */
	const srava_module_descriptor* load_file(const char *path, std::string *err, bool lazy = false);
	/* 探索路 (docs §1.3) を走査して *.so をロードする (planner 起動時・RTLD_LAZY)。
	 * 走査順 (後勝ち = 下ほど強い): sysdir < user config < exe dir < $SRAVA_MODULE_PATH。
	 * 同名 (同ファイル名) は勝者だけ dlopen (2026-08-13・#3425)。返り値 = 成功数。 */
	int         load_search_path(std::string *err);
	/* load_file の試行記録 (時系列・成功/失敗とも)。`srava --modules` 診断用。 */
	const std::vector<pigModuleLoadEvent>& load_log() const { return log_v; }
	/* load_search_path が走査した dir の記録 (順序 = 走査順)。 */
	const std::vector<pigModuleSearchDir>& search_dirs() const { return dirs_v; }

private:
	/* ---- モジュール表 (旧 static テーブル → 直接メンバ・ユーザ設計案) ---- */
	std::vector<std::string>  names_v;     /* id → 名前 ({"delayed"} 起点) */
	std::vector<std::string>  salts_v;     /* id → キャッシュキーソルト (K4) */
	std::vector<const srava_module_descriptor*> descs_v;   /* id → 記述子 (疎) */
	/* agent(so,{...}) 上書き (descriptor は .so 内 const で変更できない)。
	 * sentinel: prio=INT_MIN / exec=-1 (=未設定)。seq = 登録/上書きの通し番号 (後勝ち tie-break)。 */
	std::vector<int>          prioOvr_v;
	std::vector<int>          execOvr_v;
	std::vector<long>         seq_v;
	long                      seqN_;
	std::vector<char>         enabled_v;   /* id → 有効フラグ (疎・既定 = 有効) */
	pigDataCacheHelperFn      pdcHelper_;  /* pigDataCache の helper 生成子 (旧 g_pdcHelperFn) */
	/* ---- ロード記録 (旧 pigModuleLoader::g_log/g_dirs) ---- */
	std::vector<pigModuleLoadEvent> log_v;
	std::vector<pigModuleSearchDir> dirs_v;

	void ensure_ovr(int id);
	/* loader 内部 (pigModuleLoader.cpp で実装) */
	const srava_module_descriptor* load_file_impl(const char *path, std::string *err,
	                                              bool lazy, bool *not_a_module);
	void record(const char *path, const srava_module_descriptor *d,
	            const std::string &err, bool not_a_module);
	void record_shadowed(const std::string &path, const std::string &winner);
	void scan_dir(const char *dir, const char *origin,
	              std::vector<std::pair<std::string,std::string> > *out);
};

class ptsApplication;

/* ★ worker スレッド用の app スコープ (TLS・RAII)。
 *   ts2Parallel の _fn は「親チェーンを遡れば app に届く」保証がない (途中の worker が FIN 済みで
 *   parent を手放していると鎖が切れる — BLH2 cold cache の export_vox 障害の真因)。そこで
 *   **_fn を張る側 (pigfAgent / pigfMap = ptsApp を持つ)** が _fn 冒頭でこのスコープを張り、
 *   スレッドローカルに「今の app」を宣言する。sCallSection と同類の TLS = リエントラント安全。
 *   スコープ寿命中のみ有効 (app の生存は _fn キャプチャの sPtr が保証する)。 */
class pigAppScope {
public:
	pigAppScope(const sPtr<ptsApplication>& app);
	~pigAppScope();
private:
	ptsApplication *prev_;
};

/* 「今の app」: ①pigAppScope の TLS 上書き → ②sCallSection caller の parent 遡り、の順で解決。
 *   無ければ thNULL。実装は ptsApplication.cpp。 */
sPtr<ptsApplication> pig_current_app();

/* ★ 「今の caller が属す app のレジストリ」= pig_current_app()->module_registry。
 *   素の pigData 層 (pigDataCache / pigDataOperatorModule / パーサ helper 等) が TS_STATE /
 *   worker _fn の実行中に呼ぶ。app 未解決 / registry 未生成なら thNULL
 *   (呼び側は「未登録」フォールバックへ)。実装は ptsApplication.cpp (完全型が要るため)。 */
sPtr<pigModuleRegistry> pig_current_registry();

#endif
