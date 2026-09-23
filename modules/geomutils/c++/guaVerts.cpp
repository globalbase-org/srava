/*
 * guaVerts — verts(v) — **全頂点をまとめて点群で返す** (#3527 段 5)。
 *
 * ★★ なぜ点群型 (pt-cloud2d / pt-cloud3d) で返すか — cgaVerts と同じ理由:
 *   ① N 万点は **AK_INLINE の値には載らない** (vert(v,i) は 1 点なので値でよい)。
 *   ② 点群は **カーネル中立で外部ライブラリを持たない型** (#3528) なので、借りているのは
 *      *幾何の機能* ではなく **値の器** だけ ⇒ モジュール境界の約束①に触れない。
 *      ★ 前例あり — cgal が既に estimate_normals / verts で ptCloud を作って返している。
 *   ③ そのまま distance / closest / hull / voronoi / delaunay が食える。
 *
 * ★ 3 つ組の「**まとめて**」側 (#3527 の規約④)。⚠ ④ は「**受け皿の型が在るときだけ作る**」
 *   なので、面や片には作らない (faces(B) / parts(v) は列を載せる型が無いので作れない)。
 *
 * ⚠⚠ vert(v,i) と **同じ列を同じ順**で並ぶ ⇒ verts(v) の i 番目 == vert(v,i)。
 *   片方だけ順序を変えると黙ってずれるので、**検査がこの等式を見る** (test/srava_geomutils_vert.sh)。
 *
 * ★ 返る点群の次元は **成分数そのもの**:
 *     3D / face3d … pt-cloud3d   /   cross2d … pt-cloud2d
 *   ⚠⚠ face3d が 3D なのは #3533 の「face3d は world」に揃えたため (ひさ判断 2026-09-17)。
 *     枠の中の 2 成分で返すと hull(verts(sec)) が常に z=0 に出る — *置き場所が黙って落ちる*。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"gu/c++/guGeom.h"
#include	"pt/c++/ptCloud.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/guaVerts_.h"

CLASS_TINYSTATE(gu/c++/guaVerts,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	guaVerts_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	/* ⚠ get_result は **public 側**に置く (protected だと codegen が転送を作らない)。 */
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
class guGeom;
class ptCloud;
TS_END_INTERFACE

#endif

guaVerts_::guaVerts_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
guaVerts_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<guGeom> in = ( na > 0 ) ? sPtr<guGeom>::d_cast((*args)[0]) : sPtr<guGeom>();
	if ( ! in.is_notNull() ) {
		result = gua_err(thNEW(stdString,("verts: needs a mesh or 2D region")));
		return;
	}
	sPtr<ptCloud> out = thNEW(ptCloud,());
	const int dim = in->op_verts(out->xyz());
	if ( dim <= 0 ) {
		result = gua_err(thNEW(stdString,("verts: could not read the vertices")));
		return;
	}
	out->set_dim(dim);
	/* ★ 法線は付けない。要るなら利用者が estimate_normals を明示的に通す。
	 *   ⚠ ここで勝手に付けると「頂点を読んだだけ」のはずの op が重くなる。 */
	cloud = out;
}

/* エラー時は compute() が result にエラーを残すので result 優先。 */
sPtr<pigData>
guaVerts_::get_result()
{
	return ( result != thNULL ) ? result : sPtr<pigData>::d_cast(cloud);
}
