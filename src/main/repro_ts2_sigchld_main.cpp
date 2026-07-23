/* ts2System SIGCHLD teardown バグの最小再現ドライバ。
 * tsApplication 直下に tsSysRepro を起動し、ts2System を N 回 launch→destroy する。
 * 期待: 2 回目以降の teardown で SEGV(tsSignalCore signal_list の dangling deref)。
 * 修正後は "SURVIVED" を表示して正常終了。 */
#include	"ts2/c++/tsApplication.h"
#include	"pig/c++/tsSysRepro.h"

#include	<signal.h>

int
main(int argc, char** argv)
{
	/* SIGPIPE で隠れず、本命の SIGCHLD teardown SEGV が見えるように */
	::signal(SIGPIPE, SIG_IGN);
	thNEW(tsApplication, (thNULL, [](sPtr<tsApplication> app) {
		thNEW(tsSysRepro, (app));
	}));
	return 0;
}
