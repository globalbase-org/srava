/*
 * cgaNfaces — nfaces(mesh) — 頂点数 / 面数を返す (#3443)。
 *   ★ planner が cache のバイト列を "MESH" 前提で読んで表示していたのを op へ移したもの。
 *   幾何の語彙 (頂点・面) を知っているのはモジュールなので、ここが正しい置き場所。
 *   2D は面を持たないので nfaces=0・nverts は点の総数 (外周 + 穴 + ガイド)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"cg/c++/cgMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/cgaNfaces_.h"

CLASS_TINYSTATE(cg/c++/cgaNfaces,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	cgaNfaces_(
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


cgaNfaces_::cgaNfaces_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
cgaNfaces_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<cgMesh> in = ( na > 0 ) ? sPtr<cgMesh>::d_cast((*args)[0]) : sPtr<cgMesh>();
	if ( ! in.is_notNull() ) {
		result = thNEW(pigDataError,(thNEW(stdString,("nfaces: needs a mesh"))));
		return;
	}
	result = thNEW(pigDataInteger,((INTEGER64)in->op_nfaces()));
}
