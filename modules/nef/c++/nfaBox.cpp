/*
 * nfaBox — box(w,h,d) の計算本体 (nef 版・cgaBox のミラー)。Surface_mesh を作って Nef 化する。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"nf/c++/nfMesh.h"
#include	"ts2/c++/stdString.h"
#include	<CGAL/boost/graph/generators.h>
#include	<CGAL/Polygon_mesh_processing/triangulate_faces.h>
#include	"_ts2/c++/nfaBox_.h"

CLASS_TINYSTATE(nf/c++/nfaBox,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	nfaBox_(
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


nfaBox_::nfaBox_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
nfaBox_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	double w = 1.0, h = 1.0, d = 1.0;
	/* boxa([w,h,d]): 寸法を 1 つの array で受け取る形 (★配列判定は d_cast でなく obt_array)。 */
	sPtr<pigDataArray> dims = ( na == 1 ) ? (*args)[0]->obt_array()
	                                      : sPtr<pigDataArray>();
	if ( dims.is_notNull() ) {
		int nd = dims->length();
		if ( nd > 0 ) w = dims->get_ix(thNEW(pigDataInteger,((INTEGER64)0)))->get_flt();
		if ( nd > 1 ) h = dims->get_ix(thNEW(pigDataInteger,((INTEGER64)1)))->get_flt();
		if ( nd > 2 ) d = dims->get_ix(thNEW(pigDataInteger,((INTEGER64)2)))->get_flt();
	} else {
		if ( na > 0 ) w = (*args)[0]->get_flt();
		if ( na > 1 ) h = (*args)[1]->get_flt();
		if ( na > 2 ) d = (*args)[2]->get_flt();
	}

	nfMesh::Mesh m;
	typedef nfMesh::Point_3 P;
	CGAL::make_hexahedron(
	    P(0,0,0), P(w,0,0), P(w,h,0), P(0,h,0),
	    P(0,0,d), P(w,0,d), P(w,h,d), P(0,h,d), m);
	CGAL::Polygon_mesh_processing::triangulate_faces(m);
	mesh = thNEW(nfMesh,());
	mesh->set_from_mesh(m);   /* 境界 → Nef (SNC 構築) */
}

/* この演算の結果。エラー時は compute() が result にエラー値を残して mesh 未設定で return するので
 * result 優先。保存 (Writer 起動) は agent が出力 pigDataCache の set_body 経由で行う。 */
sPtr<pigData>
nfaBox_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
