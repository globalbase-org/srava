/*
 * ptaVert — vert(p, i) — 点群の **i 番目の点の座標** (#3527)。
 *
 * ★ 3 つ組の「取り出す」側。点群は @nverts@ で数えられるのに取り出す口が無かった。
 *   ⇒ メッシュ側 (cgal の @vert@) と **同じ名前・同じ約束**にする。
 * ★ 返りは値 — 3D は @[x,y,z]@ ・ 2D は @[x,y]@ (点群は dim を自分で持っている)。
 * ⚠ 索引は **格納順** = @points3d()@ に渡した順 / @verts(m)@ が積んだ順。
 *   ⇒ ★ これは **定義で決まる索引** の側 (#3527 の 2 分類)。入力の順がそのまま出る。
 *     メッシュの @vert@ (列挙順 = 実装依存) とは *索引の由来が違う* ので、
 *     同じ op 名でも「版を跨いで信用してよいか」は別物。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"pt/c++/ptCloud.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ptaVert_.h"
#include	<vector>
#include	<stdio.h>

CLASS_TINYSTATE(pt/c++/ptaVert,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ptaVert_(
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
class ptCloud;
TS_END_INTERFACE

#endif


ptaVert_::ptaVert_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
ptaVert_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<ptCloud> in = ( na > 0 ) ? sPtr<ptCloud>::d_cast((*args)[0]) : sPtr<ptCloud>();
	if ( ! in.is_notNull() ) {
		result = pta_err(thNEW(stdString,("nverts: needs a point cloud")));
		return;
	}
	int idx = ( na > 1 ) ? (int)(*args)[1]->get_int() : 0;
	int n   = in->np();
	if ( idx < 0 || idx >= n ) {
		char b[160];
		::snprintf(b, sizeof b, "vert: index %d is out of range (the cloud has %d point(s))", idx, n);
		result = pta_err(thNEW(stdString,(b)));
		return;
	}
	int dim = in->dim();
	const std::vector<double> &X = in->xyz();
	if ( (int)X.size() < (idx + 1) * dim ) {
		result = pta_err(thNEW(stdString,("vert: the cloud is shorter than its own point count")));
		return;
	}
	sPtr<pigDataArray> arr = thNEW(pigDataArray,());
	for ( int k = 0 ; k < dim ; ++k )
		arr->push(thNEW(pigDataFloat,(X[(size_t)idx * dim + k])));
	result = arr;
}
