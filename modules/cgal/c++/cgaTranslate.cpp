/*
 * cgaTranslate — translate(mesh, x, y, z) の計算本体(ptsCalcBody 派生)。
 * args=[mesh(cgMesh, reader), x, y, z(inline 数値)]。平行移動は EPECK で厳密(x/y/z の double を
 * K::FT に格納)。結果を cgMesh に保持して get_result() で返す(#3406 2026-07-30: get_body 統合)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"cg/c++/cgMesh.h"
#include	"cg/c++/ptscgWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/cgaTranslate_.h"

CLASS_TINYSTATE(cg/c++/cgaTranslate,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	cgaTranslate_(
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


cgaTranslate_::cgaTranslate_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
cgaTranslate_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<cgMesh> in = ( na > 0 ) ? sPtr<cgMesh>::d_cast((*args)[0]) : sPtr<cgMesh>();
	/* 平行移動量はベクトル [x,y](2D 向け・z=0)または [x,y,z]。文法が translate(m,x,y,z) も m>>>v も
	 * この形に統一して渡す。[0,0(,0)] は恒等(エラーにしない)。 */
	sPtr<pigDataArray> v = ( na > 1 ) ? (*args)[1]->obt_array()
	                                  : sPtr<pigDataArray>();
	if ( ! v.is_notNull() || v->length() < 2 ) {
		result = thNEW(pigDataError,(thNEW(stdString,(
		    "translate: needs a vector [x,y] or [x,y,z]"))));
		mesh = thNEW(cgMesh3D,());
		return;
	}
	double x = v->get_ix(thNEW(pigDataInteger,((INTEGER64)0)))->get_flt();
	double y = v->get_ix(thNEW(pigDataInteger,((INTEGER64)1)))->get_flt();
	double z = ( v->length() >= 3 ) ? v->get_ix(thNEW(pigDataInteger,((INTEGER64)2)))->get_flt() : 0.0;

	/* 平行移動 = 恒等 + 平行移動列。3D 多態 apply_affine(double[12])に委譲。 */
	double e[12] = {
	    1.0, 0.0, 0.0, x,
	    0.0, 1.0, 0.0, y,
	    0.0, 0.0, 1.0, z
	};
	mesh = ( in.is_notNull() ) ? in->apply_affine(e) : sPtr<cgMesh>();
}

/* この演算の結果 (#3406, 2026-07-30 メモ: get_body/get_result を統一)。エラー時は
 * compute() が result にエラー値を残して mesh を未設定のまま return するので result 優先。
 * 成功時は result=thNULL のままmeshを返す。保存(Writer 起動)は agent が出力 pigDataCache
 * の set_body 経由で行う。 */
sPtr<pigData>
cgaTranslate_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
