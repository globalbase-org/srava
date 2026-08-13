/*
 * cgaMirror — mirror(mesh, axis) の計算本体(ptsCalcBody 派生)。
 * args=[mesh(cgMesh), axis("x"/"y"/"z")]。指定軸の座標を反転(原点を通る軸直交平面での鏡像)。
 * 反射は行列式 -1 で厳密(EPECK)。cga_apply_affine が det<0 を検出し面の向きを反転して法線を保つ。
 * 未対応 axis は result にエラーを立て A_ERROR で伝播。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"cg/c++/cgMesh.h"
#include	"cg/c++/ptscgWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/cgaMirror_.h"

#include	<string.h>

CLASS_TINYSTATE(cg/c++/cgaMirror,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	cgaMirror_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

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
TS_END_INTERFACE

#endif


cgaMirror_::cgaMirror_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
cgaMirror_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<cgMesh>  in      = ( na > 0 ) ? sPtr<cgMesh>::d_cast((*args)[0]) : sPtr<cgMesh>();
	sPtr<pigData> axisArg = ( na > 1 ) ? (*args)[1] : sPtr<pigData>();

	/* 鏡像面の単位法線 (nx,ny,nz)。axis 引数は文字列 "x"/"y"/"z"(主軸直交平面)または
	 * 配列 [nx,ny,nz](原点通過の任意平面の法線。正規化。[0,0,0] 等の退化はエラー)。 */
	double nx = 1, ny = 0, nz = 0;
	sPtr<pigDataArray> av = axisArg.is_notNull() ? axisArg->obt_array()
	                                             : sPtr<pigDataArray>();
	if ( av.is_notNull() ) {
		if ( av->length() < 3 ) {
			result = thNEW(pigDataError,(thNEW(stdString,(
			    "mirror: normal vector needs 3 components [x,y,z]"))));
			mesh = thNEW(cgMesh3D,());
			return;
		}
		double x = av->get_ix(thNEW(pigDataInteger,((INTEGER64)0)))->get_flt();
		double y = av->get_ix(thNEW(pigDataInteger,((INTEGER64)1)))->get_flt();
		double z = av->get_ix(thNEW(pigDataInteger,((INTEGER64)2)))->get_flt();
		double len = ::sqrt(x*x + y*y + z*z);
		if ( len == 0.0 ) {
			result = thNEW(pigDataError,(thNEW(stdString,(
			    "mirror: degenerate normal vector [0,0,0]"))));
			mesh = thNEW(cgMesh3D,());
			return;
		}
		nx = x/len; ny = y/len; nz = z/len;
	} else {
		const char* axis = axisArg.is_notNull() ? axisArg->get_str()->get_str() : "x";
		if      ( ::strcmp(axis, "x") == 0 ) { nx = 1; ny = 0; nz = 0; }
		else if ( ::strcmp(axis, "y") == 0 ) { nx = 0; ny = 1; nz = 0; }
		else if ( ::strcmp(axis, "z") == 0 ) { nx = 0; ny = 0; nz = 1; }
		else {
			sPtr<stdString> msg = thNEW(stdString,("mirror: unknown axis '"));
			msg = msg->add(axis)->add("' (expected \"x\"/\"y\"/\"z\" or [x,y,z])");
			result = thNEW(pigDataError,(msg));
			mesh   = thNEW(cgMesh3D,());
			return;
		}
	}

	/* Householder 鏡像行列 H = I - 2 n nᵀ(原点通過の平面、単位法線 n)。det = -1(反射)。
	 * cga_apply_affine が det<0 を見て面の向きを反転(法線整合)。 */
	double e[12] = {
	    1 - 2*nx*nx,   -2*nx*ny,      -2*nx*nz,     0.0,
	    -2*ny*nx,      1 - 2*ny*ny,   -2*ny*nz,     0.0,
	    -2*nz*nx,      -2*nz*ny,      1 - 2*nz*nz,  0.0
	};
	mesh = ( in.is_notNull() ) ? in->apply_affine(e) : sPtr<cgMesh>();
}

/* この演算の結果 (#3406, 2026-07-30 メモ: get_body/get_result を統一)。エラー時は
 * compute() が result にエラー値を残して mesh を未設定のまま return するので result 優先。
 * 成功時は result=thNULL のままmeshを返す。保存(Writer 起動)は agent が出力 pigDataCache
 * の set_body 経由で行う。 */
sPtr<pigData>
cgaMirror_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
