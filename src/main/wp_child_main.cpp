/* wp_child — ptsWirePipe 子側。自 stdin/stdout に ptsWirePipe を張り、親が送る大レコードを
 * 受け取って "COUNT=<n>" を返す。wpBigParent が ts2System で起動する(WP_CHILD env)。 */
#include	"ts2/c++/tsApplication.h"
#include	"pig/c++/wpBigChild.h"

#include	<signal.h>

int
main(int argc, char** argv)
{
	::signal(SIGPIPE, SIG_IGN);
	thNEW(tsApplication, (thNULL, [](sPtr<tsApplication> app) {
		thNEW(wpBigChild, (app));
	}));
	return 0;
}
