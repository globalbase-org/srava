/*
 * mfaRotate — rotate(mesh, axis, deg) の計算本体(ptsCalcBody 派生)。
 * args=[mesh(mfMesh), axis("x"/"y"/"z" の文字列), deg(度。inline 数値)]。任意角の cos/sin を double で
 * 計算し K::FT に格納(EPECK 座標のまま double 近似 = 「任意角は EPICK 相当に落とす」)。原点まわりの
 * 主軸回転。未対応 axis は result にエラーを立て A_ERROR で伝播。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"mf/c++/mfMesh.h"
#include	"mf/c++/ptsmfWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/mfaRotate_.h"

#include	<math.h>
#include	<string.h>

CLASS_TINYSTATE(mf/c++/mfaRotate,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	mfaRotate_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<mfGeom>	mesh;
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
class mfGeom;
TS_END_INTERFACE

#endif


mfaRotate_::mfaRotate_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
mfaRotate_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<mfGeom>  in      = ( na > 0 ) ? sPtr<mfGeom>::d_cast((*args)[0]) : sPtr<mfGeom>();
	sPtr<pigData> axisArg = ( na > 1 ) ? (*args)[1] : sPtr<pigData>();
	double        deg     = ( na > 2 ) ? (*args)[2]->get_flt() : 0.0;

	double rad = deg * M_PI / 180.0;
	double c = ::cos(rad), s = ::sin(rad);

	/* 回転軸の単位ベクトル (ux,uy,uz) を決める。axis 引数は:
	 *   - 文字列 "x"/"y"/"z": 主軸ショートハンド
	 *   - 配列 [x,y,z]: 任意軸(原点通過)。正規化する。[0,0,0] 等の退化はエラー。 */
	double ux = 0, uy = 0, uz = 1;   /* 既定 z */
	sPtr<pigDataArray> av = axisArg.is_notNull() ? axisArg->obt_array()
	                                             : sPtr<pigDataArray>();
	if ( av.is_notNull() ) {
		if ( av->length() < 3 ) {
			result = thNEW(pigDataError,(thNEW(stdString,(
			    "rotate: axis vector needs 3 components [x,y,z]"))));
			mesh = thNEW(mfMesh,(manifold::Manifold()));
			return;
		}
		double x = av->get_ix(thNEW(pigDataInteger,((INTEGER64)0)))->get_flt();
		double y = av->get_ix(thNEW(pigDataInteger,((INTEGER64)1)))->get_flt();
		double z = av->get_ix(thNEW(pigDataInteger,((INTEGER64)2)))->get_flt();
		double len = ::sqrt(x*x + y*y + z*z);
		if ( len == 0.0 ) {
			result = thNEW(pigDataError,(thNEW(stdString,(
			    "rotate: degenerate axis vector [0,0,0]"))));
			mesh = thNEW(mfMesh,(manifold::Manifold()));
			return;
		}
		ux = x/len; uy = y/len; uz = z/len;
	} else {
		const char* axis = axisArg.is_notNull() ? axisArg->get_str()->get_str() : "z";
		if      ( ::strcmp(axis, "x") == 0 ) { ux = 1; uy = 0; uz = 0; }
		else if ( ::strcmp(axis, "y") == 0 ) { ux = 0; uy = 1; uz = 0; }
		else if ( ::strcmp(axis, "z") == 0 ) { ux = 0; uy = 0; uz = 1; }
		else {
			sPtr<stdString> msg = thNEW(stdString,("rotate: unknown axis '"));
			msg = msg->add(axis)->add("' (expected \"x\"/\"y\"/\"z\" or [x,y,z])");
			result = thNEW(pigDataError,(msg));
			mesh   = thNEW(mfMesh,(manifold::Manifold()));
			return;
		}
	}

	/* Rodrigues の回転行列(原点通過の任意軸 u まわり、右手系・反時計回り)。主軸はこの特例。 */
	double C = 1.0 - c;
	double e[12] = {
	    c + ux*ux*C,     ux*uy*C - uz*s,  ux*uz*C + uy*s,  0.0,
	    uy*ux*C + uz*s,  c + uy*uy*C,     uy*uz*C - ux*s,  0.0,
	    uz*ux*C - uy*s,  uz*uy*C + ux*s,  c + uz*uz*C,     0.0
	};
	mesh = ( in.is_notNull() ) ? in->apply_affine(e) : sPtr<mfGeom>();
}

/* この演算の結果 (#3406, 2026-07-30 メモ: get_body/get_result を統一)。エラー時は
 * compute() が result にエラー値を残して mesh を未設定のまま return するので result 優先。
 * 成功時は result=thNULL のままmeshを返す。保存(Writer 起動)は agent が出力 pigDataCache
 * の set_body 経由で行う。 */
sPtr<pigData>
mfaRotate_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
