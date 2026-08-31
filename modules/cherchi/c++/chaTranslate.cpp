/*
 * chaTranslate — translate(m,[x,y,z]) の計算本体 (cherchi 版)。
 * chMesh は素の座標配列を持つので、全頂点を平行移動するだけ。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"ch/c++/chMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/chaTranslate_.h"

CLASS_TINYSTATE(ch/c++/chaTranslate,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	chaTranslate_(
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


chaTranslate_::chaTranslate_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
chaTranslate_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<chMesh> in = ( na > 0 ) ? sPtr<chMesh>::d_cast((*args)[0]) : sPtr<chMesh>();
	if ( ! in.is_notNull() ) {
		result = thNEW(pigDataError,(thNEW(stdString,("translate: needs a cherchi mesh"))));
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

	mesh = thNEW(chMesh,());
	mesh->coords() = in->coords();
	mesh->tris()   = in->tris();
	std::vector<double> &c = mesh->coords();
	for ( size_t i = 0 ; i + 2 < c.size() ; i += 3 ) {
		c[i] += t[0]; c[i+1] += t[1]; c[i+2] += t[2];
	}
}

sPtr<pigData>
chaTranslate_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
