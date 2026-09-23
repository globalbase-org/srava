/*
 * test/modules/srava_testmod_bad.cpp — **わざと拒否される** テスト用モジュール (#3558)。
 *
 * ★★ 目的: @module(so, {optional:1})@ が「**入っていない**」と「**拒んだ**」を区別することの
 *   検定材料。拒否の経路は 4 つあるが、そのうち *正規の .so でしか作れない* 2 つをここで建てる:
 *     TESTMOD_BAD_ABI … abi_version を host と 1 ずらす        → ABI mismatch
 *     TESTMOD_BAD_SIG … export op を持つのに export_exts が空  → pig_descriptor_violation
 *     TESTMOD_NOT_A_MODULE … srava_module を **export しない** = そもそもモジュールでない .so
 *   残る 1 つ (壊れたファイル) だけは test 側で作れる (test/srava_parse.sh の
 *   module_optional_refused を参照)。
 *
 * ⚠⚠ **libpig を名指しするのではいけない** (2026-09-19)。
 *   ・libpig.dylib と書くと mac では module() の拡張子正規化で libpig.so へ化け、
 *     「入っていない」として *正しく* 飲み込まれて **検定が何も測らない** (mac が発見)
 *   ・libpig を **コピーして**名指しすると、こんどは Linux で **dlopen が返ってこない**
 *     (RTLD_GLOBAL で libpig の 2 つめの実体が載り、大域状態が二重化する)
 *   ⇒ *依存の無い空の .so* をこちらで建てるのが両方で成り立つ唯一の形。
 *
 * ⚠⚠ **探索路に置かない**。CMake が専用のサブディレクトリへ出すので、起動時の走査では
 *   拾われず、テストが**パスを名指ししたときだけ**読まれる。ここを守らないと
 *   `srava --modules` の診断に常時「壊れた .so」が並ぶ。
 *
 * ★ 実行体 (make_agent) も codec も持たない — ロードの **拒否**を見るためのものなので、
 *   拒否される前の段で必要な欄しか埋めない。
 */
#include "pig/c++/pigModule.h"
#include "pig/c++/pigOpEntry.h"

#if defined(TESTMOD_NOT_A_MODULE)
/* ★ srava_module を export しない。dlopen は成功し dlsym が失敗する = 「在るのに使えない」。
 *   中身が空だと ld が丸ごと落とす処理系があるので、無害な関数を 1 つだけ置く。 */
extern "C" int srava_testmod_not_a_module(void) { return 0; }
#else
#if defined(TESTMOD_BAD_ABI)
#  define TESTMOD_NAME "testmod_badabi"
#  define TESTMOD_ABI  (SRAVA_MODULE_ABI + 1)   /* host と必ずずれる */
#elif defined(TESTMOD_BAD_SIG)
#  define TESTMOD_NAME "testmod_badsig"
#  define TESTMOD_ABI  SRAVA_MODULE_ABI
#else
#  error "TESTMOD_BAD_ABI か TESTMOD_BAD_SIG のどちらかを定義すること"
#endif

static const pigOpEntry TESTMOD_OPS[] = {
#if defined(TESTMOD_BAD_SIG)
	/* ★ export op を持つのに export_exts を申告しない = 記述子違反。
	 *   routing は拡張子で振るので「誰も書けない」のに一般ロジックへ落ちる形。 */
	{ "export", 0, 0, AK_INLINE, 0, 0, "->ref" },
#else
	{ "testmod_noop", 0, 0, AK_INLINE, 0, 0, "->value" },
#endif
};
static const int TESTMOD_N_OPS = (int)(sizeof(TESTMOD_OPS) / sizeof(TESTMOD_OPS[0]));

static const srava_module_descriptor TESTMOD_DESC = {
	.abi_version   = TESTMOD_ABI,
	.name          = TESTMOD_NAME,
	.priority      = -99,          /* 万一載っても既定カーネルにならない */
	.make_agent    = 0,
	.exec_caps     = (unsigned)EXEC_PROCESS,
	.exec_default  = EXEC_PROCESS,
	.ops           = TESTMOD_OPS,
	.n_ops         = TESTMOD_N_OPS,
	.import_exts   = 0,
	.export_exts   = 0,            /* ★ BAD_SIG ではこれが違反そのもの */
	.provides      = 0,
	.cache_version = 1,
	.initialize    = 0,
	.configure     = 0,
};

SRAVA_MODULE_EXPORT const srava_module_descriptor* srava_module(void)
{
	return &TESTMOD_DESC;
}
#endif   /* TESTMOD_NOT_A_MODULE */
