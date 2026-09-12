/*
 * ggaTransform — transform(m, matrix) — 行優先 12 (3x4) / 16 (4x4) の一般アフィン変換。上の 3 本の一般形。
 *
 * ★ 引数の解釈と行列の組み立ては **common/affine.h** (カーネル非依存)。この .cpp が持つのは
 *   「引数を渡して double[12] を貰い、ggMesh::apply_affine へ流す」だけ。7 カーネルで
 *   受理する書き方と拒否の理由を揃えるための構造 (#3486)。
 * ★ 4 op はどれも **3D→3D で 2D 型を要さない**ので、2D 型を持たないこのカーネルでも置ける
 *   (extrude / section のような 2D 依存の op とは事情が違う → #3474 の判断)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"gg/c++/ggMesh.h"
#include	"common/affine.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ggaTransform_.h"

CLASS_TINYSTATE(gg/c++/ggaTransform,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ggaTransform_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<ggMesh>	mesh;
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
class ggMesh;
TS_END_INTERFACE

#endif


ggaTransform_::ggaTransform_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
ggaTransform_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<ggMesh> in = ( na > 0 ) ? sPtr<ggMesh>::d_cast((*args)[0]) : sPtr<ggMesh>();
	if ( ! in.is_notNull() ) {
		result = gga_err(thNEW(stdString,("transform: needs a geogram mesh")));
		return;
	}
	sPtr<pigData> arg = ( na > 1 ) ? (*args)[1] : sPtr<pigData>();

	/* 行優先 3x4 のアフィン行列。理由の受け皿 buf は **呼び手が持つ** (モジュール側に
	 * static を置かない — in-proc では 1 プロセスに複数 op が同居しうる)。 */
	double e[12];
	const char *why = 0;
	char buf[256];
	if ( ! srava_affine::matrix_transform(arg, e, &why, buf, (int)sizeof buf) ) {
		result = gga_err(thNEW(stdString,(why)));
		return;
	}
	mesh = in->apply_affine(e);
}

/* この演算の結果。エラー時は compute() が result にエラー値を残して mesh 未設定で return するので
 * result 優先。保存 (Writer 起動) は agent が出力 pigDataCache の set_body 経由で行う。 */
sPtr<pigData>
ggaTransform_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
