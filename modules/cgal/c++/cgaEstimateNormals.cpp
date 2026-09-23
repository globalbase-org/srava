/*
 * cgaEstimateNormals — estimate_normals(p [, k]) の計算本体 (#3528)。点群 → 法線つきの点群。
 *
 * ★★ なぜ **独立した op** なのか (#3528 の核心): 法線が無いときに (0,0,1) のような **既定値で
 *   埋めてはいけない**。Poisson も RANSAC もエラーにならずに走り、**静かに嘘の形**を返すため。
 *   要るのは「既定値」ではなく「法線が無いときの既定の振る舞い」で、それは
 *     ・推定は独立した op にして、**利用者が見られる / 差し替えられる**値にする
 *     ・法線を要求する op は、無ければ **明示エラー** (黙って推定しない)
 *   の 2 つ。構造は convex_decomposition(m) → part(d,i) と同じ = 重い中間結果を独立した
 *   キャッシュ可能な値にする。
 *
 * ★★ 印は 2 つ立つ — 「法線あり」と「向き付けあり」。
 *     pca_estimate_normals   接平面の PCA。**向きは決まらない** (RANSAC はこれで足りる)
 *     mst_orient_normals     kNN グラフ上の最小全域木で向きを伝播 (Poisson はこれが要る)
 *   ⚠ 向き付けに失敗した点が残ったら、**点を捨てずに** 向き付けの印だけ下ろす。
 *     CGAL の作法は「向き付かなかった点を erase する」だが、それをすると **黙って点が減る** —
 *     点群の点数は利用者が数えている量なので、減らすなら明示でなければならない。
 *     法線自体は (向きなしとして) 有効なので、RANSAC には使える。
 *   ⚠ mst_orient_normals は点の **並びを変えうる** (向き付かなかったものを後ろへ回す)。
 *     点群に並びの約束は無いので許容する。
 *
 * ⚠ カーネルは **EPICK (double)**。点集合処理 (kd-tree / PCA / MST) は厳密数と相性が悪く、
 *   そもそも点群の座標は測った値なので、EPECK へ上げる意味が無い (cgMesh とは別のカーネル)。
 *
 * ★ 点群型 (pt-cloud3d) は **中立の libsrava_pt** が持つ。cgal はそれを借りるだけで、
 *   自分のクラスも wire 形式も作らない (occt_mf が mfGeom を借りるのと同じ作法)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"cg/c++/cgMesh.h"          /* cga_err (モジュール名つきエラー) */
#include	"pt/c++/ptCloud.h"
#include	"ts2/c++/stdString.h"
/* ⚠ #3545 段 2: CGAL の include を **落とした**。点集合処理 (EPICK + pca/mst) の本体は
 *   libsrava_cg の cg_estimate_normals へ移した (宣言は cg/c++/cgMesh.h)。
 *   ★ ここに置くと EPICK の述語が @c _error_handler の実体を、@c CGAL::to_double が
 *     @c relative_precision_of_to_double を cgal.so にも作る。 */
#include	"_ts2/c++/cgaEstimateNormals_.h"
#include	<vector>
#include	<utility>
#include	<exception>
#include	<stdio.h>

CLASS_TINYSTATE(cg/c++/cgaEstimateNormals,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	cgaEstimateNormals_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<ptCloud>	cloud;
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
class ptCloud;
TS_END_INTERFACE

#endif


cgaEstimateNormals_::cgaEstimateNormals_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

/* 点集合処理の既定近傍数。CGAL の例と同じ 18 (少なすぎると面の推定が暴れる)。 */
#define CGA_EN_DEFAULT_K	18

void
cgaEstimateNormals_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<ptCloud> in = ( na > 0 ) ? sPtr<ptCloud>::d_cast((*args)[0]) : sPtr<ptCloud>();
	if ( ! in.is_notNull() ) {
		result = cga_err(thNEW(stdString,("estimate_normals: needs a point cloud")));
		return;
	}
	if ( in->dim() != 3 ) {
		result = cga_err(thNEW(stdString,(
		    "estimate_normals: only 3D point clouds have normals (pt-cloud3d)")));
		return;
	}
	int np = in->np();
	if ( np < 3 ) {
		result = cga_err(thNEW(stdString,(
		    "estimate_normals: needs at least 3 points to fit a tangent plane")));
		return;
	}
	int k = ( na > 1 ) ? (int)(*args)[1]->get_int() : CGA_EN_DEFAULT_K;
	if ( k < 1 )      k = 1;
	if ( k > np - 1 ) k = np - 1;   /* 近傍数は自分を除く点数まで */

	const std::vector<double> &X = in->xyz();
	sPtr<ptCloud> out = thNEW(ptCloud,());
	out->set_dim(3);
	/* ⚠ 向き付かなかった点が 1 つでも残ったら、**点は残したまま**印だけ下ろす
	 *   (上のコメントの理由)。法線そのものは向きなしとして有効。 */
	int  oriented = 0;
	char why[256];
	why[0] = 0;
	if ( cg_estimate_normals(X.empty() ? 0 : &X[0], np, k,
	                         out->xyz(), out->nrm(), &oriented, why, (int)sizeof why) != 0 ) {
		char b[320];
		::snprintf(b, sizeof b, "estimate_normals: CGAL failed (%s)", why);
		result = cga_err(thNEW(stdString,(b)));
		return;
	}
	out->set_oriented(oriented);
	cloud = out;
}

/* この演算の結果。エラー時は result 優先。保存は agent が出力 pigDataCache 経由で行う。 */
sPtr<pigData>
cgaEstimateNormals_::get_result()
{
	return ( result != thNULL ) ? result : sPtr<pigData>::d_cast(cloud);
}
