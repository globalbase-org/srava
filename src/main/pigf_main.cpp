/* 3-3a async 機構テスト。tsApplication → pigfMain。
 * pigfMain が pigDataFunction<pigfConst> を compact し、非同期 yield/resume で 42 を得る。 */
#include	"ts2/c++/tsApplication.h"
#include	"pig/c++/pigfMain.h"

int
main(int argc, char** argv)
{
	thNEW(tsApplication, (thNULL, [](sPtr<tsApplication> app) {
		thNEW(pigfMain, (app));
	}));
}
