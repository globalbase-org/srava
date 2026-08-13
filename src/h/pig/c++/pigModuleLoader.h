#ifndef ___pigModuleLoader_H___
#define ___pigModuleLoader_H___
/*
 * pigModuleLoader — モジュール .so ロードの**記録型** (docs/agent_so_design.md §3.1)。
 *
 * ★ #3427 ③: 旧 namespace pigModuleLoader の関数 (load_file / load_search_path / load_log /
 *   search_dirs) は **pigModuleRegistry のメソッド**へ移した (実装は pigModuleLoader.cpp のまま)。
 *   ロード記録の可変 static (g_log / g_dirs) も registry のメンバへ移り、プロセス全体の
 *   可変 static が消えた。このヘッダに残るのは記録エントリの型だけ。
 *
 * ロード記録 (診断用・2026-08-12)。`srava --modules` が「どこを見て・何をロードし・何に失敗したか」を
 * 出すために、load_file の試行を全件記録する。モジュールが動かないときの切り分けは
 *   ① そもそもファイルが見つからない ② 見つけたが load に失敗 (依存ライブラリ欠け等)
 *   ③ load できたが routing 候補から外れている
 * の 3 つで、①②はこの記録、③は pigModuleRegistry 側で分かる。
 * ★ Windows では LoadLibrary が依存 DLL 欠けを ERROR_MOD_NOT_FOUND(126) としか言わないため、
 *   「試したパス + 理由」を残すこの記録の価値が Linux より高い。
 */
#include <string>

struct pigModuleLoadEvent {
	std::string path;   /* 試した .so の絶対パス */
	std::string name;   /* 成功時: 記述子の name (失敗時は空) */
	std::string err;    /* 失敗時: 理由 (dlopen / dlsym / abi 不一致) */
	bool        ok;
	/* ★ 「モジュールではない .so」(SRAVA_MODULE_SYM を持たない = libpig.so 等) と「モジュールなのに
	 *   読めなかった」を区別する。探索路には非モジュールの .so が同居しうる (ビルドツリーの libpig.so)
	 *   ので、これを混ぜると診断出力が嘘のエラーで埋まる。 */
	bool        not_a_module;
	/* ★ 同名 (同じファイル名) の勝者が後の探索路に在ったため **読まなかった** 候補 (2026-08-13)。
	 *   ok=false / err 空 / shadowed=true で表す。「読んだが上書きされた」ではなく「読んでいない」。 */
	bool        shadowed;
};

struct pigModuleSearchDir {
	std::string dir;
	const char* origin;   /* "exe dir" / "sysdir" / "user config" / "$SRAVA_MODULE_PATH" */
	bool        exists;   /* opendir できたか */
};

#endif
