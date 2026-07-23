/* step4 往復テスト。tsApplication 直下に ptsWireStreamTest を起動。
 * ptsWireStreamTest が WriterText→ReaderText の往復(逐次/並行)を検証し、
 * PASS で exit(0) / FAIL で exit(1)。 */
#include	"ts2/c++/tsApplication.h"
#include	"pig/c++/ptsWireStreamTest.h"

/* テスト終了コード(ptsWireStreamTest.cpp で定義)。FAIL なら 1。 */
extern int ptsWireStreamTest_exitCode;

int
main(int argc, char** argv)
{
	/* tsApplication のコンストラクタが fwIO::loop でブロックし、
	 * アプリがアイドル終了(全 state machine 完了)すると戻る。 */
	thNEW(tsApplication, (thNULL, [](sPtr<tsApplication> app) {
		thNEW(ptsWireStreamTest, (app));
	}));
	return ptsWireStreamTest_exitCode;
}
