/*
 * cgaScale — scale(mesh, factor) の計算本体(ptsCalcBody 派生)。
 * args=[mesh, X]。X は スカラ(均等スケール)または配列 [sx,sy,sz](軸別)。原点中心の拡大縮小。
 * 対角行列 diag(sx,sy,sz)。係数 0 はメッシュが潰れて退化 → 明示エラー。負値は反射として有効
 * (cga_apply_affine が det<0 を見て面の向きを反転)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"cg/c++/cgMesh.h"
#include	"cg/c++/ptscgWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/cgaScale_.h"

CLASS_TINYSTATE(cg/c++/cgaScale,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	cgaScale_(
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


cgaScale_::cgaScale_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
cgaScale_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<cgMesh>  in  = ( na > 0 ) ? sPtr<cgMesh>::d_cast((*args)[0]) : sPtr<cgMesh>();
	sPtr<pigData> arg = ( na > 1 ) ? (*args)[1] : sPtr<pigData>();

	/* X は配列 [sx,sy,sz](軸別)または スカラ(均等)。 */
	double sx, sy, sz;
	sPtr<pigDataArray> av = arg.is_notNull() ? sPtr<pigDataArray>::d_cast(arg)
	                                         : sPtr<pigDataArray>();
	if ( av.is_notNull() ) {
		if ( av->length() < 3 ) {
			result = thNEW(pigDataError,(thNEW(stdString,(
			    "scale: vector needs 3 components [sx,sy,sz]"))));
			mesh = thNEW(cgMesh3D,());
			return;
		}
		sx = av->get_ix(thNEW(pigDataInteger,((INTEGER64)0)))->get_flt();
		sy = av->get_ix(thNEW(pigDataInteger,((INTEGER64)1)))->get_flt();
		sz = av->get_ix(thNEW(pigDataInteger,((INTEGER64)2)))->get_flt();
	} else {
		sx = sy = sz = arg.is_notNull() ? arg->get_flt() : 1.0;   /* 均等スケール */
	}

	if ( sx == 0.0 || sy == 0.0 || sz == 0.0 ) {
		result = thNEW(pigDataError,(thNEW(stdString,(
		    "scale: degenerate (zero) scale factor"))));
		mesh = thNEW(cgMesh3D,());
		return;
	}

	double e[12] = {
	    sx,  0.0, 0.0, 0.0,
	    0.0, sy,  0.0, 0.0,
	    0.0, 0.0, sz,  0.0
	};
	mesh = ( in.is_notNull() ) ? in->apply_affine(e) : sPtr<cgMesh>();
}

sPtr<ptsWireCacheStreamWriter>
cgaScale_::get_writer()
{
	return thNEW(ptscgWireCacheStreamWriterMesh,(parent, target, mesh));
}
