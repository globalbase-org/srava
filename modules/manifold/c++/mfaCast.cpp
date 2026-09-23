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
#include	<cstring>
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
		result = mfa_err(thNEW(stdString,("cast: needs a mesh/cross (2nd arg)")));
		return;
	}
	mesh = in;   /* identity(mfMesh/mfCross 共通・既に Manifold)。cg→mf downgrade は reader が有理数→double 化して渡す */

	/* ★★ #3533 規約②: **降格 (mf-face3d → mf-cross2d) だけは identity ではない** (cgaCast と対。
	 *   ⚠ 片方だけ直さないこと)。「空間に置かれた 2D」を「z=0 の簡易表現」と名乗り直す操作
	 *   なので、*本当に z=0 に居るとき* しか許さない ⇒ @frame_is_default()@ が偽なら明示エラー。
	 *   幾何は 1 ミリも動かさない (傾いたものを落とすのは #3534 の project_flatten)。 */
	sPtr<mfCross> c2 = sPtr<mfCross>::d_cast(in);
	if ( c2.is_notNull() && na > 0 ) {
		sPtr<pigData> tv = (*args)[0];
		const char *tname = ( tv.is_notNull() && tv->get_str() != thNULL )
		                  ? tv->get_str()->get_str() : "";
		if ( ::strcmp(tname, "mf-cross2d") == 0 && c2->is_placed() ) {
			if ( ! c2->frame_is_default() ) {
				result = mfa_err(thNEW(stdString,(
				    "cast: this 2D region is placed on another plane, so it cannot be named "
				    "\"mf-cross2d\" (the z=0 representation); cast never moves geometry — "
				    "use project_flatten(...) to drop it onto z=0, or transform it back first")));
				mesh = thNULL;
				return;
			}
			/* 幾何はそのまま・**名乗りだけ**下げる (共有されうる値なので複製する)。 */
			sPtr<mfCross> out = thNEW(mfCross,(c2->polys()));
			out->set_placed(0);
			mesh = sPtr<mfGeom>::d_cast(out);
		}
	}
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
