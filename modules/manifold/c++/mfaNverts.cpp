/*
 * mfaNverts — nverts(mesh) — 頂点数 / 面数を返す (#3443)。
 *   ★ planner が cache のバイト列を読んで表示していたのを op へ移したもの。
 *   2D は面を持たないので nfaces=0。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"mf/c++/mfMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/mfaNverts_.h"

CLASS_TINYSTATE(mf/c++/mfaNverts,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	mfaNverts_(
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


mfaNverts_::mfaNverts_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
mfaNverts_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<pigData> a = ( na > 0 ) ? (*args)[0] : sPtr<pigData>();
	sPtr<mfMesh>  m3 = sPtr<mfMesh>::d_cast(a);
	sPtr<mfCross> c2 = sPtr<mfCross>::d_cast(a);
	if ( m3.is_notNull() )      result = thNEW(pigDataInteger,((INTEGER64)m3->op_nverts()));
	else if ( c2.is_notNull() ) result = thNEW(pigDataInteger,((INTEGER64)c2->op_nverts()));
	else result = thNEW(pigDataError,(thNEW(stdString,("nverts: needs a mesh"))));
}
