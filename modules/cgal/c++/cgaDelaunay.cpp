/*
 * cgaDelaunay — delaunay(p) の op (#3525)。2D 点群 → 三角形を片として並べた 2D 領域。
 *   ★ 計算の本体は cgDelaunay.cpp (libsrava_cg)。**op から CGAL は触らない** (#3535②)。
 *   ⚠ 三角形の **番号は実装依存** — 「i 番目の三角形」を約束に使わないこと (わけは cgDelaunay.cpp)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"cg/c++/cgMesh.h"
#include	"cg/c++/ptscgWireCacheStreamWriterMesh.h"
#include	"pt/c++/ptCloud.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/cgaDelaunay_.h"
#include	<stdio.h>

CLASS_TINYSTATE(cg/c++/cgaDelaunay,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	cgaDelaunay_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<cgMesh>	mesh;
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


cgaDelaunay_::cgaDelaunay_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
cgaDelaunay_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<ptCloud> in = ( na > 0 ) ? sPtr<ptCloud>::d_cast((*args)[0]) : sPtr<ptCloud>();
	if ( ! in.is_notNull() ) {
		result = cga_err(thNEW(stdString,("delaunay: needs a 2D point cloud (pt-cloud2d)")));
		return;
	}
	/* ★ わけは **こちらのバッファ**へ受ける (010c39f の作法・モジュール大域に溜めない)。 */
	char why[256];
	why[0] = '\0';
	/* ★ 次元は **点群が決める** (voronoi と同じ)。 */
	if ( in->dim() == 3 ) mesh = sPtr<cgMesh>::d_cast(cg_delaunay_3d(in, why, (int)sizeof why));
	else                  mesh = sPtr<cgMesh>::d_cast(cg_delaunay_2d(in, why, (int)sizeof why));
	if ( ! mesh.is_notNull() ) {
		char m[320];
		::snprintf(m, sizeof m, "delaunay: %s", why[0] ? why : "could not triangulate");
		result = cga_err(thNEW(stdString,(m)));
	}
}

/* この演算の結果。エラー時は compute() が result にエラー値を残して mesh 未設定で return するので
 * result 優先。保存 (Writer 起動) は agent が出力 pigDataCache の set_body 経由で行う。 */
sPtr<pigData>
cgaDelaunay_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
