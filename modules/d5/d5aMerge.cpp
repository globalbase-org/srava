/*
 * d5aMerge — d5_merge(a,b) の計算本体 (rev4 Phase D-3・mfaUnion のミラー)。args=[meshA, meshB] は
 * reader が decode した d5Mesh。頂点/面を連結 (ブールなし) して d5Mesh に保持。get_result() で返す。
 * 2 個の cache 入力を読む consumer op = codec/wire-stream 往復の検証点。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"d5/c++/d5Mesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/d5aMerge_.h"

CLASS_TINYSTATE(d5/c++/d5aMerge,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	d5aMerge_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<d5Mesh>	mesh;
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
class d5Mesh;
TS_END_INTERFACE

#endif


d5aMerge_::d5aMerge_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
d5aMerge_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<d5Mesh> a = ( na > 0 ) ? sPtr<d5Mesh>::d_cast((*args)[0]) : sPtr<d5Mesh>();
	sPtr<d5Mesh> b = ( na > 1 ) ? sPtr<d5Mesh>::d_cast((*args)[1]) : sPtr<d5Mesh>();
	if ( ! a.is_notNull() || ! b.is_notNull() ) {
		result = thNEW(pigDataError,(thNEW(stdString,("merge: needs two d5 meshes"))));
		return;
	}
	mesh = d5Mesh::merge(a, b);
	if ( ! mesh.is_notNull() )
		result = thNEW(pigDataError,(thNEW(stdString,("merge: failed"))));
}

sPtr<pigData>
d5aMerge_::get_result()
{
	return ( result != thNULL ) ? result : sPtr<pigData>::d_cast(mesh);
}
