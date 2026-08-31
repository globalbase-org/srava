/*
 * nfaSphere — sphere(r,seg) の計算本体 (nef 版)。cgaSphere/mfaSphere と同一アルゴリズム (common/geodesic.h) なので頂点・面が一致する。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"nf/c++/nfMesh.h"
#include	"ts2/c++/stdString.h"
#include	"common/geodesic.h"   /* seg_to_n / SEED_OCTAHEDRON (cgal/manifold と共通) */
#include	<vector>
#include	"_ts2/c++/nfaSphere_.h"

CLASS_TINYSTATE(nf/c++/nfaSphere,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	nfaSphere_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<nfMesh>	mesh;
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
class nfMesh;
TS_END_INTERFACE

#endif


nfaSphere_::nfaSphere_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
nfaSphere_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	double r   = ( na > 0 ) ? (*args)[0]->get_flt() : 1.0;
	int    seg = ( na > 1 ) ? (int)(*args)[1]->get_int() : 0;   /* 円周分割数。0=既定 */
	int    n   = srava_geo::seg_to_n(seg);

	nfMesh::Mesh m;
	struct GeoSink {
		nfMesh::Mesh&                           m;
		std::vector<nfMesh::Mesh::Vertex_index> vs;
		GeoSink(nfMesh::Mesh& mm) : m(mm) {}
		int  add_vertex(double x, double y, double z) {
			vs.push_back(m.add_vertex(nfMesh::Point_3(x, y, z)));
			return (int)vs.size() - 1;
		}
		void add_triangle(int a, int b, int c) { m.add_face(vs[a], vs[b], vs[c]); }
	} sink(m);
	srava_geo::make_geodesic(srava_geo::SEED_OCTAHEDRON, n, r, sink);
	mesh = thNEW(nfMesh,());
	mesh->set_from_mesh(m);
}

/* この演算の結果。エラー時は compute() が result にエラー値を残して mesh 未設定で return するので
 * result 優先。保存 (Writer 起動) は agent が出力 pigDataCache の set_body 経由で行う。 */
sPtr<pigData>
nfaSphere_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
