/*
 * ggaTranslate — translate(m,[x,y,z]) の計算本体 (geogram 版・nfaTranslate のミラー)。
 * geogram の Mesh は頂点座標を直接持つので、全頂点を平行移動するだけ。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"gg/c++/ggMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ggaTranslate_.h"

CLASS_TINYSTATE(gg/c++/ggaTranslate,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ggaTranslate_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<ggMesh>	mesh;
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
class ggMesh;
TS_END_INTERFACE

#endif


ggaTranslate_::ggaTranslate_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
ggaTranslate_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<ggMesh> in = ( na > 0 ) ? sPtr<ggMesh>::d_cast((*args)[0]) : sPtr<ggMesh>();
	if ( ! in.is_notNull() ) {
		result = thNEW(pigDataError,(thNEW(stdString,("translate: needs a geogram mesh"))));
		return;
	}
	/* ★配列は d_cast でなく obt_array (map/lambda 由来の遅延ノードも解決される)。 */
	sPtr<pigDataArray> v = ( na > 1 && (*args)[1].is_notNull() ) ? (*args)[1]->obt_array()
	                                                            : sPtr<pigDataArray>();
	if ( ! v.is_notNull() ) {
		result = thNEW(pigDataError,(thNEW(stdString,("translate: needs [x,y,z]"))));
		return;
	}
	double t[3] = { 0.0, 0.0, 0.0 };
	int nd = v->length();
	for ( int k = 0 ; k < 3 && k < nd ; ++k )
		t[k] = v->get_ix(thNEW(pigDataInteger,((INTEGER64)k)))->get_flt();

	mesh = thNEW(ggMesh,());
	mesh->mesh().copy(in->mesh());
	for ( GEO::index_t i = 0 ; i < mesh->mesh().vertices.nb() ; ++i ) {
		GEO::vec3 &p = mesh->mesh().vertices.point(i);
		p = GEO::vec3(p.x + t[0], p.y + t[1], p.z + t[2]);
	}
}

sPtr<pigData>
ggaTranslate_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
