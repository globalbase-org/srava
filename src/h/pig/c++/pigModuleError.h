#ifndef ___pigModuleError_H___
#define ___pigModuleError_H___
/*
 * pigModuleError.h — モジュールが **自分の名前で** エラーを作るための雛形 (#3475)。
 *
 * エラー文言は "[TAG] module/op: message" の 3 段 (組み立ては pigDataError の ctor)。
 * 呼び出し側はモジュール名を明示的に渡す必要があるが、素で書くと
 *     thNEW(pigDataError,(m, i, PE_NORMAL, "cgal"))
 * を **モジュール横断で数百箇所**書くことになり、必ず付け忘れる (scrub-reaches-the-user で
 * 3 回踏んだ型)。そこで各モジュールが自分のヘッダで 1 度だけ
 *     PIG_DEFINE_MODULE_ERR(cga_err, CG_MODULE_NAME)
 * と書き、以後 `cga_err(m)` / `cga_err(m, info)` / `cga_err(m, info, PE_FATAL)` を使う。
 *
 * ★ 隠れ状態 (「いまどのモジュールを実行中か」をレジストリ/スレッドローカルに持つ) は
 *   採らない (ひさ判断 2026-09-05)。in-proc では全モジュールが 1 プロセスに同居するので、
 *   隠れ状態は実行方式に依存して壊れる。明示なら付け忘れても **名前が出ないだけ**で、
 *   **誤ったモジュール名が出ることはない**。
 *
 * ⚠ **static** であることが要る。nef は同一ソースから nef_snc.so / nef_hybrid.so の 2 つを
 *   ビルドし、NF_MODULE_NAME だけが違う。外部リンケージだと、ローダが RTLD_GLOBAL で
 *   両方を開いたときに **先に解決された方が両者に効いて** 名前が入れ替わる。
 */
#include	"pig/c++/pigData.h"

#define PIG_DEFINE_MODULE_ERR(fn, modname)                                              \
	static inline sPtr<pigDataError>                                                \
	fn(const char *m, sPtr<pigInfo> i = thNULL, int cls = PE_NORMAL)                \
	{ return thNEW(pigDataError,(m, i, cls, (modname))); }                          \
	static inline sPtr<pigDataError>                                                \
	fn(sPtr<stdString> m, sPtr<pigInfo> i = thNULL, int cls = PE_NORMAL)            \
	{ return thNEW(pigDataError,(m, i, cls, (modname))); }

#endif
