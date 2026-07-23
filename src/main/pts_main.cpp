/* pts 土台の smoke。tsApplication → ptsApplication を起動。
 * ptsApplication が INI で ptsApp=自分 を立て、ACT で確認して自己終了する。 */
#include	"ts2/c++/tsApplication.h"
#include	"pig/c++/ptsApplication.h"

int
main(int argc, char** argv)
{
	thNEW(tsApplication, (thNULL, [](sPtr<tsApplication> app) {
		thNEW(ptsApplication, (app));
	}));
}
