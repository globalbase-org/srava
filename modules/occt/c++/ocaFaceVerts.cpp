/*
 * ocaFaceVerts — face_verts(solid, i) — **面 i の頂点番号** (#3547 ④ / #3527 の規約③)。
 *
 * ★ 番号は vert / verts と **同じ列**を指す (座標から番号へ引き戻すのは丸めが絡んで危ういため、
 *   cgal と同じく番号で返す op を分けてある)。
 * ⚠⚠ cgal の face_verts は三角形なので常に 3 個だが、**B-rep の面は n 個** —
 *   箱の面 = 4 ・ 円筒の側面 = 2 ・ 球の面 = 2 (極)。⇒ 長さは面ごとに違う。
 * ★ 面の並びは face(s,i) と同じ (TopExp_Explorer の順・その決定性は ocaFace.cpp で測ってある)。
 * ⚠ 2D は面 1 枚が値そのものなので sig に置かない (cgal と同じ判断)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"oc/c++/ocShape.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ocaFaceVerts_.h"
#include	<stdio.h>
#include	<vector>

CLASS_TINYSTATE(oc/c++/ocaFaceVerts,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ocaFaceVerts_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

protected:
	virtual void	compute();
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
TS_END_INTERFACE

#endif

ocaFaceVerts_::ocaFaceVerts_(TS_ARGS0)
	: ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
	TS_CPARGS0
}

void
ocaFaceVerts_::compute()
{
	ocShape::ensure_init();
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<ocShape> in = ( na > 0 ) ? sPtr<ocShape>::d_cast((*args)[0]) : sPtr<ocShape>();
	if ( ! in.is_notNull() ) {
		result = oca_err(thNEW(stdString,("face_verts: needs a 3D shape (oc-brep3d)")));
		return;
	}
	const long i = ( na > 1 ) ? (long)(*args)[1]->get_int() : 0;
	std::vector<int> idx;
	const int n = in->face_vert_indices((int)i, idx);
	if ( n < 0 ) {
		char b[192];
		::snprintf(b, sizeof b,
		    "face_verts: index %ld is out of range (this solid has %d face%s; "
		    "valid indices are 0..%d)", i, in->nfaces(), ( in->nfaces() == 1 ) ? "" : "s",
		    ( in->nfaces() > 0 ) ? in->nfaces() - 1 : 0);
		result = oca_err(thNEW(stdString,(b)));
		return;
	}
	sPtr<pigDataArray> arr = thNEW(pigDataArray,());
	for ( size_t k = 0 ; k < idx.size() ; ++k ) arr->push(thNEW(pigDataInteger,((INTEGER64)idx[k])));
	result = arr;
}
