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
#include	"mf/c++/mfAffineDemote.h"   /* ★ #3554 段5 */
#include	"pig/c++/pigOpMatch.h"   /* ★ 段5: routing と同じ軸判定 */
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
	/* ★★ #3526 (2026-09-13): **2D は枠 (平面) を持つようになった**ので、面外へ出す変換は
	 *   もう断らない — mfCross::apply_affine が z 成分を **枠へ渡す**。
	 *   ⚠ #3518 の 1 の但し書きとして 2940662 で「黙って射影する」のを明示エラーにしたが、
	 *     それは *置き場所を持てなかった* からで、持てるなら断る理由は無い。
	 *   ⚠ 線形部が退化して平面が線に潰れる場合だけ null が返る ⇒ 明示エラーにする。 */
	mesh = ( in.is_notNull() ) ? in->apply_affine(e) : sPtr<mfGeom>();
	/* ★ #3554 段5: routing (rotate#z) と **同じ判定** = 軸が z か。行列で見ると
	 *   rotate(R,"x",180) で sig と実型が食い違う (2026-09-19 に cgal で踏んだ)。 */
	if ( pig_val_axis_is_z(( args != 0 && args->length() > 1 ) ? (*args)[1] : sPtr<pigData>()) )
		mf_demote_if_flat(in, mesh, e);
	if ( in.is_notNull() && ! mesh.is_notNull() ) {
		result = mfa_err(thNEW(stdString,(
		    "rotate: this transform flattens the 2D region onto a line (its plane collapses)")));
		mesh = thNEW(mfMesh,(manifold::Manifold()));
		return;
	}
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
