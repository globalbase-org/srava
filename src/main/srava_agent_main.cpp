/* srava-agent — エージェントプロセス本体。tsApplication 直下に cgatsAgent を起動し、
 * 自 stdin/stdout で control-plane(pigwire)を話す。プランナーが ts2System で起動する。
 * (骨格段階: dispatch + 計算本体。CGAL は未リンク。box=ダミー mesh) */
#include	"ts2/c++/tsApplication.h"
#include	"cg/c++/cgatsAgent.h"

#include	<signal.h>

int
main(int argc, char** argv)
{
#ifdef SIGPIPE
	::signal(SIGPIPE, SIG_IGN);   /* 相手が先に閉じた後の write を EPIPE 化(即死回避)。MinGW は SIGPIPE 無し */
#endif

	/* NB(#4 性能崖): 巨大インライン引数の pipe 送信ストールは ts2IO の write_c EAGAIN ハンドラが
	 * writefds でなく readfds に積む不具合(tinyState #3365)が原因だった。修正済みのため、以前ここで
	 * 行っていた stdin/stdout の F_SETPIPE_SZ 1MB 拡張(暫定)も pigfAgent 側の set_divisible も不要に
	 * なり撤去した。任意サイズの引数が素の write_c で詰まらず送れる(再現/退行検知は repro_pipe)。 */

	thNEW(tsApplication, (thNULL, [](sPtr<tsApplication> app) {
		thNEW(cgatsAgent, (app));
	}));
	return 0;
}
