/* ts2IO::write_c の「pipe バッファ超えで停止」最小再現ドライバ。
 * tsApplication 直下に tsPipeRepro を起動し、子(reader)へ TSPIPE_BYTES バイトを 1 回 write_c する。
 * 期待: 64KB 以下は "[repro] DONE ..." を表示して終了、超えると停止(表示が出ずハング)。
 * 使い方: `TSPIPE_BYTES=131072 timeout 10 ./repro_pipe` でハング(再現)を検出。 */
#include	"ts2/c++/tsApplication.h"
#include	"pig/c++/tsPipeRepro.h"

#include	<signal.h>

int
main(int argc, char** argv)
{
	::signal(SIGPIPE, SIG_IGN);   /* 子が先に死んでも write で即死しないように */
	thNEW(tsApplication, (thNULL, [](sPtr<tsApplication> app) {
		thNEW(tsPipeRepro, (app));
	}));
	return 0;
}
