/* cgatsAgent end-to-end テスト。tsApplication 直下に cgatsAgentTest を起動し、
 * 別プロセス srava_agent(SRAVA_AGENT)と box 演算を往復する。
 * キャッシュ dir はここで test 専用に設定+清掃。 */
#include	"ts2/c++/tsApplication.h"
#include	"cg/c++/cgatsAgentTest.h"
#include	"pig/c++/osglue.h"   /* osglue_setenv(MinGW 対応) */

#include	<signal.h>
#include	<stdlib.h>
#include	<string.h>
#include	<stdio.h>


int
main(int argc, char** argv)
{
	::signal(SIGPIPE, SIG_IGN);

	const char *dir = "/tmp/srava-cgatsagent-test-cache";
	osglue_setenv("SRAVA_CACHE_DIR", dir);
	osglue_rmrf(dir);   /* portable 再帰削除(Windows の cmd.exe に rm が無いため system 不可) */

	/* ★ #3427 ③: fixture 登録はテスト app (cgatsAgentTest) の INI へ移動 (app 所有レジストリ)。
	 * 終了コードは app のメンバから (旧 file-global cgatsAgentTest_exitCode は撤去)。 */
	sPtr<cgatsAgentTest> t;
	thNEW(tsApplication, (thNULL, [&t](sPtr<tsApplication> app) {
		t = thNEW(cgatsAgentTest, (app));
	}));
	return ( t != thNULL ) ? t->exit_code() : 1;
}
