/*
 * ocaCone — cone(r, h, seg) — 円錐 (原点中心・軸 +Z・底面 z=-h/2・頂点 z=+h/2) の計算本体 (occt 版・#3474)。
 * ★ occt は **解析曲面**で作る (近似しないので seg は無視)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"oc/c++/ocShape.h"
#include	"common/solids.h"   /* SOLID_PI (座標規約を共通ヘッダと共有) */

#include	<BRepPrimAPI_MakeCone.hxx>
#include	<TopoDS_Shape.hxx>
#include	<gp_Pnt.hxx>
#include	<gp_Dir.hxx>
#include	<gp_Ax2.hxx>
#include	<Standard_Failure.hxx>
#include	<cmath>
#include	<string>
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ocaCone_.h"

CLASS_TINYSTATE(oc/c++/ocaCone,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ocaCone_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<ocShape>	out;
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
class ocShape;
TS_END_INTERFACE

#endif


ocaCone_::ocaCone_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
ocaCone_::compute()
{
	ocShape::ensure_init();
	int na = ( args != 0 ) ? args->length() : 0;
	double r   = ( na > 0 ) ? (*args)[0]->get_flt() : 1.0;
	double h   = ( na > 1 ) ? (*args)[1]->get_flt() : 1.0;
	int    seg = ( na > 2 ) ? (int)(*args)[2]->get_int() : 0;   /* 円周分割数。0=既定 32 */
	if ( !(r > 0) ) { result = oca_err(thNEW(stdString,("cone: radius must be > 0"))); return; }
	if ( !(h > 0) ) { result = oca_err(thNEW(stdString,("cone: height must be > 0"))); return; }
	try {
		/* ★ 円錐は **解析曲面** (円錐面 1 + 平面 1)。cylinder と同じく原点中心・軸 +Z で、
		 *   BRepPrimAPI_MakeCone の基準点が底面中心なので h/2 下げる。seg は近似しないので無視する
		 *   (occt の sphere が seg を無視するのと同じ理由)。 */
		gp_Ax2 ax(gp_Pnt(0, 0, -h/2), gp_Dir(0, 0, 1));
		BRepPrimAPI_MakeCone mk(ax, r, 0.0, h);
		TopoDS_Shape sh = mk.Shape();
		if ( sh.IsNull() ) { result = oca_err(thNEW(stdString,("cone: OCCT produced a null shape"))); return; }
		out = thNEW(ocShape,());
		out->set_shape(sh);
	} catch ( const Standard_Failure& e ) {
		std::string m = std::string("cone: OCCT failed [") + e.DynamicType()->Name() + "] (" +
		    ( ( e.GetMessageString() && e.GetMessageString()[0] ) ? e.GetMessageString() : "no message" ) + ")";
		result = oca_err(thNEW(stdString,(m.c_str())));
		return;
	}
}

/* この演算の結果。エラー時は compute() が result にエラー値を残して本体未設定で return するので
 * result 優先。保存 (Writer 起動) は agent が出力 pigDataCache の set_body 経由で行う。 */
sPtr<pigData>
ocaCone_::get_result()
{
	return ( result != thNULL ) ? result : sPtr<pigData>::d_cast(out);
}
