/* srava-agent — カーネル非依存のエージェントプロセス本体 (.so 化 Phase 3c・docs §1.2)。
 *
 *   srava_agent <module.so> [op file line]
 *
 * argv[1] のカーネル .so を **ptsAgentApplication (基底 ptsApplication) の INI** がロードする
 * (#3427 ③: ctor に渡す → INI_ptsObject_START が app 所有レジストリ module_registry へ
 * PIG_MODLOAD_FILE/RTLD_NOW で配線)。以降 enable() が agents.lookup(0) でそれを引いて
 * 実行体を起こす。op/file/line (argv[2..]) は agentwatch/ps 表示用の飾りで agent は無視する。
 *
 * ★ この host は pig/pts/tinyState を持ち -rdynamic で export する。dlopen(RTLD_NOW) が .so の
 *   未解決シンボルをここから解決する (カーネル .so 側はそれらを bundle しない)。 */
#include	"ts2/c++/tsApplication.h"
#include	"pig/c++/ptsAgentApplication.h"
#include	"pig/c++/pigBuildStamp.h"

#include	<signal.h>
#include	<cstdio>
#include	<string.h>

int
main(int argc, char** argv)
{
#ifdef SIGPIPE
	::signal(SIGPIPE, SIG_IGN);   /* 相手が先に閉じた後の write を EPIPE 化(即死回避)。MinGW は SIGPIPE 無し */
#endif

	if ( argc < 2 ) {
		std::fprintf(stderr, "usage: %s <module.so> [op file line]\n", argv[0]);
		return 2;
	}

	/* ★ 版の突き合わせ (2026-08-15 bench 報告)。planner が `b=<ビルド識別子>` を渡してくるので、
	 * 自分のものと比べて違えば **ここで落ちる**。混ぜて動かすと、症状が「両版が持つ素の式が
	 * `volume: needs a mesh` で落ちる」「沈黙してハング (SIGTERM 不応)」など原因の見当がつかない
	 * 形で出るため、起動直後に潰しておく。b= が無い = 古い planner なので、その場合は素通しする
	 * (古い planner はこの引数を知らないので、ここで弾いても状況は良くならない)。 */
	for ( int i = 2 ; i < argc ; ++i ) {
		if ( ::strncmp(argv[i], "b=", 2) != 0 )
			continue;
		if ( ::strcmp(argv[i] + 2, srava_build_stamp()) == 0 )
			break;   /* 一致 */
		std::fprintf(stderr,
			"srava_agent: planner と版が違います (planner=%s / agent=%s)。\n"
			"  planner と agent は同じビルドのものを使ってください。ビルドツリーで動かすときは\n"
			"  SRAVA_AGENT にそのツリーの srava_agent を指定します (agent の実体: %s)。\n",
			argv[i] + 2, srava_build_stamp(), argv[0]);
		return 3;   /* pigfAgent がこの終了コードを見て「版が違う」と報告する */
	}

	/* ★ #3427 ③: 旧 file-static (g_agent_so / g_load_failed) は撤去。bootstrap は
	 * std::function (キャプチャ可) なので argv[1] はキャプチャで渡し、ロード失敗は app の
	 * member (module_load_failed) を完走後に読む (thNEW(tsApplication,...) はブロックする)。 */
	const char *agent_so = argv[1];
	sPtr<ptsAgentApplication> aapp;
	thNEW(tsApplication, (thNULL, [&aapp, agent_so](sPtr<tsApplication> app) {
		aapp = thNEW(ptsAgentApplication, (app, agent_so));
	}));
	return ( aapp != thNULL && aapp->module_load_failed() ) ? 1 : 0;
}
