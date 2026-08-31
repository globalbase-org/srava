/*
 * d4aCube — d4_cube(s) の計算本体 (rev4 Phase D-3・mfaBox のミラー)。原点隅の s×s×s 立方体を
 * d4Mesh に保持。get_result() で agent へ返す (保存は set_body 経由)。leaf producer (mesh 出力)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"d4/c++/d4Mesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/d4aCube_.h"

#include	<stdexcept>
#include	<stdlib.h>

CLASS_TINYSTATE(d4/c++/d4aCube,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	d4aCube_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<d4Mesh>	mesh;
private:
	TS_DEFARGS
};

TS_END_IMPLEMENT

TS_BEGIN_INTERFACE
#include	"ts2/c++/sRptr.h"
#include	"ts2/c++/sArray.h"
#include	"ts2/c++/stdString.h"
class ptsObject;
class pigData;
class stdString;
class d4Mesh;
TS_END_INTERFACE

#endif


d4aCube_::d4aCube_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
d4aCube_::compute()
{

	/* ★ ホスト側の安全網 (ptsCalcBody の try/catch) の**テスト用フック** (2026-08-26)。
	 * d4 は **exec_default=EXEC_THREAD = in-proc** なので、ここで投げると planner と同じ
	 * プロセスの専用スレッドで例外が飛ぶ。網が無ければ **planner ごと terminate (SIGABRT)**、
	 * 網があれば「module threw an uncaught exception: ...」というエラーで planner は生き残る。
	 * ⚠ 既存の PIG_TEST_* フック (PIG_TEST_SLOW / PIG_TEST_RAISE_SIGNAL) と同じ位置づけ。
	 *   実モジュールに置くと本番経路に試験用の分岐が残るので、**テスト用の d4 に置く**。 */
	if ( ::getenv("PIG_TEST_MODULE_THROW") )
		throw std::runtime_error("deliberate throw from d4_cube (PIG_TEST_MODULE_THROW)");
	int na = ( args != 0 ) ? args->length() : 0;
	double s = ( na > 0 ) ? (*args)[0]->get_flt() : 1.0;
	mesh = d4Mesh::cube(s);
}

sPtr<pigData>
d4aCube_::get_result()
{
	return ( result != thNULL ) ? result : sPtr<pigData>::d_cast(mesh);
}
