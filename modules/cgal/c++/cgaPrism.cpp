/*
 * cgaPrism — 正 n 角柱生成の計算本体(ptsCalcBody 派生)。args=[n, height, radius](INLINE)。
 * CGAL::make_regular_prism で Surface_mesh(EPECK)を作り三角形化、cgMesh に保持。cgaBox と同型。
 * n 角形頂点は cos/sin(double)由来だが EPECK に厳密格納される(多面体としては厳密)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"cg/c++/cgMesh.h"
#include	"cg/c++/ptscgWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/cgaPrism_.h"

#include	<CGAL/boost/graph/generators.h>
#include	<CGAL/Polygon_mesh_processing/triangulate_faces.h>

CLASS_TINYSTATE(cg/c++/cgaPrism,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	cgaPrism_(
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


cgaPrism_::cgaPrism_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
cgaPrism_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	int    n = ( na > 0 ) ? (int)(*args)[0]->get_int() : 3;
	double h = ( na > 1 ) ? (*args)[1]->get_flt() : 1.0;
	double r = ( na > 2 ) ? (*args)[2]->get_flt() : 1.0;
	if ( n < 3 ) n = 3;

	mesh = thNEW(cgMesh3D,());
	cgMesh::Mesh& m = mesh->mesh();
	CGAL::make_regular_prism((unsigned)n, m, cgMesh::Point_3(0,0,0), h, r, true);
	/* CGAL は高さを Y 軸に作る。extrude/box(Z=高さ)に合わせ、頂点を X 軸 +90°回転
	 * (x,y,z)→(x,-z,y) で **高さを Z 軸**へ。90°は厳密・det=+1 で面の向き不変。
	 * これで prism(n,h,r) ≡ extrude(ngon(n,r),h)(断面 n 角形は XY 平面・z=0..h)。 */
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
cgaPrism_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
