/*
 * mfaRotate — rotate(mesh, axis, deg) の計算本体(ptsCalcBody 派生)。
 * args=[mesh(mfMesh), axis("x"/"y"/"z" の文字列), deg(度。inline 数値)]。任意角の cos/sin を double で
 * 計算し K::FT に格納(EPECK 座標のまま double 近似 = 「任意角は EPICK 相当に落とす」)。原点まわりの
 * 主軸回転。未対応 axis は result にエラーを立て A_ERROR で伝播。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"mf/c++/mfMesh.h"
#include	"common/affine.h"   /* アフィン変換の共通規約 (#3486) */
#include	"mf/c++/ptsmfWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/mfaRotate_.h"

#include	<math.h>
#include	<string.h>

CLASS_TINYSTATE(mf/c++/mfaRotate,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	mfaRotate_(
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


mfaRotate_::mfaRotate_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
mfaRotate_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<mfGeom> in = ( na > 0 ) ? sPtr<mfGeom>::d_cast((*args)[0]) : sPtr<mfGeom>();
	sPtr<pigData> arg = ( na > 1 ) ? (*args)[1] : sPtr<pigData>();
	double        deg = ( na > 2 ) ? (*args)[2]->get_flt() : 0.0;

	/* ★ 引数の解釈と行列の組み立ては **common/affine.h** (カーネル非依存・7 モジュール共通)。
	 *   受け付ける書き方だけでなく **拒否の理由** もそこに集約してある (#3486)。
	 *   理由の受け皿 buf は呼び手が持つ (モジュール側に static を置かない)。 */
	double e[12];
	const char *why = 0;
	char buf[256];
	if ( ! srava_affine::matrix_rotate(arg, deg, e, &why, buf, (int)sizeof buf) ) {
		result = mfa_err(thNEW(stdString,(why)));
		mesh = thNEW(mfMesh,(manifold::Manifold()));
		return;
	}
	mesh = ( in.is_notNull() ) ? in->apply_affine(e) : sPtr<mfGeom>();
	/* ★ #3498: Manifold::Transform は **遅延**する (mfMesh.h の mf_eval_err の一覧)。 */
	if ( (result = mf_eval_err(mesh, brk_, "rotate")) != thNULL ) mesh = thNULL;
}

/* この演算の結果 (#3406, 2026-07-30 メモ: get_body/get_result を統一)。エラー時は
 * compute() が result にエラー値を残して mesh を未設定のまま return するので result 優先。
 * 成功時は result=thNULL のままmeshを返す。保存(Writer 起動)は agent が出力 pigDataCache
 * の set_body 経由で行う。 */
sPtr<pigData>
mfaRotate_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
