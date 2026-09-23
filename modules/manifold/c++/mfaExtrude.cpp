/*
 * mfaExtrude — extrude(cross, h) の計算本体(mf 版・cgaExtrude のミラー)。
 * 2D 断面(mfCross)を Z 方向へ h 押し出して 3D(mfMesh)。Manifold::Extrude に委譲(XY→Z・cg と同軸)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"mf/c++/mfMesh.h"
#include	"mf/c++/ptsmfWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/mfaExtrude_.h"

CLASS_TINYSTATE(mf/c++/mfaExtrude,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	mfaExtrude_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<mfMesh>	mesh;
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
class mfMesh;
TS_END_INTERFACE

#endif


mfaExtrude_::mfaExtrude_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
mfaExtrude_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<mfCross> in = ( na > 0 ) ? sPtr<mfCross>::d_cast((*args)[0]) : sPtr<mfCross>();
	double h = ( na > 1 ) ? (*args)[1]->get_flt() : 1.0;
	if ( ! in.is_notNull() ) {
		result = mfa_err(thNEW(stdString,("extrude: needs a 2D polygon")));
		return;
	}
	if ( h == 0.0 ) {
		result = mfa_err(thNEW(stdString,("extrude: height must be non-zero")));
		return;
	}
	manifold::Manifold m = manifold::Manifold::Extrude(in->polys(), h);   /* ★ #3529 */
	/* ★★ #3526: 2D が **枠 (平面)** を持つようになった。押し出しは **world +Z のまま**
	 *   (ひさ判断 ①: 枠の法線に固定すると *斜めに押し出す手段が無くなる*)。
	 *
	 *   世界での立体は { O + xU + yV + t*ẑ : (x,y) ∈ P, t ∈ [0,h] } で、これは局所の
	 *   **まっすぐな角柱** (Manifold::Extrude が作るもの) の **アフィン像**:
	 *       (x,y,t) → O + xU + yV + t*ẑ        行列 = [U V ẑ] ・ 平行移動 = O
	 *   ⇒ 角柱をそのまま作ってから [U V ẑ | O] を当てればよい (厳密)。
	 *
	 *   ★ 体積は area * h * |det| で、det = (U x V)・ẑ = 法線の z 成分。
	 *     45 度傾けた 2x3 を h=1 で押し出すと 6*1*cos45 = 4.2426… = *射影面積 x 長さ*。
	 *   ⚠ det == 0 = **平面が ẑ を含む** = 掃引方向が面の中を向いている ⇒ 立体にならない。
	 *     黙って体積 0 を返さずに明示エラー (#3518 の 5 と同じ判断)。 */
	if ( ! in->frame_is_default() ) {
		const double *U = in->frame_u(), *V = in->frame_v(), *O = in->frame_o();
		double det = ( U[1]*V[2] - U[2]*V[1] ) * 0.0
		           + ( U[2]*V[0] - U[0]*V[2] ) * 0.0
		           + ( U[0]*V[1] - U[1]*V[0] ) * 1.0;   /* (U x V)・ẑ */
		if ( ( det < 0 ? -det : det ) <= 1e-12 ) {
			/* ★ 文言は occt (ocaExtrude.cpp) と **同じ**にしてある — 同じ状況なので。
			 *   ⚠ 片方だけ直さないこと (同じ式を書いた利用者が別の説明を読むことになる)。 */
			result = mfa_err(thNEW(stdString,(
			    "extrude: the sweep direction lies inside the 2D region, so the prism has "
			    "no well-defined inside (its signed volume cancels to 0); move the region "
			    "or the direction so the sweep leaves the surface")));
			return;
		}
		manifold::mat3x4 t(
		    manifold::vec3(U[0], U[1], U[2]),      /* col 0 = U */
		    manifold::vec3(V[0], V[1], V[2]),      /* col 1 = V */
		    manifold::vec3(0.0, 0.0, 1.0),         /* col 2 = ẑ (world +Z) */
		    manifold::vec3(O[0], O[1], O[2]));     /* col 3 = O */
		m = m.Transform(t);
	}
	mesh = thNEW(mfMesh,(m));
}

/* この演算の結果 (#3406, 2026-07-30 メモ: get_body/get_result を統一)。エラー時は
 * compute() が result にエラー値を残して mesh を未設定のまま return するので result 優先。
 * 成功時は result=thNULL のままmeshを返す。保存(Writer 起動)は agent が出力 pigDataCache
 * の set_body 経由で行う。 */
sPtr<pigData>
mfaExtrude_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
