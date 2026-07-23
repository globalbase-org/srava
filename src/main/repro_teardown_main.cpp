/* 多数 ts2System 子の一括 teardown レース(ワーカースレッドでの pipe FIN/abort vs main app teardown)
 * の最小再現ドライバ。tsApplication 直下に tsTeardownRepro を起動するだけ。
 * 使い方: `for i in $(seq 50); do ./repro_teardown >/dev/null 2>&1 || echo SEGV; done`
 *   レースなので確率的。SEGV(exit 139)が出れば再現。 */
#include	"ts2/c++/tsApplication.h"
#include	"pig/c++/tsTeardownRepro.h"

#include	<signal.h>

int
main(int argc, char** argv)
{
	::signal(SIGPIPE, SIG_IGN);   /* 子が先に死んでも write で即死しないように */
	thNEW(tsApplication, (thNULL, [](sPtr<tsApplication> app) {
		thNEW(tsTeardownRepro, (app));
	}));
	return 0;
}
