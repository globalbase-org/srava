/*
 * tsHello — tinyState ビルド経路(tscpp2 codegen → compile → link → run)の検証 probe。
 * hello-world 例を踏襲した最小の状態機械。ステップ3で pigfFunction 等を実装する前の
 * パイプライン確認用。実装が乗ったら削除してよい。
 */
#include	"_ts2/c++/tsHello_.h"

CLASS_TINYSTATE(probe/c++/tsHello,ts2/c++/tinyState)


#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	tsHello_(
		sPtr<tinyState> parent);
private:
protected:
	TS_DEFARGS
};

TS_END_IMPLEMENT

TS_BEGIN_INTERFACE
// predefine
#include	"ts2/c++/sRptr.h"
class tinyState;
TS_END_INTERFACE

#endif


tsHello_::tsHello_(TS_ARGS0)
        : tinyState_(parent)
{
    TS_CPARGS0
}


/*******************************************
	STATE MACHINE
********************************************/

TS_STATE(INI_START)
{
	return rDO|ACT_START;
}

TS_STATE(ACT_START)
{
	::printf("[probe] tinyState build pipeline OK\n");
	return rDO|FIN_START;
}

TS_STATE(FIN_START)
{
	return rDO|FIN_TINYSTATE_START;
}
