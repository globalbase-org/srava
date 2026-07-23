/* step5 ptsWirePipe echo 往復テスト。tsApplication 直下に ptsWirePipeTest を起動。
 * 同一プロセス内で socketpair 越しに 2 本の ptsWirePipe を handshake → ping/echo → 番兵終了。 */
#include	"ts2/c++/tsApplication.h"
#include	"pig/c++/ptsWirePipeTest.h"

extern int ptsWirePipeTest_exitCode;

int
main(int argc, char** argv)
{
	thNEW(tsApplication, (thNULL, [](sPtr<tsApplication> app) {
		thNEW(ptsWirePipeTest, (app));
	}));
	return ptsWirePipeTest_exitCode;
}
