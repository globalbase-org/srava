/*
 * d3aMerge — d3_merge(a,b) の計算本体 (rev4 Phase D-3・mfaUnion のミラー)。args=[meshA, meshB] は
 * reader が decode した d3Mesh。頂点/面を連結 (ブールなし) して d3Mesh に保持。get_result() で返す。
 * 2 個の cache 入力を読む consumer op = codec/wire-stream 往復の検証点。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"d3/c++/d3Mesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/d3aMerge_.h"

CLASS_TINYSTATE(d3/c++/d3aMerge,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	d3aMerge_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<d3Mesh>	mesh;
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
class d3Mesh;
TS_END_INTERFACE

#endif


d3aMerge_::d3aMerge_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
d3aMerge_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<d3Mesh> a = ( na > 0 ) ? sPtr<d3Mesh>::d_cast((*args)[0]) : sPtr<d3Mesh>();
	sPtr<d3Mesh> b = ( na > 1 ) ? sPtr<d3Mesh>::d_cast((*args)[1]) : sPtr<d3Mesh>();
	if ( ! a.is_notNull() || ! b.is_notNull() ) {
		result = thNEW(pigDataError,(thNEW(stdString,("merge: needs two d3 meshes"))));
		return;
	}
	mesh = d3Mesh::merge(a, b);
	if ( ! mesh.is_notNull() )
		result = thNEW(pigDataError,(thNEW(stdString,("merge: failed"))));
}

sPtr<pigData>
d3aMerge_::get_result()
{
	return ( result != thNULL ) ? result : sPtr<pigData>::d_cast(mesh);
}
