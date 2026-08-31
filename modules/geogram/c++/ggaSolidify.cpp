/*
 * ggaSolidify — solidify(m) の計算本体 (geogram 版・#3445)。自己交差した境界を arrangement で解いて外側シェルだけ残す。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"gg/c++/ggMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ggaSolidify_.h"

CLASS_TINYSTATE(gg/c++/ggaSolidify,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ggaSolidify_(
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


ggaSolidify_::ggaSolidify_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
ggaSolidify_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<ggMesh> in = ( na > 0 ) ? sPtr<ggMesh>::d_cast((*args)[0]) : sPtr<ggMesh>();
	if ( ! in.is_notNull() ) {
		result = thNEW(pigDataError,(thNEW(stdString,("solidify: needs a geogram mesh"))));
		return;
	}
	char why[512];
	why[0] = '\0';
	mesh = in->op_solidify(why, (int)sizeof why);
	if ( ! mesh.is_notNull() )
	{
		char b[600];
		const char *m = "solidify: failed";
		if ( why[0] != '\0' ) { ::snprintf(b, sizeof b, "solidify: %s", why); m = b; }
		result = thNEW(pigDataError,(thNEW(stdString,(m))));
	}
}

sPtr<pigData>
ggaSolidify_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
