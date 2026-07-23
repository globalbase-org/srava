/* step5 pigfAgent エンドツーエンドテスト。tsApplication 直下に pigfAgentTest を起動。
 * pigDataPair/pigDataCache/継続 を別プロセス srava-agent-stub と往復して検証する。
 * agent パスは SRAVA_AGENT(ctest が注入)。キャッシュ dir はここで test 専用に設定+清掃。 */
#include	"ts2/c++/tsApplication.h"
#include	"pig/c++/pigfAgentTest.h"
#include	"pig/c++/osglue.h"   /* osglue_setenv(MinGW 対応) */

#include	<signal.h>
#include	<stdlib.h>
#include	<string.h>
#include	<stdio.h>

extern int pigfAgentTest_exitCode;

int
main(int argc, char** argv)
{
	/* pipe IPC: 双方 wend 後の遅延 write で SIGPIPE 即死しないよう EPIPE 化 */
	::signal(SIGPIPE, SIG_IGN);

	/* テスト専用キャッシュ dir を清掃して MISS から始める(T2 の HIT を保証) */
	const char *dir = "/tmp/srava-agent-test-cache";
	osglue_setenv("SRAVA_CACHE_DIR", dir);
	osglue_rmrf(dir);   /* portable 再帰削除(Windows の cmd.exe に rm が無いため system 不可) */

	thNEW(tsApplication, (thNULL, [](sPtr<tsApplication> app) {
		thNEW(pigfAgentTest, (app));
	}));
	return pigfAgentTest_exitCode;
}
