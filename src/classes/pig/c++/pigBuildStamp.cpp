/*
 * pigBuildStamp — planner と agent が **同じビルドか**を突き合わせるための識別子。
 *
 * ★なぜ要るか(2026-08-15 bench 報告):
 *   planner(新)と agent(旧)を混ぜて実行すると、症状が版差の内容によって 3 通りに割れる:
 *     ① 両版が持つ素の式 `volume(box(2,2,2))` すら `volume: needs a mesh` で落ちる
 *        (= 新機能を使っていなくても壊れる・エラー文が実態と乖離していて最も厄介)
 *     ② 新しい op → `unknown op`
 *     ③ 引数が増えた op → `agent closed unexpectedly` の後、**沈黙してハング**(SIGTERM 不応)
 *   /usr/local を更新した直後に、ビルドツリーを持っている人が `SRAVA_AGENT` を指定せず走らせると
 *   必ず踏む。原因が「版の食い違い」だと分かる形で**即座に落とす**のがここの目的。
 *
 * 値は「ビルドが違えば違う」ことだけが要件なので、この TU のコンパイル時刻を使う。planner と agent は
 * 同じ libpig を共有するので、同一ビルドなら必ず一致し、別ビルド(別ツリー / 別 install)なら食い違う。
 * (SRAVA_BUILD_STAMP を define すれば、再現ビルド用に外から固定値を与えることもできる。)
 */
#include	"pig/c++/pigBuildStamp.h"
#include	<string.h>

#ifndef SRAVA_BUILD_STAMP
#define SRAVA_BUILD_STAMP __DATE__ " " __TIME__
#endif

/* argv に載せるので **空白を含めない** (agent の起動コマンドは空白区切りで argv 化される)。 */
const char*
srava_build_stamp()
{
	static char buf[64];
	if ( buf[0] == '\0' ) {
		::strncpy(buf, SRAVA_BUILD_STAMP, sizeof buf - 1);
		for ( char *p = buf ; *p ; ++p ) if ( *p == ' ' ) *p = '_';
	}
	return buf;
}
