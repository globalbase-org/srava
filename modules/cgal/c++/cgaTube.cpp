/*
 * cgaTube — tube_ruled(path[, segs]) の計算本体(ptsCalcBody 派生)= パスに沿って丸断面を掃引した管。
 *
 * **次元ディスパッチ**: パス頂点の位置の長さで 3D / 2D を振り分ける(offset/transform 等と同方針)。
 *   - 3D: path = [[[x,y,z], r], ...] → 3D 折れ線まわりに丸断面を掃引した立体(cgMesh3D)。「蛇」。
 *   - 2D: path = [[[x,y],   r], ...] → 2D 折れ線を半径 r(=半幅)で太らせた帯領域(cgMesh2D)。「2D tube」。
 *         r は 3D と同じく**半径(半幅)**。各頂点で太さが変わる可変幅。丸ジョイント/丸キャップ。
 *
 * ★掃引の幾何そのもの(接線・rotation-minimizing frame・断面リング・側壁・キャップ・2D スタンプ)は
 *   **カーネル非依存**なので共通ヘッダ src/h/common/tube.h に括り出してある(#3415・geodesic.h と同方針)。
 *   manifold.so の mfaTube が同じヘッダを使うので、両カーネルの頂点・三角形の並びが一致する。
 *   このファイルに残るのは「pigData 引数の読み取り」と「CGAL 表現への流し込み」だけ:
 *     - 3D: Surface_mesh に頂点/三角形を積む(座標は double → K::FT。revolve と同方針)
 *     - 2D: スタンプ(円・台形)を Polygon_set_2 で union する
 *   ブール演算は 3D では不使用(掃引は構成的に閉多様体)。
 *
 * **連続重複頂点は弾かずに間引く**(接線が定義できないので)。spline 等が区間境界で点を重複させても通る。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"cg/c++/cgMesh.h"
#include	"cg/c++/ptscgWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	"common/segs.h"   /* ★ #3530: segs / n の共通検査 */
#include	"common/tube.h"   /* 掃引の共通生成器(manifold.so と共有) */
#include	"_ts2/c++/cgaTube_.h"

#include	<vector>
#include	<cmath>

CLASS_TINYSTATE(cg/c++/cgaTube,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	cgaTube_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<cgMesh>	mesh;   /* 3D=cgMesh3D / 2D=cgMesh2D(次元ディスパッチ) */
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


cgaTube_::cgaTube_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

/* ★★ #3535②: **この TU は CGAL を 1 つも参照しない**。2 つの Sink (CgTubeSink /
 * CgRibbonSink) と外向き保証は libsrava_cg 側 (cgMesh3D::build_tube / cgMesh2D::build_tube)
 * へ移した。⚠ CGAL は 6.x でヘッダオンリーなので、ここで触ると **この .so の中に
 * CGAL の可変大域状態 (_error_handler / _error_behaviour / get_default_random) の実体**が
 * できて libsrava_cg 側と別物になる (理由は cgMesh.h の build_tube の宣言のところ)。 */
using srava_geo::TubeV3;

void
cgaTube_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<pigDataArray> path = ( na > 0 ) ? (*args)[0]->obt_array()
	                                     : sPtr<pigDataArray>();
	int    segs_in = ( na > 1 ) ? (int)(*args)[1]->get_int() : 0;   /* 0 = 未指定 */
	int    segs = 0;
	/* ★★ #3530: segs の意味を全 op / 全カーネルで 1 本に揃えた (src/h/common/segs.h)。
	 *   0 or 省略 = 既定値 / 1,2 / 負 = 明示エラー / 3 以上 = その値。 */
	if ( srava_geo::check_segs(segs_in, 32, &segs) != srava_geo::SEGS_OK ) {
		result = cga_err(thNEW(stdString,(srava_geo::segs_error("tube_ruled").c_str()))); return;
	}

	int nraw = path.is_notNull() ? path->length() : 0;
	if ( nraw < 2 ) {
		result = cga_err(thNEW(stdString,(
		    "tube_ruled: needs >= 2 path vertices ([[[x,y,z],r],...] for 3D / [[[x,y],r],...] for 2D)")));
		return;
	}

	/* ---- パス読み取り: 各要素 [位置, r]。位置の長さ(2/3)で 2D/3D を判定(先頭頂点で確定) ---- */
	std::vector<TubeV3> Praw((size_t)nraw);
	std::vector<double> Rraw((size_t)nraw);
	int dim = 0;
	for ( int i = 0 ; i < nraw ; ++i ) {
		/* 要素は obt_array() で取る (mfaTube と同じ作法。配列は要素を eager 解決しない)。 */
		sPtr<pigDataArray> pr = path->get_ix(thNEW(pigDataInteger,((INTEGER64)i)))->obt_array();
		if ( ! pr.is_notNull() || pr->length() < 2 ) {
			result = cga_err(thNEW(stdString,(
			    "tube_ruled: each vertex must be [pos, r] (pos=[x,y,z] or [x,y])")));
			return;
		}
		sPtr<pigDataArray> pos = pr->get_ix(thNEW(pigDataInteger,((INTEGER64)0)))->obt_array();
		int pl = pos.is_notNull() ? pos->length() : 0;
		if ( i == 0 ) dim = ( pl >= 3 ) ? 3 : 2;   /* 先頭頂点で次元を確定 */
		if ( pl < dim ) {
			result = cga_err(thNEW(stdString,(
			    dim == 3 ? "tube_ruled: vertex position must be [x,y,z] (3D path)"
			             : "tube_ruled: vertex position must be [x,y] (2D path)")));
			return;
		}
		Praw[(size_t)i] = TubeV3(pos->get_ix(thNEW(pigDataInteger,((INTEGER64)0)))->get_flt(),
		                         pos->get_ix(thNEW(pigDataInteger,((INTEGER64)1)))->get_flt(),
		                         dim == 3 ? pos->get_ix(thNEW(pigDataInteger,((INTEGER64)2)))->get_flt() : 0.0);
		Rraw[(size_t)i] = pr->get_ix(thNEW(pigDataInteger,((INTEGER64)1)))->get_flt();
		if ( Rraw[(size_t)i] < 0.0 ) {
			result = cga_err(thNEW(stdString,("tube_ruled: radius must be >= 0")));
			return;
		}
	}

	/* ---- 連続重複頂点を間引く(弾かない)。接線が定義できないため。 ---- */
	std::vector<TubeV3> P;
	std::vector<double> R;
	srava_geo::tube_dedup(Praw, Rraw, P, R);
	if ( P.size() < 2 ) {
		result = cga_err(thNEW(stdString,(
		    "tube_ruled: needs >= 2 distinct path vertices (all given vertices coincide)")));
		return;
	}

	/* ---- 2D: 折れ線を半径 r で太らせた帯領域(可変幅・丸ジョイント)。stamp-and-union。 ---- */
	if ( dim == 2 ) {
		sPtr<cgMesh2D> out = thNEW(cgMesh2D,());
		out->build_tube(P, R, segs);
		mesh = out;
		return;
	}

	/* ======================= 以下 3D: 折れ線まわりの掃引立体 ======================= */
	sPtr<cgMesh3D> m3 = thNEW(cgMesh3D,());
	int st = m3->build_tube(P, R, segs);
	if ( st == srava_geo::TUBE_ERR_ZERO_SEGMENT ) {
		result = cga_err(thNEW(stdString,(
		    "tube_ruled: two consecutive zero-radius vertices (degenerate segment)")));
		return;
	}
	if ( st != srava_geo::TUBE_OK ) {
		result = cga_err(thNEW(stdString,("tube_ruled: duplicate consecutive path vertices")));
		return;
	}

	/* ★ 外向き保証 (is_closed / volume / 反転) も build_tube の中でやる。 */
	mesh = m3;
}

/* この演算の結果 (#3406, 2026-07-30 メモ: get_body/get_result を統一)。エラー時は
 * compute() が result にエラー値を残して mesh を未設定のまま return するので result 優先。
 * 成功時は result=thNULL のままmeshを返す。保存(Writer 起動)は agent が出力 pigDataCache
 * の set_body 経由で行う。 */
sPtr<pigData>
cgaTube_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
