/*
 * chaCentroid — centroid(m) — 体積重心 [x,y,z] を返す (発散定理)。
 *
 * ★ 値を返すだけの op (→value)。2D 型を要さないので、2D 型を持たないこのカーネルでも置ける。
 *   無いと「値の素性を訊く」ためだけに別カーネルへ cast させることになり、cast が通らない
 *   値 (非有界・非多様体) では確認手段そのものが消える (#3478) — それが #3487 の動機。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"ch/c++/chMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/chaCentroid_.h"

CLASS_TINYSTATE(ch/c++/chaCentroid,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	chaCentroid_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

protected:
	virtual void	compute();
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
TS_END_INTERFACE

#endif


chaCentroid_::chaCentroid_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
chaCentroid_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<chMesh> in = ( na > 0 ) ? sPtr<chMesh>::d_cast((*args)[0]) : sPtr<chMesh>();
	if ( ! in.is_notNull() ) {
		result = cha_err(thNEW(stdString,("centroid: needs a cherchi mesh")));
		return;
	}
	double c[3] = { 0, 0, 0 };
	in->op_centroid(c);
	sPtr<pigDataArray> arr = thNEW(pigDataArray,());
	for ( int i = 0 ; i < 3 ; ++i )
		arr->push(thNEW(pigDataFloat,(c[i])));
	result = arr;
}
