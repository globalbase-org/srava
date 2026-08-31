/*
 * ggaCast — cast("gg-mesh3d", m) の計算本体。実体の変換は codec の昇格読みが担うので、ここは受け取った mesh をそのまま返す (nfaCast と同じ)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"gg/c++/ggMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ggaCast_.h"

CLASS_TINYSTATE(gg/c++/ggaCast,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ggaCast_(
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


ggaCast_::ggaCast_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
ggaCast_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<ggMesh> in = ( na > 1 ) ? sPtr<ggMesh>::d_cast((*args)[1]) : sPtr<ggMesh>();
	if ( ! in.is_notNull() ) {
		result = thNEW(pigDataError,(thNEW(stdString,("cast: needs a mesh"))));
		return;
	}
	mesh = in;
}

sPtr<pigData>
ggaCast_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
