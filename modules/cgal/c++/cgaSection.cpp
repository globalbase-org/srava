/*
 * cgaSection — section(mesh, [px,py,pz], [nx,ny,nz]) の計算本体(ptsCalcBody 派生)。
 * 3D メッシュを「点 P を通り法線 N の平面」で切った 2D 断面(cgMesh2D)を返す。
 * args=[mesh(cgMesh, reader), point(inline 配列), normal(inline 配列)]。多態 op_section に委譲
 * (3D=Polygon_mesh_slicer + even-odd 充填 / 2D=エラー)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"cg/c++/cgMesh.h"
#include	"cg/c++/ptscgWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/cgaSection_.h"

CLASS_TINYSTATE(cg/c++/cgaSection,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	cgaSection_(
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


cgaSection_::cgaSection_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

static void read3(sPtr<pigData> a, double out[3], double dflt2) {
	sPtr<pigDataArray> v = a.is_notNull() ? a->obt_array() : sPtr<pigDataArray>();
	if ( ! v.is_notNull() ) { out[0] = out[1] = 0.0; out[2] = dflt2; return; }
	out[0] = v->get_ix(thNEW(pigDataInteger,((INTEGER64)0)))->get_flt();
	out[1] = v->get_ix(thNEW(pigDataInteger,((INTEGER64)1)))->get_flt();
	out[2] = ( v->length() >= 3 ) ? v->get_ix(thNEW(pigDataInteger,((INTEGER64)2)))->get_flt() : dflt2;
}

void
cgaSection_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<cgMesh> in = ( na > 0 ) ? sPtr<cgMesh>::d_cast((*args)[0]) : sPtr<cgMesh>();
	if ( ! in.is_notNull() ) {
		result = thNEW(pigDataError,(thNEW(stdString,("section: missing mesh"))));
		return;
	}
	double P[3], N[3];
	read3( (na > 1) ? (*args)[1] : sPtr<pigData>(), P, 0.0 );   /* 点(z 省略=0) */
	read3( (na > 2) ? (*args)[2] : sPtr<pigData>(), N, 1.0 );   /* 法線(省略=z 軸) */

	/* mode: 0=平面ちょうど / -1=平面の直下(h-ε) / +1=平面の直上(h+ε)。既定 0。 */
	int mode = ( na > 3 ) ? (int)(*args)[3]->get_int() : 0;
	if ( mode > 0 ) mode = 1; else if ( mode < 0 ) mode = -1;

	int coplanar = 0;
	mesh = in->op_section(P, N, mode, &coplanar);   /* 多態: 3D=厳密カット / 2D=null(エラー) */
	if ( ! mesh.is_notNull() ) {
		result = thNEW(pigDataError,(thNEW(stdString,(
		    "section: needs a 3D mesh and a non-degenerate normal"))));
		return;
	}
	/* ★ 3 要素配列仕様(ひさ設計 2026-08-15)の要素判定。パーサが section(m,P,N) を
	 *   [section(..,0), section(..,-1), section(..,+1)] へ展開するので、ここでは各モードが
	 *   「自分の出番か」を判定して、出番でなければ **空集合**(空 cross2d)を返す:
	 *     共面あり … 平面ちょうど(0)は退化して定義できない → 空。両側の極限(-1/+1)が答え。
	 *     共面なし … 平面ちょうど(0)が答え。極限(-1/+1)は空。
	 *   空要素は `{}`(fold の中立元)ではなく **empty2d() と同じ空メッシュ**で返す。 */
	if ( ( coplanar && mode == 0 ) || ( ! coplanar && mode != 0 ) )
		mesh = thNEW(cgMesh2D,());   /* 空集合 */
}

/* この演算の結果 (#3406, 2026-07-30 メモ: get_body/get_result を統一)。エラー時は
 * compute() が result にエラー値を残して mesh を未設定のまま return するので result 優先。
 * 成功時は result=thNULL のままmeshを返す。保存(Writer 起動)は agent が出力 pigDataCache
 * の set_body 経由で行う。 */
sPtr<pigData>
cgaSection_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
