/*
 * ocaVerts — verts(v) — **全頂点をまとめて点群で返す** (#3547 ④ / #3527 の規約④)。
 *
 * ★★ なぜ点群型 (pt-cloud3d / pt-cloud2d) で返すか — cgaVerts / guaVerts と同じ理由:
 *   ① N 点は **AK_INLINE の値には載らない** (vert(v,i) は 1 点なので値でよい)。
 *   ② 点群は **カーネル中立で外部ライブラリを持たない型** (#3528) なので、借りているのは
 *      *幾何の機能* ではなく **値の器** だけ ⇒ モジュール境界の約束①に触れない。
 *      ★ 前例 2 つ — cgal (verts / estimate_normals) と geomutils (verts)。
 *      ⇒ 消費側は sig に並べて LINK に srava_pt を足すだけ (新しいクラスも wire 形式も作らない)。
 *   ③ そのまま distance / closest / hull / delaunay が食える。
 *
 * ⚠⚠ vert(v,i) と **同じ列を同じ順** ⇒ verts(v) の i 番目 == vert(v,i) (検査がこの等式を見る)。
 * ★ 次元は名乗りの規約どおり — oc-cross2d は 2 ・ それ以外は world の 3。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"oc/c++/ocShape.h"
#include	"pt/c++/ptCloud.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ocaVerts_.h"
#include	<vector>

CLASS_TINYSTATE(oc/c++/ocaVerts,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ocaVerts_(
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

ocaVerts_::ocaVerts_(TS_ARGS0)
	: ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
	TS_CPARGS0
}

void
ocaVerts_::compute()
{
	ocShape::ensure_init();
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<ocShape>  s3 = ( na > 0 ) ? sPtr<ocShape>::d_cast((*args)[0])  : sPtr<ocShape>();
	sPtr<ocFace2D> f2 = ( na > 0 ) ? sPtr<ocFace2D>::d_cast((*args)[0]) : sPtr<ocFace2D>();
	if ( ! s3.is_notNull() && ! f2.is_notNull() ) {
		result = oca_err(thNEW(stdString,("verts: needs an OCCT shape or 2D region")));
		return;
	}
	std::vector<double> xyz;
	const int n = s3.is_notNull() ? s3->verts_all(xyz) : f2->verts_all(xyz);
	if ( n <= 0 ) {
		result = oca_err(thNEW(stdString,("verts: this value has no vertices")));
		return;
	}
	const int dim = ( f2.is_notNull() && f2->on_z0_plane() ) ? 2 : 3;
	sPtr<ptCloud> out = thNEW(ptCloud,());
	std::vector<double> &dst = out->xyz();
	dst.clear();
	for ( int i = 0 ; i < n ; ++i )
		for ( int k = 0 ; k < dim ; ++k ) dst.push_back(xyz[(size_t)i * 3 + k]);
	out->set_dim(dim);
	/* ★ 法線は付けない (要るなら estimate_normals を明示的に通す・guaVerts と同じ判断)。 */
	cloud = out;
}

sPtr<pigData>
ocaVerts_::get_result()
{
	return ( result != thNULL ) ? result : sPtr<pigData>::d_cast(cloud);
}
