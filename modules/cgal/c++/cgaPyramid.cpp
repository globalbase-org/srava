/*
 * cgaPyramid — 正 n 角錐生成の計算本体(ptsCalcBody 派生)。args=[n, height, radius](INLINE)。
 * CGAL::make_pyramid で Surface_mesh(EPECK)を作り三角形化、cgMesh に保持。cgaPrism と同型(関数差)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"cg/c++/cgMesh.h"
#include	"cg/c++/ptscgWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/cgaPyramid_.h"

#include	<CGAL/boost/graph/generators.h>
#include	<CGAL/Polygon_mesh_processing/triangulate_faces.h>

CLASS_TINYSTATE(cg/c++/cgaPyramid,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	cgaPyramid_(
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


cgaPyramid_::cgaPyramid_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
cgaPyramid_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	int    n = ( na > 0 ) ? (int)(*args)[0]->get_int() : 3;
	double h = ( na > 1 ) ? (*args)[1]->get_flt() : 1.0;
	double r = ( na > 2 ) ? (*args)[2]->get_flt() : 1.0;
	if ( n < 3 ) n = 3;

	mesh = thNEW(cgMesh3D,());
	cgMesh::Mesh& m = mesh->mesh();
	CGAL::make_pyramid((unsigned)n, m, cgMesh::Point_3(0,0,0), h, r, true);
	/* CGAL は高さを Y 軸に作る → X 軸 +90°回転 (x,y,z)→(x,-z,y) で高さを Z 軸へ
	 * (prism/extrude/box と統一。底面 n 角形は z=0 の XY 平面・頂点は z=h)。 */
	for ( cgMesh::Mesh::Vertex_index v : m.vertices() ) {
		cgMesh::Point_3 p = m.point(v);
		m.point(v) = cgMesh::Point_3(p.x(), -p.z(), p.y());
	}
	CGAL::Polygon_mesh_processing::triangulate_faces(m);
}

/* この演算の結果 (#3406, 2026-07-30 メモ: get_body/get_result を統一)。エラー時は
 * compute() が result にエラー値を残して mesh を未設定のまま return するので result 優先。
 * 成功時は result=thNULL のままmeshを返す。保存(Writer 起動)は agent が出力 pigDataCache
 * の set_body 経由で行う。 */
sPtr<pigData>
cgaPyramid_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
