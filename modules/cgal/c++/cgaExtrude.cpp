/*
 * cgaExtrude — extrude(poly2d, h) の計算本体(ptsCalcBody 派生)= 2D→3D の橋。
 * 入力 cgMesh2D の各穴あき多角形を高さ h で押し出して角柱メッシュ(cgMesh3D)を作る。
 *   - 天/底キャップ: 外周+穴を制約にした CDT(制約付き Delaunay)で材料側の三角形を集める(穴=トンネル)。
 *   - 側壁: 外周(CCW)と各穴(CW)の各辺を四角形で。穴の壁は内向き(トンネル面)。
 * prism(n,h,r) は ngon の extrude と同形。穴ありも対応(2D difference の結果を立体化できる)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"cg/c++/cgMesh.h"
#include	"cg/c++/ptscgWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/cgaExtrude_.h"

#include	<map>
#include	<list>
#include	<vector>

CLASS_TINYSTATE(cg/c++/cgaExtrude,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	cgaExtrude_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<cgMesh3D>	mesh;
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
class cgMesh3D;
TS_END_INTERFACE

#endif


cgaExtrude_::cgaExtrude_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

/* ★★ #3535②: **この TU は CGAL を 1 つも参照しない**。CDT の道具一式と押し出しの構成は
 * libsrava_cg 側 (cgMesh3D::build_extrude) へ移した。⚠ CGAL は 6.x でヘッダオンリーなので、
 * ここで触ると **この .so の中に CGAL の可変大域状態 (_error_handler / _error_behaviour) の
 * 実体**ができて libsrava_cg 側と別物になる (理由は cgMesh.h の build_box の宣言のところ)。 */

void
cgaExtrude_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<cgMesh2D> in = ( na > 0 ) ? sPtr<cgMesh2D>::d_cast((*args)[0]) : sPtr<cgMesh2D>();
	double h = ( na > 1 ) ? (*args)[1]->get_flt() : 1.0;

	mesh = thNEW(cgMesh3D,());
	if ( ! in.is_notNull() ) {
		result = cga_err(thNEW(stdString,("extrude: needs a 2D polygon")));
		return;
	}
	if ( h == 0.0 ) {
		result = cga_err(thNEW(stdString,("extrude: height must be non-zero")));
		return;
	}

	/* ★ #3535②: 構成は libsrava_cg 側 (cgMesh3D::build_extrude)。
	 *   ⚠ 枠のアフィン当て (下) は double と apply_affine だけなので、こちらに残してよい。 */
	mesh->build_extrude(in, h);

	/* ★★ #3526: 2D が **枠 (平面)** を持つようになった。押し出しは **world +Z のまま**
	 *   (ひさ判断 ①: 枠の法線に固定すると *斜めに押し出す手段が無くなる*)。
	 *
	 *   世界での立体は { O + xU + yV + t*ẑ : (x,y) ∈ P, t ∈ [0,h] } で、これは上で作った
	 *   **局所のまっすぐな角柱**の アフィン像:
	 *       (x,y,t) → O + xU + yV + t*ẑ        行列 = [U V ẑ] ・ 平行移動 = O
	 *   ⇒ 角柱をそのまま作ってから [U V ẑ | O] を当てればよい (厳密 — 枠は double だが
	 *     K::FT へ上げるので、そこから先の座標は厳密なまま)。
	 *
	 *   ★ 体積は area * h * |det| で、det = (U x V)・ẑ = 法線の z 成分。
	 *   ⚠ det == 0 = **平面が ẑ を含む** = 掃引方向が面の中を向いている ⇒ 立体にならない。
	 *     黙って体積 0 を返さずに明示エラー。★ 文言は occt / manifold と **同じ**。 */
	if ( ! in->frame_is_default() ) {
		const double *U = in->frame_u(), *V = in->frame_v(), *O = in->frame_o();
		double det = U[0]*V[1] - U[1]*V[0];   /* (U x V)・ẑ */
		if ( ( det < 0 ? -det : det ) <= 1e-12 ) {
			result = cga_err(thNEW(stdString,(
			    "extrude: the sweep direction lies inside the 2D region, so the prism has "
			    "no well-defined inside (its signed volume cancels to 0); move the region "
			    "or the direction so the sweep leaves the surface")));
			mesh = thNEW(cgMesh3D,());
			return;
		}
		double e[12] = { U[0], V[0], 0.0, O[0],
		                 U[1], V[1], 0.0, O[1],
		                 U[2], V[2], 1.0, O[2] };
		sPtr<cgMesh> moved = mesh->apply_affine(e);
		sPtr<cgMesh3D> m3 = sPtr<cgMesh3D>::d_cast(moved);
		if ( m3.is_notNull() ) mesh = m3;
	}
}

/* この演算の結果 (#3406, 2026-07-30 メモ: get_body/get_result を統一)。エラー時は
 * compute() が result にエラー値を残して mesh を未設定のまま return するので result 優先。
 * 成功時は result=thNULL のままmeshを返す。保存(Writer 起動)は agent が出力 pigDataCache
 * の set_body 経由で行う。 */
sPtr<pigData>
cgaExtrude_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
