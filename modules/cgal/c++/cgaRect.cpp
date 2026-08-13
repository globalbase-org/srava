/*
 * cgaRect — rect(w, h) の計算本体(ptsCalcBody 派生)= 2D プリミティブ。
 * 原点隅の軸並行長方形(CCW)を cgMesh2D に 1 つ作る。extrude/booleans の断面に使う。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"cg/c++/cgMesh.h"
#include	"cg/c++/ptscgWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/cgaRect_.h"
#include	<stdio.h>

CLASS_TINYSTATE(cg/c++/cgaRect,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	cgaRect_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<cgMesh2D>	mesh;
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
class cgMesh2D;
TS_END_INTERFACE

#endif


cgaRect_::cgaRect_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
cgaRect_::compute()
{
	typedef cgMesh::K K;
	int na = ( args != 0 ) ? args->length() : 0;
	double w = ( na > 0 ) ? (*args)[0]->get_flt() : 1.0;
	double h = ( na > 1 ) ? (*args)[1]->get_flt() : 1.0;

	/* 非正の幅/高さは退化(or 時計回り)ポリゴン → Polygon_set_2 の前提(単純・CCW・正面積)を
	 * 破り、下流の 2D ブール演算でエージェントがクラッシュする。ここで明示エラーにして弾く
	 * (符号ミスの早期検出。エラーは呼び出し位置の file,line 付きで戻る)。 */
	if ( w <= 0.0 || h <= 0.0 ) {
		char buf[96];
		::snprintf(buf, sizeof buf, "rect: width and height must be positive (got %g, %g)", w, h);
		result = thNEW(pigDataError,(thNEW(stdString,(buf))));
		return;
	}

	mesh = thNEW(cgMesh2D,());
	cgMesh2D::Polygon_2 r;        /* CCW: (0,0)→(w,0)→(w,h)→(0,h) */
	r.push_back(K::Point_2(K::FT(0.0), K::FT(0.0)));
	r.push_back(K::Point_2(K::FT(w),   K::FT(0.0)));
	r.push_back(K::Point_2(K::FT(w),   K::FT(h)));
	r.push_back(K::Point_2(K::FT(0.0), K::FT(h)));
	mesh->regions().push_back(cgMesh2D::Pwh_2(r));
}

/* この演算の結果 (#3406, 2026-07-30 メモ: get_body/get_result を統一)。エラー時は
 * compute() が result にエラー値を残して mesh を未設定のまま return するので result 優先。
 * 成功時は result=thNULL のままmeshを返す。保存(Writer 起動)は agent が出力 pigDataCache
 * の set_body 経由で行う。 */
sPtr<pigData>
cgaRect_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
