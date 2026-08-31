#ifndef ___pigModule_H___
#define ___pigModule_H___
/*
 * pigModule — モジュール (幾何カーネル / 解析プラグイン) の自己申告記述子。
 *   docs/agent_so_design.md §2.1。.so 化 Phase 2 (2026-08-08) で導入。
 *
 * Phase 2 は静的リンクのまま: 各モジュールがこの記述子を **静的初期化で** pigModuleRegistry へ
 *   登録する (dlopen 化は Phase 3 で、.so が srava_module() を extern "C" 公開する)。
 *
 * ★ 記述子の「メタ部」(name/priority/exec_caps/ops/exts) は planner が decide_out_module /
 *   agent_module_name で参照する。「実行体部」(make_agent) は in-proc カーネル (manifold) では
 *   planner にリンクされ populate されるが、External 専用カーネル (cgal) では 0 でよい
 *   (planner は cgal の make_agent を呼ばない = agent プロセスが持つ)。
 *   codecs は Phase 3 で追加 (現状は pigCacheCodec への個別 register_codec のまま)。
 */
#include "pig/c++/pigOpEntry.h"        /* pigOpEntry (ops 表) */
#include "pig/c++/pigAgentRegistry.h"  /* pigAgentFactory (make_agent) */
#include "pig/c++/pigCacheCodec.h"     /* pigCacheReaderFn/WriterFn/MatchFn (codecs 表・Phase 4③') */

/* ★ 2026-08-28 (ひさ設計・ABI v16): **1 行 = (本体クラス階層, そのモジュールが名乗る型名)**。
 *   旧 pigModuleCodec (自由文字列の name + types) と descriptor.wires (階層の一覧) を統合した。
 *
 *   ★ 統合できた理由: v15 まで削った結果、codec 表の実体は「持ち込む階層ごとに名乗る型名」に
 *     なっており、wires と同じ粒度だった (15 モジュール中 14 が行数一致。唯一ずれていた openvdb は
 *     **到達不能な申告の残骸**で、それを外したら揃った)。
 *   ★ 旧 name は自由文字列で、型名と紛らわしいだけで誰も使っていなかった (番兵と表示ラベルのみ)。
 *     識別は wire (クラスそのもの) が担うので、名前を別に書く必要が無い。
 *   ★ 型名が **階層でなくモジュールに属する**のは必要な非対称: mfGeom について manifold は
 *     "mf-mesh3d,mf-cross2d" を名乗り、occt_mf は "mf-mesh3d" だけを名乗る。 */
struct pigModuleType {
	const pigWireClass* wire;    /* 本体クラス階層 (Cls::WIRE)。**0 = 配列終端** */
	const char*         types;   /* このモジュールがこの階層について名乗る型名の CSV */
	/* ⚠⚠ **types と tags は位置対応しない**。独立した 2 本のリストで、個数も一致しない。
	 *   例 (cgal): types = "cg-mesh3d,cg-cross2d" (2 個) / tags = "MESH,PLY2,MFM3,MFC2,NEFB" (5 個)。
	 *     実際の対応は多対少 — MESH/MFM3/NEFB → cg-mesh3d、PLY2/MFC2 → cg-cross2d。
	 *   ★ **どのタグがどの型になるかは申告しない**。答えるのは wire->create に通して返ってきた
	 *     具象の type_name() だけ (`srava --module-info` が出しているのはその実測結果)。
	 *   ⚠ ABI v10〜v13 の tags/types は **位置対応だった**ので、当時を知っているほど誤読しやすい。
	 *     位置対応をやめたのは、1 つの階層が多くの 4CC を読む一方で名乗る型は少ないため
	 *     (昇格読み・降格読みを行で分けていた頃の名残が位置対応だった)。 */
	/* ★ 2026-08-28 (ひさ設計): この階層が扱う 4CC の CSV。**診断専用**。
	 *   ⚠⚠ **実行時の判断に使ってはならない**。読めるかどうかを答えるのは
	 *     wire->create (create_for_meta) 一本で、この欄はその答えを**列挙するための候補**でしかない。
	 *     v10〜v13 の tags は reader 選択を駆動しており、申告と実装がずれると**黙って**誤った
	 *     reader が選ばれた。v14 で撤去したのはそのため。
	 *   ★ 今回は判断経路から切り離したうえで戻す。`srava --module-info` が各タグを
	 *     wire->create に通して**実際に受理するか確かめる**ので、ずれは黙って残らず表に出る。
	 *   0 可 (診断で列挙されなくなるだけ)。 */
	const char*         tags;
};

/* 起動方式の能力 bitmask (docs §2.2)。今回実装は THREAD/PROCESS のみ。REMOTE は枠。 */
enum {
	EXEC_THREAD  = 1 << 0,   /* planner 内 thread (ptsMediatorInternal) */
	EXEC_PROCESS = 1 << 1,   /* agent プロセス (ptsMediatorExternal) */
	EXEC_REMOTE  = 1 << 2,   /* 将来: リモートノード */
};

#define SRAVA_MODULE_ABI 16   /* 記述子 ABI 版。dlopen 時に不一致なら拒否。v3: codecs 追加 (Phase 4③')。
                              * v4: pigOpEntry に型シグネチャ sig 追加 (rev4 Phase B・型ディスパッチ)。
                              * v5: read_foreign_tags 撤去 (rev4 sig 化・クロスカーネル受理は op sig の foreign
                              *     入力型で表現するようになり、read_foreign_tags→coercion/can_read_module が不要に)
                              * v6: hash_salt 追加 (#3427)。.so 側の静的自己登録
                              *     (register_agent / register_type / register_hash_salt) を廃し、
                              *     すべて記述子から loader/registry が登録する形へ統一。
                              * v7: 負荷コントロール (#3419) — thread_kind / sigma / initialize を追加。
                              * v11: **types / type_tags を撤去** (ひさ設計 2026-08-28)。所有型は codecs の writer 行から導く。
                              * v12: **pigOpEntry.mkCalc を配線ポインタ wiring へ置換** (ひさ設計 2026-08-28)。
                              *     op の cache 引数が「どの本体クラスとして実体化されるか」を OPS 行が申告する
                              *     (OPWIRE)。消費型をモジュール単位の一覧から導くのをやめた — writer を持たない
                              *     検査専用 / reader を持たない生成専用 / codec を持たないモジュールが現に成立し、
                              *     codec 表からも sig からも消費型は導けないため。★ops 表の**レイアウトが変わる**ので版必須。
                              * v13: **codec 表から match/mkReader/mkWriter を撤去し、記述子に wires を追加**
                              *     (ひさ設計 2026-08-28)。3 つとも本体クラス階層に帰属していた。codec 表に
                              *     残るのは名前だけ (4CC ↔ 型名 ↔ writes)。★両方のレイアウトが変わるので版必須。
                              * v14: **codec 行の tags と記述子の codec_tags を撤去** (ひさ設計 2026-08-28)。
                              *     4CC を表から引く経路が全て無くなり、読み手がゼロになった。codec 表は
                              *     { name, types, writes } = **型名の登録簿**に一本化。★レイアウト変更につき版必須。
                              * v15: **codec 行の writes と読み専用の行そのものを撤去** (ひさ指摘 2026-08-28)。
                              *     表は { name, types } だけ。読み専用行は情報を足しておらず、writes は
                              *     wires 側の mkWriter の写し (= 二重申告) だった。★レイアウト変更につき版必須。
                              * v16: **codecs と wires を pigModuleType 1 本に統合** (ひさ設計 2026-08-28)。
                              *     旧 codec 行の自由文字列 name は撤去 (識別は wire = クラスそのもの)。
                              *     ★レイアウト変更につき版必須。
                              * ★ v8: **thread_kind / sigma_x100 / sigma_at_n を撤去** (ひさ判断 2026-08-21)。
                              *     実測で「申告に基づくスレッド配分」が目的を達しないと分かったため
                              *     (docs/srava_load_control_design.md §12.7 / §12.8)。initialize は残す。
                              *     ⚠ **フィールドを削るので版を上げる** — v7 記述子をそのまま読むと
                              *     initialize の位置がずれる (v7 は未公開なので実害は無いが、規律として)。
                              * ★ v9: arity (N') 追加 (#3436 P4)。0 = 未指定 = 2。
                              * ★ v10: **configure** 追加 (#3441・ひさ設計 2026-08-26)。
                              *     module(so,{opts}) のハッシュ**全体**をモジュールへ渡すフック。
                              *     geogram の内部スレッド数固定 (nproc 決め打ち) が発端。
                              *     initialize (プロセスに1度・引数なし) とは役割が違う: configure は
                              *     opts が設定/更新されるたびに呼ばれる (docs/srava_load_control_design.md
                              *     §17.6 / docs/srava_module_design.md)。 */

struct srava_module_descriptor {
	int               abi_version;   /* = SRAVA_MODULE_ABI */
	const char*       name;          /* "cgal" / "manifold" / "pipe_proximity" */
	int               priority;      /* 既定カーネル選択順 (大=優先・同値は後勝ち)。plugin は 0 */

	/* --- 実行体 --- */
	pigAgentFactory   make_agent;    /* in-proc 実行体生成子。External 専用カーネルは 0 可 */
	unsigned          exec_caps;     /* EXEC_* の bitmask */
	int               exec_default;  /* 既定の起動方式 (EXEC_THREAD / EXEC_PROCESS) */

	/* --- I/O 能力 (空/0 可) --- */
	const pigOpEntry* ops;    int n_ops;  /* 対応 op 表 (= 実行体の OPS[] を再エクスポート) */
	const char*       import_exts;   /* import で読める拡張子 CSV ("stl,off")。0 可 */
	const char*       export_exts;   /* export で書ける拡張子 CSV。0 可 */

	/* --- キャッシュ 4CC タグ (Phase 4③: 4CC→カーネル id 表を .so 申告駆動に) --- */
	/* ★ 2026-08-28 (ABI v14): codec_tags (このモジュールの 4CC の CSV) を **撤去**した。
	 *   読み手が一人も残っていなかった (4CC で表を引く経路が全て無くなったため)。 */
	/* ★ v5 (rev4 sig 化): 旧 read_foreign_tags (昇格読み可能な foreign 4CC) は撤去。クロスカーネルの
	 *   受理は各 op の sig に foreign 入力型を明示列挙して表現する (実際の昇格読みは codec の foreign
	 *   reader = cgal の cg-mf-upgrade が担う・そちらは codec_tags/codecs 側に残る)。 */

	/* --- codec factory (Phase 4③': reader/writer を descriptor に接続) --- */
	/* ★ 2026-08-28 (ABI v16): 旧 codecs + wires を統合。0 終端 (wire==0 が番兵)。0 可。
	 *   この 1 本が「このモジュールは、どの本体クラス階層について、どの型名を名乗るか」を表す。
	 *   reader / writer / match / create_for_meta は wire から引く。 */
	const pigModuleType* provides;

	/* ★ 2026-08-28 (ひさ設計・ABI v11): 旧 types / type_tags を **撤去**。
	 *   「自分が所有する型とその形式」は codecs の **writer を持つ行** が既に表しており
	 *   (types × tags の位置対応)、二重に申告する理由が無かった。二重申告は必ずずれる —
	 *   実際 openvdb は codecs で mf-mesh3d(MFM3) を書けると言いながら types には書いていなかった。
	 *   非幾何型 (value/ref) は全モジュール共通なので libpig の pig_nongeometric_types 1 本が持つ。 */

	/* --- キャッシュキーソルト (#3427: 旧 manifest.cpp の静的初期化からの自己申告を記述子へ移動) --- */
	/* このモジュールが出力するキャッシュのキーに混ぜる弁別バイト列 (manifold="\x01MFM")。
	 * 基準カーネル (cgal) は 0 = ソルト無しで、既存キャッシュキーを byte 不変に保つ。 */
	const char*       hash_salt;

	/* ═══ v8: スレッド関連の申告 (thread_kind / sigma) は撤去した。理由は上の ABI 注記。 ═══ */

	/* ★ v9 (#3436 P4): **N'** = このモジュールが 1 ノードあたり受け取りたい最大項数 (policy)。
	 * **0 = 未指定 = 2** (二項)。分解の実項数は k = min(N', op の sig が申告する N, 群の執行者が
	 * 許す最大)。`module(so, {arity:k})` で上書きできる。
	 * ★ capability (op ごとの N・**正しさ**の上限) と policy (モジュールごとの N'・**つまみ**) の
	 *   分離がこの設計の核。N' は「最大」であって「固定」ではない (docs §5.2)。 */
	int               arity;

	/* ★ モジュール全体の初期化 (§7)。**そのモジュールの最初の agent が起きるときに 1 回だけ**
	 * 呼ばれる。TBB の global_control のように「プロセスに 1 度だけ」設定したいものを置く。
	 * 0 可 (何もしない)。呼ぶのは ptsMediator で、**TS_STATE 内なので排他は不要**
	 * (tinyState の状態機械は app-mutex 下で直列化される)。 */
	void            (*initialize)(void);

	/* ★ v10 (#3441): **module(so,{opts}) のハッシュ全体**を受け取るフック。0 可 (使わない)。
	 * `initialize` と違って「1 回だけ」ではなく、**opts が設定/更新されるたびに**呼ばれる
	 * (同じプロセス内で module() が複数回呼ばれれば複数回)。★ 冪等に実装すること。
	 *   - planner (in-proc 含む): module() 実行時に pigModuleRegistry が直接呼ぶ。
	 *   - process agent: 起動直後に C_ENV (pigwire.h) で opts を受け取り、agent 側の
	 *     pigModuleRegistry が同じ経路で呼ぶ (「planner と同じ方法で上書きする」)。
	 *   ⚠ 稼働中の agent への **再配線はしない** — 次にその agent が (再) 起動されたときに
	 *   反映される (旧 C_ENV/L_THR も実運用ではこの形でしか機能していなかった)。
	 * opts は thNULL のことがある (module(so,"on"/"off") 等、opts 無しの呼び出し) — 実装は
	 * 自分が使うキーが無ければ何もしないこと。 */
	void            (*configure)(sPtr<pigData> opts);
};

/* ★ .so 化 Phase 3: .so が公開する唯一の C エクスポート。ローダは dlsym(SRAVA_MODULE_SYM) で
 * この関数を引き、返る記述子を pigModuleRegistry / pigAgentRegistry へ配線する (pigModuleLoader)。
 * 静的リンク版 (フォールバックビルド・§8) では manifest.cpp を .so にだけ入れ、実行体本体には
 * 入れない → 実行ファイルにこのシンボルは現れず衝突しない。 */
#define SRAVA_MODULE_SYM  "srava_module"

/* ★ 2026-08-27 (MinGW 実機で判明): **Windows では明示的にエクスポート属性が要る**。
 *   ELF/Mach-O は既定で全シンボルを公開するが、PE は「エクスポート表」に載ったものだけが
 *   GetProcAddress で引ける。MinGW の ld は救済として **--export-all-symbols を暗黙に効かせる**
 *   が、これは **DLL 内に明示的な dllexport が 1 つも無いとき限定**の挙動である。
 *   ⇒ OCCT をリンクするモジュールは、OCCT ヘッダの Standard_EXPORT が dllexport を撒くので
 *     自動エクスポートが切れ、`srava_module` だけが表から漏れて
 *     `GetProcAddress failed: srava_module: error 127` になった (occt.dll は 2213 個
 *     エクスポートしていたのに srava_module だけが無い、という形で発覚)。
 *   ★ 属性を明示すれば「他に dllexport があるか」に依存しなくなる。ELF/Mach-O では
 *     visibility("default") = 既定と同じなので無害。 */
#if defined(_WIN32) || defined(__CYGWIN__)
#  define SRAVA_MODULE_EXPORT  extern "C" __declspec(dllexport)
#else
#  define SRAVA_MODULE_EXPORT  extern "C" __attribute__((visibility("default")))
#endif

SRAVA_MODULE_EXPORT const srava_module_descriptor* srava_module(void);
typedef const srava_module_descriptor* (*srava_module_fn)(void);

#endif
