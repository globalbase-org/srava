/*
 * mfaCast — cast("manifold", mesh) の計算本体(mf 版・cgaCast のミラー・#3404)。
 *   mf agent(Manifold カーネル)側の cast は入力 mfMesh をそのまま出力する identity。
 *   引数: args=[type_string(inline・"manifold"), mesh(cache)]。mesh は args[1](mf リーダが decode)。
 *
 *   損失方向 exact→float: mf リーダ(create_for_meta)が "MESH"(CGAL 3D exact)も受理し、有理数文字列を
 *   double 化して Manifold を作る(mfMesh::decode_mesh_exact・#3404 Phase D)。よって cast("manifold",
 *   exactMesh) は identity のまま成立(リーダが MESH→double 変換を担う)。無損失方向 float→exact は cgaCast。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"mf/c++/mfMesh.h"
#include	"mf/c++/ptsmfWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/mfaCast_.h"

CLASS_TINYSTATE(mf/c++/mfaCast,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	mfaCast_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<mfGeom>	mesh;
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
class mfGeom;
TS_END_INTERFACE

#endif


mfaCast_::mfaCast_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
mfaCast_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<mfGeom> in = ( na > 1 ) ? sPtr<mfGeom>::d_cast((*args)[1]) : sPtr<mfGeom>();
	if ( ! in.is_notNull() ) {
		result = thNEW(pigDataError,(thNEW(stdString,("cast: needs a mesh/cross (2nd arg)"))));
		return;
	}
	mesh = in;   /* identity(mfMesh/mfCross 共通・既に Manifold)。cg→mf downgrade は reader が有理数→double 化して渡す */
}

/* この演算の結果 (#3406, 2026-07-30 メモ: get_body/get_result を統一)。エラー時は
 * compute() が result にエラー値を残して mesh を未設定のまま return するので result 優先。
 * 成功時は result=thNULL のままmeshを返す。保存(Writer 起動)は agent が出力 pigDataCache
 * の set_body 経由で行う。 */
sPtr<pigData>
mfaCast_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
