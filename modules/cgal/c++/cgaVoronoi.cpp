/*
 * cgaVoronoi — voronoi(p, box) の op (#3525)。2D 点群 → セルに分けられた 2D 領域。
 *   ★ 計算の本体は cgVoronoi.cpp (libsrava_cg)。**op から CGAL は触らない** (#3535②) —
 *     op の TU が CGAL を使うと、その .so にテンプレートと *CGAL の可変な大域状態* が
 *     もう 1 つ実体化される。ここは引数を解いて渡すだけ。
 *   ★ 箱は @[[x0,y0],[x1,y1]]@ (対角 2 点)。省略できない — わけは cgVoronoi.cpp 冒頭。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"cg/c++/cgMesh.h"
#include	"cg/c++/ptscgWireCacheStreamWriterMesh.h"
#include	"pt/c++/ptCloud.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/cgaVoronoi_.h"
#include	<stdio.h>

CLASS_TINYSTATE(cg/c++/cgaVoronoi,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	cgaVoronoi_(
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


cgaVoronoi_::cgaVoronoi_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
cgaVoronoi_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<ptCloud> in = ( na > 0 ) ? sPtr<ptCloud>::d_cast((*args)[0]) : sPtr<ptCloud>();
	if ( ! in.is_notNull() ) {
		result = cga_err(thNEW(stdString,("voronoi: needs a 2D point cloud (pt-cloud2d)")));
		return;
	}
	/* ★ 次元は **点群が決める** (2D の点群なら 2D の箱・3D なら 3D の箱)。
	 *   ⚠ 箱は省略可にしない (暗黙の箱だと箱の大きさで面積/体積が黙って変わる)。 */
	const int nd = in->dim();
	sPtr<pigDataArray> bx = ( na > 1 ) ? (*args)[1]->obt_array() : sPtr<pigDataArray>();
	if ( ! bx.is_notNull() || bx->length() < 2 ) {
		result = cga_err(thNEW(stdString,(
		    "voronoi: needs a clip box (2 opposite corners) (it cannot be omitted: the cells are "
		    "unbounded, and the box also keeps the far Voronoi vertices from being computed)")));
		return;
	}
	double b[2][3] = {{0,0,0},{0,0,0}};
	for ( int k = 0 ; k < 2 ; ++k ) {
		sPtr<pigDataArray> c = bx->get_ix(thNEW(pigDataInteger,((INTEGER64)k)))->obt_array();
		if ( ! c.is_notNull() || c->length() < nd ) {
			char m[160];
			::snprintf(m, sizeof m, "voronoi: each corner of the box must have %d coordinate(s) "
			                        "(the point cloud is %dD)", nd, nd);
			result = cga_err(thNEW(stdString,(m)));
			return;
		}
		for ( int d = 0 ; d < nd ; ++d )
			b[k][d] = c->get_ix(thNEW(pigDataInteger,((INTEGER64)d)))->get_flt();
	}
	/* ★ 隅の与え方は問わない (min/max に直す)。 */
	double bmin[3], bmax[3];
	for ( int d = 0 ; d < nd ; ++d ) {
		bmin[d] = ( b[0][d] < b[1][d] ) ? b[0][d] : b[1][d];
		bmax[d] = ( b[0][d] < b[1][d] ) ? b[1][d] : b[0][d];
	}
	/* ★ わけは **こちらのバッファ**へ受ける (010c39f の作法・モジュール大域に溜めない)。 */
	char why[256];
	why[0] = '\0';
	if ( nd == 3 ) mesh = sPtr<cgMesh>::d_cast(cg_voronoi_3d(in, bmin, bmax, why, (int)sizeof why));
	else           mesh = sPtr<cgMesh>::d_cast(cg_voronoi_2d(in, bmin, bmax, why, (int)sizeof why));
	if ( ! mesh.is_notNull() ) {
		char m[320];
		::snprintf(m, sizeof m, "voronoi: %s", why[0] ? why : "could not build the diagram");
		result = cga_err(thNEW(stdString,(m)));
	}
}

/* この演算の結果。エラー時は compute() が result にエラー値を残して mesh 未設定で return するので
 * result 優先。保存 (Writer 起動) は agent が出力 pigDataCache の set_body 経由で行う。 */
sPtr<pigData>
cgaVoronoi_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
