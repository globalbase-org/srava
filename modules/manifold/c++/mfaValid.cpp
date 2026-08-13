/*
 * mfaValid — valid(mesh) の計算本体(mf 版・cgaValid のミラー)。値返し(1=正常 / 0=破綻)。
 * Manifold の妥当性検査は「サイレント破綻の検出点」= Status()==NoError かつ非空(mfMesh::op_valid)。
 * 2D(mfCross)は CrossSection が構成上正規化されるので 1(空なら 0)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"mf/c++/mfMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/mfaValid_.h"

CLASS_TINYSTATE(mf/c++/mfaValid,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	mfaValid_(
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


mfaValid_::mfaValid_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
mfaValid_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<pigData> a = ( na > 0 ) ? (*args)[0] : sPtr<pigData>();
	sPtr<mfMesh>  m3 = sPtr<mfMesh>::d_cast(a);
	sPtr<mfCross> c2 = sPtr<mfCross>::d_cast(a);
	int v;
	if ( m3.is_notNull() )      v = m3->op_valid();
	else if ( c2.is_notNull() ) v = ( c2->op_area() > 0.0 ) ? 1 : 0;   /* 2D: 非空なら valid */
	else {
		result = thNEW(pigDataError,(thNEW(stdString,("valid: needs a mesh"))));
		return;
	}
	result = thNEW(pigDataInteger,((INTEGER64)v));
}
