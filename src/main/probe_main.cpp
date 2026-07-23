/* tinyState ビルド経路 probe の起動。tsApplication → tsHello。 */
#include	"ts2/c++/tsApplication.h"
#include	"probe/c++/tsHello.h"

int
main(int argc, char** argv)
{
	thNEW(tsApplication, (thNULL, [](sPtr<tsApplication> app) {
		thNEW(tsHello, (app));
	}));
}
