/*
 * cgaCombine — 2 メッシュの単純合体(`a +++ b`)の計算本体(ptsCalcBody 派生)。
 * union と違い corefinement しない: 両メッシュの内容を 1 つに連結するだけ(交差・重なりは
 * 解かない)。ブール演算(|||/&&&/---)を実行する前に、重なり具合を viewer でさっと確認する用途。
 * 多態 op_combine(3D=copy_face_graph で連結 / 2D=Pwh 領域を集める)。次元非依存。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"cg/c++/cgMesh.h"
#include	"cg/c++/cgaBoolError.h"
#include	"cg/c++/ptscgWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/cgaCombine_.h"

CLASS_TINYSTATE(cg/c++/cgaCombine,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	cgaCombine_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<cgMesh>	mesh;     /* compute() の combine 結果(get_result が agent へ返す) */
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
class cgMesh;
TS_END_INTERFACE

#endif


cgaCombine_::cgaCombine_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

/* args[0], args[1] の cgMesh(reader が decode 済)を op_combine で連結 → 結果を cgMesh に。
 * シリアライズは writer 側。重い計算は ACT_START(スレッド)。引き渡しは get_result()(#3406 2026-07-30: get_body 統合)。 */
void
cgaCombine_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<cgMesh> ma = ( na > 0 ) ? sPtr<cgMesh>::d_cast((*args)[0]) : sPtr<cgMesh>();
	sPtr<cgMesh> mb = ( na > 1 ) ? sPtr<cgMesh>::d_cast((*args)[1]) : sPtr<cgMesh>();
	if ( ! ma.is_notNull() || ! mb.is_notNull() ) {
		result = thNEW(pigDataError,(cga_missing_operand_msg("combine",
		     (na>0)?(*args)[0]:sPtr<pigData>(), (na>1)?(*args)[1]:sPtr<pigData>(), na)));
		return;
	}
	mesh = ma->op_combine(mb);   /* 多態: 3D=copy_face_graph / 2D=Pwh 連結。次元を知らない */
	if ( ! mesh.is_notNull() )
		result = thNEW(pigDataError,(thNEW(stdString,("combine: incompatible operands (mixed dimension?)"))));
}

/* この演算の結果 (#3406, 2026-07-30 メモ: get_body/get_result を統一)。エラー時は
 * compute() が result にエラー値を残して mesh を未設定のまま return するので result 優先。
 * 成功時は result=thNULL のままmeshを返す。保存(Writer 起動)は agent が出力 pigDataCache
 * の set_body 経由で行う。 */
sPtr<pigData>
cgaCombine_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
