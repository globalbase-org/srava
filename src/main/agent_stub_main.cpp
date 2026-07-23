/* srava-agent-stub — pigfAgent の「全状態通す」テスト用の最小エージェント。
 * tsApplication 直下に ptsAgentStub を起動し、自 stdin/stdout で control-plane を話す。
 * pigfAgent が ts2System でこのバイナリを起動する(パスは SRAVA_AGENT env で注入)。 */
#include	"ts2/c++/tsApplication.h"
#include	"pig/c++/ptsAgentStub.h"

#include	<signal.h>

int
main(int argc, char** argv)
{
	/* pipe IPC: 相手が先に閉じた後の write を EPIPE で返させる(SIGPIPE で即死させない)。 */
	::signal(SIGPIPE, SIG_IGN);
	thNEW(tsApplication, (thNULL, [](sPtr<tsApplication> app) {
		thNEW(ptsAgentStub, (app));
	}));
	return 0;
}
