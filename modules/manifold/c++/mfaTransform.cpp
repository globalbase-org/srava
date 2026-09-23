/*
 * mfaTransform — transform(mesh, matrix) の計算本体(ptsCalcBody 派生)= 低レベル一般アフィン変換。
 * args=[mesh(mfMesh), matrix(array)]。matrix は行優先の 12 要素(3x4 アフィン m00..m23)または
 * 16 要素(4x4。最終行 0,0,0,1 は無視)。各要素 double を K::FT に格納(EPECK 座標のまま近似)。
 * 反射(det<0)は cga_apply_affine が向き反転で補正。要素数不正は result にエラーを立て A_ERROR。
 * 高レベルの translate/rotate/mirror はこの一般変換の特例。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"mf/c++/mfMesh.h"
#include	"mf/c++/mfAffineDemote.h"   /* ★ #3554 段5 */
#include	"common/affine.h"   /* アフィン変換の共通規約 (#3486) */
#include	"mf/c++/ptsmfWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/mfaTransform_.h"

CLASS_TINYSTATE(mf/c++/mfaTransform,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	mfaTransform_(
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


mfaTransform_::mfaTransform_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
mfaTransform_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<mfGeom> in = ( na > 0 ) ? sPtr<mfGeom>::d_cast((*args)[0]) : sPtr<mfGeom>();
	sPtr<pigData> arg = ( na > 1 ) ? (*args)[1] : sPtr<pigData>();

	/* ★ 引数の解釈と行列の組み立ては **common/affine.h** (カーネル非依存・7 モジュール共通)。
	 *   受け付ける書き方だけでなく **拒否の理由** もそこに集約してある (#3486)。
	 *   理由の受け皿 buf は呼び手が持つ (モジュール側に static を置かない)。 */
	double e[12];
	const char *why = 0;
	char buf[256];
	if ( ! srava_affine::matrix_transform(arg, e, &why, buf, (int)sizeof buf) ) {
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
	mf_demote_if_flat(in, mesh, e);   /* ★ #3554 段5: xy に帰着するなら cross2d のまま */
	if ( in.is_notNull() && ! mesh.is_notNull() ) {
		result = mfa_err(thNEW(stdString,(
		    "transform: this transform flattens the 2D region onto a line (its plane collapses)")));
		mesh = thNEW(mfMesh,(manifold::Manifold()));
		return;
	}
	/* ★ #3498: Manifold::Transform は **遅延**する (mfMesh.h の mf_eval_err の一覧)。 */
	if ( (result = mf_eval_err(mesh, brk_, "transform")) != thNULL ) mesh = thNULL;
}

/* この演算の結果 (#3406, 2026-07-30 メモ: get_body/get_result を統一)。エラー時は
 * compute() が result にエラー値を残して mesh を未設定のまま return するので result 優先。
 * 成功時は result=thNULL のままmeshを返す。保存(Writer 起動)は agent が出力 pigDataCache
 * の set_body 経由で行う。 */
sPtr<pigData>
mfaTransform_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
