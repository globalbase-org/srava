/*
 * cgaOffset — offset(mesh, d) の計算本体(ptsCalcBody 派生)。多態 op_offset に委譲(次元非依存):
 *   2D=straight skeleton(面取り・d>0 膨張/d<0 収縮)、3D=Minkowski(球近似・d>0 膨張のみ)。
 * 用途: 角丸/面取り・肉厚(offset(p,0)---offset(p,-t) や 3D は offset(m,t)---m)・クリアランス・工具径補正。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"cg/c++/cgMesh.h"
#include	"cg/c++/ptscgWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/cgaOffset_.h"

CLASS_TINYSTATE(cg/c++/cgaOffset,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	cgaOffset_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<ptsWireCacheStreamWriter>	get_writer();
protected:
	virtual void	compute();
	sPtr<cgMesh>	mesh;
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
class cgMesh;
class ptsWireCacheStreamWriter;
TS_END_INTERFACE

#endif


cgaOffset_::cgaOffset_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
cgaOffset_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<cgMesh> in = ( na > 0 ) ? sPtr<cgMesh>::d_cast((*args)[0]) : sPtr<cgMesh>();
	double d   = ( na > 1 ) ? (*args)[1]->get_flt() : 0.0;
	int subdiv = ( na > 2 ) ? (int)(*args)[2]->get_int() : 1;   /* 3D 球の細分化(2D は無視) */

	if ( ! in.is_notNull() ) {
		result = thNEW(pigDataError,(thNEW(stdString,("offset: needs a mesh"))));
		return;
	}
	mesh = in->op_offset(d, subdiv);   /* 多態: 2D=skeleton / 3D=Minkowski(球。d<0 は補集合トリック) */
	if ( ! mesh.is_notNull() )
		result = thNEW(pigDataError,(thNEW(stdString,(
		    "offset failed: 入力が単純多角形でない(自己交差・重複頂点・零長エッジ)か、"
		    "退化形状の可能性。valid() で確認し repair() で修復するか、輪郭を見直してください "
		    "(曲線を concat した継ぎ目の重複は polygon が間引きます)"))));
}

sPtr<ptsWireCacheStreamWriter>
cgaOffset_::get_writer()
{
	return thNEW(ptscgWireCacheStreamWriterMesh,(parent, target, mesh));
}
