/*
 * cgaPrism — 正 n 角柱生成の計算本体(ptsCalcBody 派生)。args=[n, height, radius](INLINE)。
 * CGAL::make_regular_prism で Surface_mesh(EPECK)を作り三角形化、cgMesh に保持。cgaBox と同型。
 * n 角形頂点は cos/sin(double)由来だが EPECK に厳密格納される(多面体としては厳密)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"cg/c++/cgMesh.h"
#include	"cg/c++/ptscgWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	"common/segs.h"   /* ★ #3530: segs / n の共通検査 */
#include	"_ts2/c++/cgaPrism_.h"


CLASS_TINYSTATE(cg/c++/cgaPrism,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	cgaPrism_(
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
class cgMesh;
TS_END_INTERFACE

#endif


cgaPrism_::cgaPrism_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
cgaPrism_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	int    n = ( na > 0 ) ? (int)(*args)[0]->get_int() : 3;
	double h = ( na > 1 ) ? (*args)[1]->get_flt() : 1.0;
	double r = ( na > 2 ) ? (*args)[2]->get_flt() : 1.0;
	/* ★ #3516: 他の 5 カーネルは元から明示エラーにしていた。cgal だけ **黙って n を 3 へ
	 *   丸めて**いたので揃える (同じ式が cgal では通り nef では落ちる、という食い違いを消す)。 */
	if ( srava_geo::check_sides(n, &n) != srava_geo::SEGS_OK ) {   /* ★ #3530: n は形そのもの = 既定値なし */
		result = cga_err(thNEW(stdString,(srava_geo::sides_error("prism").c_str()))); return; }
	if ( !(h > 0) )  { result = cga_err(thNEW(stdString,("prism: height must be > 0"))); return; }
	if ( !(r > 0) )  { result = cga_err(thNEW(stdString,("prism: radius must be > 0"))); return; }

	/* ★ #3535②: 構成は libsrava_cg 側 (cgMesh3D::build_regular_prism) が持つ。
	 *   高さを Y→Z へ移す回転もそちらへ移した (理由は cgMesh.h の宣言のところ)。 */
	mesh = thNEW(cgMesh3D,());
	mesh->build_regular_prism(n, h, r);
}

/* この演算の結果 (#3406, 2026-07-30 メモ: get_body/get_result を統一)。エラー時は
 * compute() が result にエラー値を残して mesh を未設定のまま return するので result 優先。
 * 成功時は result=thNULL のままmeshを返す。保存(Writer 起動)は agent が出力 pigDataCache
 * の set_body 経由で行う。 */
sPtr<pigData>
cgaPrism_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
