/*
 * cgaColor — color(mesh, c) の計算本体(ptsCalcBody 派生)。args=[mesh(cgMesh, reader), c(inline)]。
 * c = 名前("red"/"green"/…) / "#RRGGBB" / [r,g,b](0-255) を RGB に解釈し、全面に f:color を付ける
 * (3D 専用・combine で各成分の色が残る)。結果を cgMesh に保持して get_result() で返す(#3406 2026-07-30: get_body 統合)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"cg/c++/cgMesh.h"
#include	"cg/c++/ptscgWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	"common/colorspec.h"   /* 色指定の解釈 (mfaColor と共通) */
#include	"_ts2/c++/cgaColor_.h"

#include	<string.h>
#include	<stdlib.h>

CLASS_TINYSTATE(cg/c++/cgaColor,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	cgaColor_(
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


cgaColor_::cgaColor_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

/* 色指定の解釈 (名前 / #RRGGBB / [r,g,b]) は common/colorspec.h に共有 (mfaColor と同一表)。 */

void
cgaColor_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<cgMesh> in   = ( na > 0 ) ? sPtr<cgMesh>::d_cast((*args)[0]) : sPtr<cgMesh>();
	sPtr<pigData> spec = ( na > 1 ) ? (*args)[1] : sPtr<pigData>();
	int r = 150, g = 150, b = 150;
	if ( ! srava_color::parse_spec(spec, r, g, b) ) {
		result = thNEW(pigDataError,(thNEW(stdString,(
		    "color: 2nd arg must be a name (\"red\"...), \"#RRGGBB\", or [r,g,b] (0-255)"))));
		mesh = thNEW(cgMesh3D,());
		return;
	}
	mesh = ( in.is_notNull() ) ? in->op_color(r, g, b) : sPtr<cgMesh>();   /* 2D は op_color が null=エラー */
}

/* この演算の結果 (#3406, 2026-07-30 メモ: get_body/get_result を統一)。エラー時は
 * compute() が result にエラー値を残して mesh を未設定のまま return するので result 優先。
 * 成功時は result=thNULL のままmeshを返す。保存(Writer 起動)は agent が出力 pigDataCache
 * の set_body 経由で行う。 */
sPtr<pigData>
cgaColor_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
