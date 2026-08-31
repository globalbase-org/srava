/*
 * chaCast — cast("ch-mesh3d", m) の計算本体。実体の変換は codec の昇格読みが担うので、ここは受け取った mesh をそのまま返す (nfaCast と同じ)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"ch/c++/chMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/chaCast_.h"

CLASS_TINYSTATE(ch/c++/chaCast,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	chaCast_(
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


chaCast_::chaCast_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
chaCast_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<chMesh> in = ( na > 1 ) ? sPtr<chMesh>::d_cast((*args)[1]) : sPtr<chMesh>();
	if ( ! in.is_notNull() ) {
		result = thNEW(pigDataError,(thNEW(stdString,("cast: needs a mesh"))));
		return;
	}
	mesh = in;
}

sPtr<pigData>
chaCast_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
