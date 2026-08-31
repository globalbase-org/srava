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
 * ★ #3439: **モジュール由来の派生テーブルを全廃**した (types / agents / codecs / salt)。
 *   検索は「読み込んだ記述子 1 本」を **is_enabled(id) を見ながら**走査する。これで
 *   @module("foo.so","off")@ が「最初から foo.so をロードしなかった場合」と同じ挙動になる
 *   (派生状態が無いので「チェックし忘れ」が構造的に起きえない)。組込 codec (D_REF) も
 *   「常に on の組込記述子 "pig"」として同じ経路に乗る。
 *   なお値キャッシュ ("TEXT") は codec ではなく ptsDataCache の既定分岐 = モジュール由来でない。
 *
 * 残るサブレジストリは**組込専用**のものだけ (モジュール由来でないので漏れない):
 *   backends — 起動方式 (thread/process) → Mediator     (旧 pigExecBackend namespace)
 *   vparser  — 値パーサ生成子 (言語パーサ)              (旧 pigValueParser namespace)
 * ロード記録 (旧 pigModuleLoader の g_log/g_dirs) もここが持つ。
 * 登録経路は register_descriptor の 1 本 (#3427 ①)。
 *
 * ★ K4 (hash_salt): 同一 op でもカーネルが違えば結果 byte が異なるため、キャッシュキーに
 *   弁別ソルトを混ぜる。**基準カーネル (混ぜない) は登録しない** → その op のキャッシュキーは
 *   従来のまま byte 不変。非基準カーネルだけ salt を持つ (manifold="\x01MFM")。
 */
#include "ts2/c++/stdObject.h"         /* 基底 (thNEW で app 所有にするため) */
#include "pig/c++/pigData.h"           /* pigDataCacheHelperFn (pdc フック・旧 g_pdcHelperFn) */
#include "pig/c++/pigAgentRegistry.h"
#include "pig/c++/pigCacheCodec.h"
#include "pig/c++/pigExecBackend.h"
#include "pig/c++/pigValueParser.h"
#include "pig/c++/pigModuleLoader.h"   /* pigModuleLoadEvent / pigModuleSearchDir (ロード記録の型) */

#include <string>
#include <vector>

struct srava_module_descriptor;   /* 完全型は pigModule.h (global scope) */
struct pigOpEntry;                /* 完全型は pigOpEntry.h (★ #3436 P4 §6.2 の op_entry) */

/* ★ #3427 ③: ptsApplication の INI が実行するモジュールロードの指示 (ctor 引数)。 */
#define PIG_MODLOAD_NONE    0   /* ロードしない (テスト / smoke) */
#define PIG_MODLOAD_SEARCH  1   /* 探索路を走査 (planner・RTLD_LAZY) */
#define PIG_MODLOAD_FILE    2   /* 単一 .so (agent / probe・RTLD_NOW) */

/* ★ #3439 ⑦: 記述子の自己矛盾を検出する (空文字 = 問題なし)。load_file が ABI 不一致と同じく
 * ロードを拒否するのに使う。いまの規則: import/export op を持つなら対応する exts の申告が必須。 */
std::string pig_descriptor_violation(const srava_module_descriptor *d);

/* ★ 2026-08-28 (ひさ設計): **非幾何型のリストは libpig に 1 本だけ持つ**。
 *   非幾何型 = 計算の行き先にならない型 (値キャッシュ `value` / 外部ファイル参照 `ref`)。
 *   これは全モジュール共通・planner と agent でも共通なので、モジュールごとに申告させる理由が無い。
 *   ここが唯一の定義で、planner / agent / probe すべてがこれを見る (増やすときもここだけ)。 */
extern const char *const pig_nongeometric_types[];   /* { "value", "ref", 0 } */
int  pig_type_is_nongeometric(const char *name);

class pigModuleRegistry : public stdObject {
public:
	pigModuleRegistry();   /* 組込登録 (pig-ref codec。backends は自 ctor で thread/process) */

	/* ---- サブレジストリ (直接メンバ所有) ---- */
	/* ★ #3439 ②: 型軸は **記述子 1 本**へ。旧 pigTypeRegistry (派生テーブル) を廃止し、
	 *   記述子の types×type_tags を module id ごとに展開して持ち、検索は **on/off を見ながら**
	 *   その並びを走査する。これで @module(so,"off")@ が「最初からロードしなかった場合」と
	 *   同じ挙動になる (派生状態が無いので「チェックし忘れ」が構造的に起きえない)。
	 *   性能上の懸念は無い: 旧実装も線形走査 + CSV パースで、エントリ数は 10 前後。 */
	pigExecBackend    backends;
	pigValueParser    vparser;

	/* ---- in-proc 実行体の生成子 (記述子走査・**無効モジュールは飛ばす**・#3439 ④) ----
	 * module が空/0 のときは「make_agent を持つ有効なモジュールがちょうど 1 つならそれ」
	 * (agent プロセスは planner が計画した .so を 1 本だけ積むので、その 1 本を引く経路)。 */
	pigAgentFactory   agent_factory(const char *module) const;

	/* ★ #3419 §7: モジュール全体の初期化を **1 回だけ** 走らせる。
	 * そのモジュールの最初の agent が起きるときに ptsMediator が呼ぶ。
	 * 2 度目以降は何もしない。**TS_STATE 内から呼ばれるので排他は不要**
	 * (tinyState の状態機械は app-mutex 下で直列化される)。
	 * 「初期化済み」フラグは **app 所有のこのレジストリ**が持つ (可変 static を作らない・#3427)。 */
	void              ensure_initialized(const char *module);
	/* ★ 2026-08-28 (ABI v12): reader_for (4CC × 型名で reader を引く) は **撤去**した。
	 *   reader は本体クラス階層に 1 本しかなく (pigWireClass::mkReader)、op の引数配線が
	 *   直接それを起こすので、型名で codec 表を引き直す必要が無くなった。 */
	/* ★ 2026-08-19: この 4CC を **読めるモジュールが出せる型を全部** CSV で集める (有効なモジュールのみ)。
	 *   4CC → 型は 1:1 ではない (複数モジュールが同じ形式を読めるし、昇格読みもある) ので、
	 *   「先勝ちで 1 つ返す」逆引きは嘘になる。列挙して見せるのが正しい (旧 type_of_tag は撤去)。
	 *   返り値 = 見つかった個数。 */
	int               types_readable_from_tag(const unsigned char tag[4], std::string &out) const;
	pigCacheWriterFn  writer_for_body(sPtr<pigData> body) const;
	/* 本文が codec で書ける「ストリーム本体」か (= 値ではない)。writer_for_body の薄い述語。 */
	int               is_stream_body(sPtr<pigData> body) const
		{ return ( writer_for_body(body) != 0 ) ? 1 : 0; }

	/* ---- 型軸クエリ ----
	 * ★ 2026-08-28 (ひさ設計): 正引き tag_of_type (型 → 4CC) は **撤去**した。呼び手が 1 つ
	 *   (cast の routing エラー文) しか残っておらず、そこは planner が形式に踏み込む場所では
	 *   なかったため — in-proc の値はまだメモリ上の body でしかなく、4CC は pigDataCache の
	 *   都合。形式が要る診断は読む側 (ptsGenericAgent / reader_for) が出す。
	 *   逆引き type_of_tag は 2026-08-19 に撤去済み (形式は複数モジュールが共有してよいので
	 *   1 つ返す API は必ず嘘をつく → types_readable_from_tag で全部列挙する)。 */
	int         type_is_known(const char *name) const;
	/* ★ そのモジュールが **持てる (= 書ける) 型** を out へ積む (有効なモジュールのみ・返り値 = 個数)。
	 *   agent の「消費できる型リスト」に使う。判定は codecs のうち **writer を持つ行** の types
	 *   ＝「自分で書ける型 = 自分の body クラスがある型」。読取専用の foreign 行 (cgal の
	 *   cg-mf-upgrade 等) は「読めるが持てない」ので入らない。 */
	/* ★ 2026-08-28: types_of_module は **撤去**した。呼び手は srava_module_probe だけで、
	 *   probe は記述子を手に持っているので codecs を直接読めばよい (名前の引き直しと
	 *   is_enabled の 2 段が意味を持っていなかった)。 */

	/* ---- pigDataCache の I/O helper 生成フック (旧 pigData.cpp の g_pdcHelperFn) ----
	 * pig 静的層 (pigDataCache) は codegen クラス (ptsDataCache) へ直依存できないためフック経由。
	 * ptsApplication の INI が ptsDataCache_helper() を登録する。未登録 (単体テスト) は
	 * pigDataCache が「素の入れ物」として振る舞う (従来のフォールバックと同じ)。 */
	/* ★ 2026-08-28 (ひさ設計): モジュールを **本当にアンロードする** (dlclose)。
	 *   module(so,"off") がこれを呼ぶ。以降 module(so,{}) で再ロードできる。
	 *   ⚠ **一度でも使われたモジュールは落とせない**。.so の中身を指すオブジェクト
	 *     (本体クラスの実体・agent) が生まれる入口は make_agent 一つで、その直前に必ず
	 *     ensure_initialized が initDone_v を立てる。だから initDone_v が判定に足りる
	 *     (生存オブジェクトの登録簿も参照カウントも要らない)。
	 *   成功=1 / 拒否=0 (err に理由)。 */
	int                  unload_module(int module_id, std::string *err);

	/* ★ このモジュールで仕事をしたことを記録する (以後アンロード不可)。 */
	void                 mark_used(int module_id);

	void                 set_pdc_helper(pigDataCacheHelperFn f) { pdcHelper_ = f; }
	pigDataCacheHelperFn pdc_helper() const { return pdcHelper_; }

	/* ---- モジュール名 ↔ id ----
	 * "delayed"(=id 0 = MODULE_NONE 番兵) だけ予約。cgal/manifold は記述子駆動で登録される。 */
	/* モジュール名 → id (登録順・0 起点)。既存名は既存 id を返す (冪等)。 */
	int         register_module(const char *name);
	/* 名前 → id。未登録 = -1。 */
	int         id_of_name(const char *name) const;

	/* ★ モジュール専用の大域データの置き場 (ひさ設計 2026-08-26)。
	 *
	 * ★なぜ要るか: モジュール実装に **可変な file-scope static を置いてはいけない**。
	 *   モジュールは in-proc (EXEC_THREAD) で走りうる = 1 プロセスに複数 op が同居するので、
	 *   op ごとに違ってよい値をプロセス大域に置くと混線する (test/srava_no_mutable_static.sh)。
	 *   一方で「**そのモジュールにひとつ**」で正しい状態 (ライブラリの設定値など) は実在する。
	 *   その置き場をここに一本用意し、モジュールは stdObject 派生に詰めて預ける。
	 *
	 * ★ 引き方 (モジュール側): 素の幾何クラスは ptsObject 派生ではないので ptsApp に届かないが、
	 *   **pig_current_registry()** が sCallSection の TLS から app → registry を辿ってくれる。
	 *   ⇒ ABI (記述子) を変えずにモジュールから到達できる。
	 *   id は id_of_name("<自分の名前>") で引く。
	 * ⚠ 呼び出し文脈が無い (caller が取れない) と registry が thNULL になる。モジュールは
	 *   その場合 **「設定が無い」と同じ扱い**へ落ちること (勝手に既定を変えない)。 */
	/* ★ module==0/"" なら「make_agent を持つ有効なモジュールが 1 本だけ」の構成でそれを指す
	 * (agent プロセスは .so が 1 本なので名前を知らなくても引ける)。pig_current_module_id が使う。 */
	int  resolve_single_or_named(const char *module) const;

	/* ★ いま configure(opts) を呼んでいる相手のモジュール id (呼び出し中のみ有効・他は -1)。
	 * 記述子の configure は素の関数ポインタで自分の id を知らないので、ここから引く。 */
	int             configuring_module_id() const { return configuringId_; }

	void            set_module_data(int module_id, sPtr<stdObject> d);
	sPtr<stdObject> module_data(int module_id) const;
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
	/* ★ その記述子を**実際に供給した .so のパス**(組込登録など由来が無ければ "")。
	 * `srava --modules` の「有効なパス」表示はこれを使う。以前はロード記録を名前で後ろから
	 * 検索していたが、それは「同名を供給するファイルが複数あると、どれが有効か推測になる」
	 * (Redmine #3425 ①)。登録した瞬間に出所を控えておけば推測が要らない。 */
	const char*                    descriptor_path(int module_id) const;

	/* module id がその op を持つか。ops 未登録 (= 万能フォールバック扱い) は **-1 (不明)**。 */
	int         supports_op(int module_id, const char *op) const;
	/* 登録済みのどれかのカーネルがその op を持つか (1/0)。mk_call の generic 受理用。 */
	int         any_supports_op(const char *op) const;
	/* module の op の型シグネチャ (pigOpEntry.sig)。未登録/未注釈 = 0。 */
	const char* op_sig(int module_id, const char *op) const;
	/* ★ #3436 P4 §6.2: module の op 表の 1 行そのもの (in[]/nin/variadic を planner から見る)。
	 *   未登録 = 0。sig が **型**を持つのに対し、こちらは全引数の **種別と個数**を持つ。 */
	const pigOpEntry* op_entry(int module_id, const char *op) const;
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
	/* ★ #3436 P4: module id の実効 **N'** (1 ノードあたり受け取りたい最大項数・policy)。
	 *   module(so,{arity:k}) の上書き優先 → 記述子の arity → **既定 2**。docs/sig_grammar_design.md §5.4。 */
	int         arity(int module_id) const;
	void        set_arity(int module_id, int k);
	/* ★ #3441 (ひさ設計 2026-08-26): module(so,{opts}) の**ハッシュ全体**を保持する。
	 * exec_default/priority/arity のような個別の上書きテーブルとは別に、opts をそのまま
	 * 持っておいて各モジュールの configure() へ渡す。未設定は thNULL。 */
	void          set_opts(int module_id, sPtr<pigData> opts);
	sPtr<pigData> opts_for(int module_id) const;
	/* ★ その module の記述子の configure(opts_for(id)) を呼ぶ。module==0/"" は
	 * ensure_initialized(0) と同じ引き方 (agent プロセスのように make_agent を持つ有効な
	 * モジュールが 1 本だけの構成で「その 1 本」を指す)。opts が無くても configure が
	 * あれば thNULL を渡して呼ぶ (モジュール側で「未設定なら何もしない」を判断させる)。
	 * 記述子が configure を持たなければ何もしない。 */
	void          apply_opts(const char *module);
	/* ★ #3441: agent プロセス側 (C_ENV 受信) の便利口。module==0 解決で opts を保存してから
	 * configure() を呼ぶ (set_opts + apply_opts を 1 回で)。 */
	void          set_and_apply_opts(const char *module, sPtr<pigData> opts);
	/* ★ #3436 P4: op が **可換**か (どれかの有効モジュールが commutative=1 を申告していれば 1)。
	 *   キャッシュキー正規化 (pigDataOperator::normalize) と木の形の決定が共有する唯一の根拠。
	 *   op 名のハードコード (strcmp("union")…) を全廃するための入口。 */
	int         op_commutative(const char *op) const;

	/* agent("so","off"/"on") 実行時無効化。disabled = codec/descriptor は生きるが候補選択から除外。 */
	/* 既定カーネル = 登録済みモジュールの **priority 最大** (同値は後勝ち)。無ければ ""。 */
	const char* default_module_name() const;

	/* キャッシュキー弁別ソルト (K4)。記述子 (hash_salt) を直読みし、無効モジュールは 0
	 * (#3439 ④: 派生テーブル salts_v を廃止)。 */
	const char* hash_salt(int module_id) const;

	/* ---- .so 動的ロード (旧 pigModuleLoader・docs §3.1) ----
	 * .so を 1 個ロードして自分へ配線する。成功で記述子 (.so 内の静的寿命) を返す。失敗は 0 で、
	 * err != 0 なら理由を格納する。成功時は dlclose しない (登録した実行体が生き続けるため)。
	 *   lazy=false: RTLD_NOW  — 未解決シンボルを即解決 (agent 側・fail-fast)。
	 *   lazy=true : RTLD_LAZY — 関数解決を呼び出し時まで遅延 (planner 側・メタ取得のみ)。
	 * ★ **ロード済みなら何もしない** (冪等)。ただし同じファイル名で **別の実ファイル**を指した
	 *   場合は 0 を返して拒否する (1 モジュール名につき dlopen は 1 回 = #3425 の不変条件。
	 *   黙って先勝ちにすると「指定したのに効いていない」になる)。conflict != 0 ならこの衝突か
	 *   どうかを書き戻す — 呼び手が「見つからない」と区別して扱えるようにするため
	 *   (module(so,{optional:1}) が飲み込んでよいのは前者だけ)。 */
	const srava_module_descriptor* load_file(const char *path, std::string *err, bool lazy = false,
	                                         bool *conflict = 0);
	/* ★ #3431: **区切りを含まないモジュール名** ("nef_snc.so") を探索路上の実ファイルへ解決する。
	 *   見つからなければ与えられた文字列をそのまま返す (dlopen 側の明示エラーに委ねる)。
	 *   load_file が内部で通すので、通常は呼び手が意識する必要はない。 */
	std::string resolve_module_file(const char *path) const;
	/* ★ 既に**ロード済み**のモジュールを .so のファイル名で引き当てる (id・無ければ -1)。
	 *   ロードは起動時の探索路走査で済んでいるので、module()/load() は普通これに当たる。
	 *   ファイル名で引くのは、ローダが「1 ファイル名につき dlopen は 1 回」を不変条件に
	 *   しているため (#3425)。パス付きで渡されても末尾成分だけを見る。 */
	int         id_of_loaded_file(const char *path) const;
	/* 探索路 (docs §1.3) を走査して *.so をロードする (planner 起動時・RTLD_LAZY)。
	 * 走査順 (後勝ち = 下ほど強い): sysdir < exe 相対 install < user config < exe dir < $SRAVA_MODULE_PATH。
	 * 同名 (同ファイル名) は勝者だけ dlopen (2026-08-13・#3425)。返り値 = 成功数。 */
	int         load_search_path(std::string *err);
	/* ★ #3452: load_search_path の**列挙だけ**(dlopen しない)。dirs_v を埋めるので
	 *   resolve_module_file()/module() の名前解決(探索路引き)はこれだけで機能する。
	 *   起動時コストは dlopen (静的初期化含む) がほぼ全てなので、これは実質ゼロコスト。
	 *   planner 起動時はこちらを使う (実ロードは script の module() 呼び出しに委ねる)。
	 *   `srava --modules` 診断は引き続き load_search_path() (dlopen まで) を使う。 */
	void        enumerate_search_path();
	/* load_file の試行記録 (時系列・成功/失敗とも)。`srava --modules` 診断用。 */
	const std::vector<pigModuleLoadEvent>& load_log() const { return log_v; }
	/* load_search_path が走査した dir の記録 (順序 = 走査順)。 */
	const std::vector<pigModuleSearchDir>& search_dirs() const { return dirs_v; }

private:
	/* ---- モジュール表 (旧 static テーブル → 直接メンバ・ユーザ設計案) ---- */
	/* ★ #3439 ②: 記述子の types×type_tags を id ごとに展開したもの (派生テーブルではなく
	 *   **モジュールの持ち物**)。検索時に is_enabled(id) を見るので off なら見えない。 */

	std::vector<std::string>  names_v;     /* id → 名前 ({"delayed"} 起点) */
	std::vector<const srava_module_descriptor*> descs_v;   /* id → 記述子 (疎) */
	std::vector<int>                           initDone_v;  /* ★ #3419 §7: id → initialize 済みか */
	/* ★ 2026-08-28 (ひさ設計): id → dlopen ハンドル (0 = 組込 / unload 済み)。
	 *   以前は成功時にハンドルを捨てていた (意図的リーク) が、module(so,"off") を
	 *   実アンロードにするため保持する。 */
	std::vector<void*>                         handle_v;
	/* ★ 2026-08-28 (ひさ設計): id → **このモジュールで実際に仕事をしたか**。
	 *   立つのは 2 箇所: ensure_initialized (in-proc / agent プロセスで実行体を起こす直前) と、
	 *   planner が外部 agent へ .so を託す確定点 (pigfModuleAgent の descriptor_path 取得)。
	 *   ★ initDone_v では足りない — exec_default=PROCESS のモジュールは planner 側で
	 *     ensure_initialized を通らないので、cgal を使った後でも落とせてしまっていた。 */
	std::vector<char>                          used_v;
	std::vector<std::string>  descPath_v;   /* id → その記述子を供給した .so のパス (#3425 ①) */
	std::string               loadingPath_; /* load_file が register_descriptor に渡す出所 (内部) */
	/* agent(so,{...}) 上書き (descriptor は .so 内 const で変更できない)。
	 * sentinel: prio=INT_MIN / exec=-1 (=未設定)。seq = 登録/上書きの通し番号 (後勝ち tie-break)。 */
	std::vector<int>          prioOvr_v;
	std::vector<int>          execOvr_v;
	/* ★ id → モジュール専用の大域データ (set_module_data/module_data)。中身の型は
	 * モジュール側の stdObject 派生で、registry は**中身を知らない**(ただの預かり所)。 */
	std::vector<sPtr<stdObject> > data_v;
	int                       configuringId_;   /* configure 呼び出し中のモジュール id (他は -1) */
	std::vector<int>          arityOvr_v;   /* ★ #3436 P4: module(so,{arity:k}) の上書き (0=未設定) */
	std::vector<sPtr<pigData> > optsOvr_v;  /* ★ #3441: module(so,{opts}) のハッシュ全体 (疎・未設定=thNULL) */
	std::vector<long>         seq_v;
	long                      seqN_;
	/* ★ 2026-08-28 (ひさ指摘): enabled_v / set_enabled / is_enabled は **撤去**した。
	 *   module(so,"off") が実アンロード (dlclose) になり、フラグを倒す呼び手が
	 *   1 つも無くなったため — is_enabled は常に真を返す死んだ条件だった。 */
	pigDataCacheHelperFn      pdcHelper_;  /* pigDataCache の helper 生成子 (旧 g_pdcHelperFn) */
	/* ---- ロード記録 (旧 pigModuleLoader::g_log/g_dirs) ---- */
	std::vector<pigModuleLoadEvent> log_v;
	std::vector<pigModuleSearchDir> dirs_v;

	void ensure_ovr(int id);
	/* ★ #3441: 「module==0/"" なら make_agent を持つ有効なモジュールが 1 本だけの構成でそれを
	 * 指す」という解決を 1 箇所にまとめたもの (ensure_initialized/apply_opts/
	 * set_and_apply_opts が共有)。見つからなければ -1。 */
	/* loader 内部 (pigModuleLoader.cpp で実装) */
	const srava_module_descriptor* load_file_impl(const char *path, std::string *err,
	                                              bool lazy, bool *not_a_module);
	void record(const char *path, const srava_module_descriptor *d,
	            const std::string &err, bool not_a_module);
	void record_shadowed(const std::string &path, const std::string &winner);
	void scan_dir(const char *dir, const char *origin,
	              std::vector<std::pair<std::string,std::string> > *out);
	/* ★ #3452: load_search_path/enumerate_search_path 共有の列挙部 (パス 1)。dirs_v を埋め、
	 *   見つかった候補 (未 dlopen) を *cands へ積む。 */
	void enumerate_dirs(std::vector<std::pair<std::string,std::string> > *cands);
};

class ptsApplication;

/* 「今の app」: sCallSection caller の parent 遡りで解決 (caller が ptsObject でなくても
 *   生きている親を遡って最初の ptsObject の ptsApp)。無ければ thNULL。実装は ptsApplication.cpp。
 *   ※ ts2Parallel worker からも有効: tinyState 側修正 (develop-v2 b601451) で spawn worker の
 *   親は常に _root (全 worker 終了まで生存保証) になり、FIN 済み worker で鎖が切れることは
 *   なくなった (旧 pigAppScope TLS 回避は撤去済)。 */
sPtr<ptsApplication> pig_current_app();

/* ★ 「今の caller が属す app のレジストリ」= pig_current_app()->module_registry。
 *   素の pigData 層 (pigDataCache / pigDataOperatorModule / パーサ helper 等) が TS_STATE /
 *   worker _fn の実行中に呼ぶ。app 未解決 / registry 未生成なら thNULL
 *   (呼び側は「未登録」フォールバックへ)。実装は ptsApplication.cpp (完全型が要るため)。 */
sPtr<pigModuleRegistry> pig_current_registry();
/* ★ いま走っている op のモジュール id (無ければ -1)。実装は ptsApplication.cpp。
 * in-proc は caller 鎖の ptsMediatorInternal から、agent プロセスは「唯一のモジュール」から。 */
int                     pig_current_module_id();

#endif
