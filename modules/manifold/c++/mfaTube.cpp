/*
 * mfaTube — tube(path[, segs]) の計算本体(mf 版・cgaTube のミラー)= パスに沿って丸断面を掃引した管。
 *
 * **次元ディスパッチ**: パス頂点の位置の長さで 3D / 2D を振り分ける(cgaTube と同一)。
 *   - 3D: path = [[[x,y,z], r], ...] → 3D 折れ線まわりに丸断面を掃引した立体(mfMesh)。
 *   - 2D: path = [[[x,y],   r], ...] → 2D 折れ線を半径 r(=半幅)で太らせた帯領域(mfCross)。
 *
 * ★掃引の幾何(接線・rotation-minimizing frame・断面リング・側壁・キャップ・2D スタンプ)は
 *   cgaTube と **同じ共通ヘッダ** src/h/common/tube.h が生成する(#3415・geodesic.h と同方針)。
 *   よって cgal / manifold で頂点座標・三角形の並びが一致する。このファイルに残るのは
 *   「pigData 引数の読み取り」と「Manifold 表現への流し込み」だけ:
 *     - 3D: MeshGL64(double 頂点 + 三角形)を組んで Manifold へ
 *     - 2D: スタンプ(円・台形)の輪郭列を CrossSection の **Positive フィル則**で合併
 *           (CCW 輪郭の重なりは winding>0 = 和集合。Boolean を N 回回すより安い)
 *
 * tube が mf にも在ることで、tube 主体のモデル(examples/pipe_clearance.sra・lib/std/guide.sra 等)が
 * 既定カーネル(manifold)の in-proc 経路に丸ごと乗る(#3415 の目的)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"mf/c++/mfMesh.h"
#include	"mf/c++/ptsmfWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	"common/tube.h"   /* 掃引の共通生成器(cgal.so と共有) */
#include	<vector>
#include	"_ts2/c++/mfaTube_.h"

CLASS_TINYSTATE(mf/c++/mfaTube,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	mfaTube_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<mfGeom>	geom;   /* 3D=mfMesh / 2D=mfCross(次元ディスパッチ) */
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


mfaTube_::mfaTube_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

namespace {
using srava_geo::TubeV3;

/* 3D Sink: 共通生成器が出す頂点/三角形を MeshGL64 へ積む(mfMesh::geodesic の MfGeoSink と同型)。 */
struct MfTubeSink {
	manifold::MeshGL64 m;
	MfTubeSink() { m.numProp = 3; }
	int add_vertex(double x, double y, double z) {
		int id = (int)(m.vertProperties.size() / 3);
		m.vertProperties.push_back(x);
		m.vertProperties.push_back(y);
		m.vertProperties.push_back(z);
		return id;
	}
	void add_triangle(int a, int b, int c) {
		m.triVerts.push_back((uint64_t)a);
		m.triVerts.push_back((uint64_t)b);
		m.triVerts.push_back((uint64_t)c);
	}
};

/* 2D Sink: スタンプ輪郭(CCW)を溜める。合併は CrossSection の Positive フィル則が行う。 */
struct MfRibbonSink {
	manifold::Polygons polys;
	void add_ring(const double *xy, int npts) {
		manifold::SimplePolygon ring;
		ring.reserve((size_t)npts);
		for ( int i = 0 ; i < npts ; ++i )
			ring.push_back(manifold::vec2(xy[2*i], xy[2*i+1]));
		polys.push_back(ring);
	}
};
} /* anonymous namespace */

void
mfaTube_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<pigDataArray> path = ( na > 0 ) ? (*args)[0]->obt_array()
	                                     : sPtr<pigDataArray>();
	int segs = ( na > 1 ) ? (int)(*args)[1]->get_int() : 32;   /* 円の辺数(精度ピッチ)。既定 32 */
	if ( segs < 3 ) segs = 3;
	if ( segs > 4096 ) segs = 4096;

	int nraw = path.is_notNull() ? path->length() : 0;
	if ( nraw < 2 ) {
		result = thNEW(pigDataError,(thNEW(stdString,(
		    "tube: needs >= 2 path vertices ([[[x,y,z],r],...] for 3D / [[[x,y],r],...] for 2D)"))));
		return;
	}

	/* ---- パス読み取り: 各要素 [位置, r]。位置の長さ(2/3)で 2D/3D を判定(先頭頂点で確定) ---- */
	std::vector<TubeV3> Praw((size_t)nraw);
	std::vector<double> Rraw((size_t)nraw);
	int dim = 0;
	for ( int i = 0 ; i < nraw ; ++i ) {
		/* 要素は obt_array() で取る (遅延ノードなら compact ゲートウェイが解決する)。
		 * 配列は要素を eager 解決しないので、素の d_cast だと in-proc で null になる。 */
		sPtr<pigDataArray> pr = path->get_ix(thNEW(pigDataInteger,((INTEGER64)i)))->obt_array();
		if ( ! pr.is_notNull() || pr->length() < 2 ) {
			result = thNEW(pigDataError,(thNEW(stdString,(
			    "tube: each vertex must be [pos, r] (pos=[x,y,z] or [x,y])"))));
			return;
		}
		sPtr<pigDataArray> pos = pr->get_ix(thNEW(pigDataInteger,((INTEGER64)0)))->obt_array();
		int pl = pos.is_notNull() ? pos->length() : 0;
		if ( i == 0 ) dim = ( pl >= 3 ) ? 3 : 2;   /* 先頭頂点で次元を確定 */
		if ( pl < dim ) {
			result = thNEW(pigDataError,(thNEW(stdString,(
			    dim == 3 ? "tube: vertex position must be [x,y,z] (3D path)"
			             : "tube: vertex position must be [x,y] (2D path)"))));
			return;
		}
		Praw[(size_t)i] = TubeV3(pos->get_ix(thNEW(pigDataInteger,((INTEGER64)0)))->get_flt(),
		                         pos->get_ix(thNEW(pigDataInteger,((INTEGER64)1)))->get_flt(),
		                         dim == 3 ? pos->get_ix(thNEW(pigDataInteger,((INTEGER64)2)))->get_flt() : 0.0);
		Rraw[(size_t)i] = pr->get_ix(thNEW(pigDataInteger,((INTEGER64)1)))->get_flt();
		if ( Rraw[(size_t)i] < 0.0 ) {
			result = thNEW(pigDataError,(thNEW(stdString,("tube: radius must be >= 0"))));
			return;
		}
	}

	/* ---- 連続重複頂点を間引く(弾かない)。接線が定義できないため。 ---- */
	std::vector<TubeV3> P;
	std::vector<double> R;
	srava_geo::tube_dedup(Praw, Rraw, P, R);
	if ( P.size() < 2 ) {
		result = thNEW(pigDataError,(thNEW(stdString,(
		    "tube: needs >= 2 distinct path vertices (all given vertices coincide)"))));
		return;
	}

	/* ---- 2D: 折れ線を半径 r で太らせた帯領域(可変幅・丸ジョイント)。stamp-and-union。 ---- */
	if ( dim == 2 ) {
		MfRibbonSink sink;
		srava_geo::make_tube_2d(P, R, segs, sink);
		/* CCW 輪郭の重なり = winding>0。Positive フィル則で合併される(mfCross::polygon と同則)。 */
		geom = thNEW(mfCross,(manifold::CrossSection(
		    sink.polys, manifold::CrossSection::FillRule::Positive)));
		return;
	}

	/* ======================= 以下 3D: 折れ線まわりの掃引立体 ======================= */
	MfTubeSink sink;
	int st = srava_geo::make_tube_3d(P, R, segs, sink);
	if ( st == srava_geo::TUBE_ERR_ZERO_SEGMENT ) {
		result = thNEW(pigDataError,(thNEW(stdString,(
		    "tube: two consecutive zero-radius vertices (degenerate segment)"))));
		return;
	}
	if ( st != srava_geo::TUBE_OK ) {
		result = thNEW(pigDataError,(thNEW(stdString,("tube: duplicate consecutive path vertices"))));
		return;
	}
	/* 共通生成器は外向き右手系で三角形を出すので、cg 側のような向き反転の保険は要らない
	 * (Manifold は逆向きだと体積が負になるだけで検出もできるが、そもそも起きない)。 */
	geom = thNEW(mfMesh,(manifold::Manifold(sink.m)));
}

/* この演算の結果 (#3406, 2026-07-30 メモ: get_body/get_result を統一)。エラー時は
 * compute() が result にエラー値を残して geom を未設定のまま return するので result 優先。
 * 成功時は result=thNULL のまま geom を返す。保存(Writer 起動)は agent が出力 pigDataCache
 * の set_body 経由で行う。 */
sPtr<pigData>
mfaTube_::get_result()
{
	return ( result != thNULL ) ? result : geom;
}
