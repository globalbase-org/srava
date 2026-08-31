/*
 * chaBox — box(w,h,d) / boxa([w,h,d]) の計算本体 (cherchi 版)。cgal/manifold/nef と同じ軸平行の箱 (0,0,0)-(w,h,d)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"ch/c++/chMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/chaBox_.h"

CLASS_TINYSTATE(ch/c++/chaBox,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	chaBox_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<chMesh>	mesh;
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
class chMesh;
TS_END_INTERFACE

#endif


chaBox_::chaBox_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
chaBox_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	double w = 1.0, h = 1.0, d = 1.0;
	/* boxa([w,h,d]): 寸法を 1 つの array で受け取る形 (★配列判定は d_cast でなく obt_array)。 */
	sPtr<pigDataArray> dims = ( na == 1 ) ? (*args)[0]->obt_array() : sPtr<pigDataArray>();
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
	mesh = thNEW(chMesh,());
	const double P[8][3] = {{0,0,0},{w,0,0},{w,h,0},{0,h,0},{0,0,d},{w,0,d},{w,h,d},{0,h,d}};
	for ( int i = 0 ; i < 8 ; ++i ) mesh->add_vertex(P[i][0], P[i][1], P[i][2]);
	/* CCW 外向き (体積が正になる向き)。 */
	static const int F[12][3] = {{0,2,1},{0,3,2},{4,5,6},{4,6,7},{0,1,5},{0,5,4},
	                             {1,2,6},{1,6,5},{2,3,7},{2,7,6},{3,0,4},{3,4,7}};
	for ( int i = 0 ; i < 12 ; ++i ) mesh->add_triangle(F[i][0], F[i][1], F[i][2]);
}

sPtr<pigData>
chaBox_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
