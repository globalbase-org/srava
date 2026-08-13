/*
 * d4aMerge — d4_merge(a,b) の計算本体 (rev4 Phase D-3・mfaUnion のミラー)。args=[meshA, meshB] は
 * reader が decode した d4Mesh。頂点/面を連結 (ブールなし) して d4Mesh に保持。get_result() で返す。
 * 2 個の cache 入力を読む consumer op = codec/wire-stream 往復の検証点。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"d4/c++/d4Mesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/d4aMerge_.h"

CLASS_TINYSTATE(d4/c++/d4aMerge,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	d4aMerge_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<d4Mesh>	mesh;
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
class d4Mesh;
TS_END_INTERFACE

#endif


d4aMerge_::d4aMerge_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
d4aMerge_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<d4Mesh> a = ( na > 0 ) ? sPtr<d4Mesh>::d_cast((*args)[0]) : sPtr<d4Mesh>();
	sPtr<d4Mesh> b = ( na > 1 ) ? sPtr<d4Mesh>::d_cast((*args)[1]) : sPtr<d4Mesh>();
	if ( ! a.is_notNull() || ! b.is_notNull() ) {
		result = thNEW(pigDataError,(thNEW(stdString,("merge: needs two d4 meshes"))));
		return;
	}
	mesh = d4Mesh::merge(a, b);
	if ( ! mesh.is_notNull() )
		result = thNEW(pigDataError,(thNEW(stdString,("merge: failed"))));
}

sPtr<pigData>
d4aMerge_::get_result()
{
	return ( result != thNULL ) ? result : sPtr<pigData>::d_cast(mesh);
}
