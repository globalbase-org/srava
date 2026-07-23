/* wp_parent — ptsWirePipe 親側で大レコード送信 deadlock を再現するドライバ。
 * tsApplication 直下に wpBigParent を起動。子(wp_child)は WP_CHILD env でパス注入。 */
#include	"ts2/c++/tsApplication.h"
#include	"pig/c++/wpBigParent.h"

#include	<signal.h>
#include	<cstdio>

extern int wpBigParent_exitCode;

int
main(int argc, char** argv)
{
	::setvbuf(stdout, NULL, _IONBF, 0);
	::signal(SIGPIPE, SIG_IGN);
	thNEW(tsApplication, (thNULL, [](sPtr<tsApplication> app) {
		thNEW(wpBigParent, (app));
	}));
	if ( ::getenv("WP_TDDBG") ) ::fprintf(stderr,"[wp] main: app loop RETURNED, exit code=%d\n", wpBigParent_exitCode);
	return wpBigParent_exitCode;
}
