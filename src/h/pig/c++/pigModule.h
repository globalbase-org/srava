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

/* ★ .so 化 Phase 4③': モジュールが提供するキャッシュ codec 1 件 (reader/writer factory)。
 * descriptor.codecs は **name==0 を番兵**とする配列 (n_codecs は持たず、別 TU の配列を
 * extern で指せるようにするため)。ローダが register_descriptor で pigCacheCodec へ登録する。 */
struct pigModuleCodec {
	const char*       name;      /* dedup キー ("cg-mesh" 等)。0 = 配列終端 */
	const char*       tags;      /* D_META 4CC の CSV ("MESH,PLY2") */
	/* ★ P2 (⑤ cross-module 型変換): tags と **位置対応** した出力型名の CSV
	 *   ("cg-mesh3d,cg-cross2d")。reader が i 番目のタグを読んだとき生成する型。
	 *   reader_for(tag, target_type) が「そのタグを読めて出力型が target と一致する codec」を
	 *   引くのに使う (owner 概念に代わる型軸の reader 選択)。無型 (REF 等) は "" 可。 */
	const char*       out_types;
	pigCacheMatchFn   match;     /* writer 選択 (本文型判定)。読取専用 codec は never (0 返し) */
	pigCacheReaderFn  mkReader;
	pigCacheWriterFn  mkWriter;
};

/* 起動方式の能力 bitmask (docs §2.2)。今回実装は THREAD/PROCESS のみ。REMOTE は枠。 */
enum {
	EXEC_THREAD  = 1 << 0,   /* planner 内 thread (ptsMediatorInternal) */
	EXEC_PROCESS = 1 << 1,   /* agent プロセス (ptsMediatorExternal) */
	EXEC_REMOTE  = 1 << 2,   /* 将来: リモートノード */
};

#define SRAVA_MODULE_ABI 6   /* 記述子 ABI 版。dlopen 時に不一致なら拒否。v3: codecs 追加 (Phase 4③')。
                              * v4: pigOpEntry に型シグネチャ sig 追加 (rev4 Phase B・型ディスパッチ)。
                              * v5: read_foreign_tags 撤去 (rev4 sig 化・クロスカーネル受理は op sig の foreign
                              *     入力型で表現するようになり、read_foreign_tags→coercion/can_read_module が不要に)
                              * v6: hash_salt 追加 (#3427)。.so 側の静的自己登録
                              *     (register_agent / register_type / register_hash_salt) を廃し、
                              *     すべて記述子から loader/registry が登録する形へ統一。 */

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
	const char*       codec_tags;         /* このモジュールがサポートするキャッシュ 4CC の CSV (cgal="MESH,PLY2" / mf="MFM3,MFC2")。0 可 */
	/* ★ v5 (rev4 sig 化): 旧 read_foreign_tags (昇格読み可能な foreign 4CC) は撤去。クロスカーネルの
	 *   受理は各 op の sig に foreign 入力型を明示列挙して表現する (実際の昇格読みは codec の foreign
	 *   reader = cgal の cg-mf-upgrade が担う・そちらは codec_tags/codecs 側に残る)。 */

	/* --- codec factory (Phase 4③': reader/writer を descriptor に接続) --- */
	const pigModuleCodec* codecs;         /* name==0 番兵終端の配列。ローダが pigCacheCodec へ登録 (reader 選択は tags×out_types の型軸)。0 可 */

	/* --- このモジュールが I/O する実装型 (#3427: 旧 manifest.cpp の register_type を記述子へ移動) --- */
	/* 型名と 4CC の CSV。**位置対応** ("mf-mesh3d,mf-cross2d" と "MFM3,MFC2")。0 可。
	 * ⚠ codecs の tags/out_types から導出してはいけない。foreign 読み codec (cgal の cg-mf-upgrade =
	 *   MFM3/MFC2 を読んで cg-mesh3d/cg-cross2d を出す) が混ざっており、導出すると
	 *   register_type("cg-mesh3d","MFM3") のような誤った対応を後勝ちで焼き込んでしまう。
	 *   「自分が所有する型」はここで明示的に申告する。 */
	const char*       types;              /* "mf-mesh3d,mf-cross2d" */
	const char*       type_tags;          /* "MFM3,MFC2" (types と位置対応) */

	/* --- キャッシュキーソルト (#3427: 旧 manifest.cpp の静的初期化からの自己申告を記述子へ移動) --- */
	/* このモジュールが出力するキャッシュのキーに混ぜる弁別バイト列 (manifold="\x01MFM")。
	 * 基準カーネル (cgal) は 0 = ソルト無しで、既存キャッシュキーを byte 不変に保つ。 */
	const char*       hash_salt;
};

/* ★ .so 化 Phase 3: .so が公開する唯一の C エクスポート。ローダは dlsym(SRAVA_MODULE_SYM) で
 * この関数を引き、返る記述子を pigModuleRegistry / pigAgentRegistry へ配線する (pigModuleLoader)。
 * 静的リンク版 (フォールバックビルド・§8) では manifest.cpp を .so にだけ入れ、実行体本体には
 * 入れない → 実行ファイルにこのシンボルは現れず衝突しない。 */
#define SRAVA_MODULE_SYM  "srava_module"
extern "C" const srava_module_descriptor* srava_module(void);
typedef const srava_module_descriptor* (*srava_module_fn)(void);

#endif
