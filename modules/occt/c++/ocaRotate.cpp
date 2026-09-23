/*
 * ocaRotate — rotate(shape, axis, deg) の計算本体 (#3461)。
 * ★ 原点を通る軸まわりの回転。回転行列は直交なので op_affine の中で gp_Trsf が使われ、
 *   **解析曲面のまま**運ばれる。axis は "x"/"y"/"z" または [ax,ay,az]。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"oc/c++/ocShape.h"
#include	"common/affine.h"   /* アフィン変換の共通規約 (#3486) */
#include	"ts2/c++/stdString.h"
#include	<cstdio>
#include	<cstring>
#include	<cmath>
#include	"_ts2/c++/ocaRotate_.h"


CLASS_TINYSTATE(oc/c++/ocaRotate,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ocaRotate_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<ocGeom>	out;
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
class ocGeom;
TS_END_INTERFACE

#endif


ocaRotate_::ocaRotate_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/


void
ocaRotate_::compute()
{
	ocShape::ensure_init();
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<ocGeom> in = ( na > 0 ) ? sPtr<ocGeom>::d_cast((*args)[0]) : sPtr<ocGeom>();
	if ( ! in.is_notNull() ) {
		result = oca_err(thNEW(stdString,("rotate: needs an OCCT shape (oc-brep3d) or 2D region (oc-face3d)")));
		return;
	}
	sPtr<pigData> arg = ( na > 1 ) ? (*args)[1] : sPtr<pigData>();
	double        deg = ( na > 2 ) ? (*args)[2]->get_flt() : 0.0;

	/* ★ 引数の解釈と行列の組み立ては **common/affine.h** (カーネル非依存・7 モジュール共通)。
	 *   受け付ける書き方だけでなく **拒否の理由** もそこに集約してある (#3486)。
	 *   理由の受け皿 buf は呼び手が持つ (モジュール側に static を置かない)。 */
	double e[12];
	const char *why = 0;
	char buf[256];
	if ( ! srava_affine::matrix_rotate(arg, deg, e, &why, buf, (int)sizeof buf) ) {
		result = oca_err(thNEW(stdString,(why)));
		return;
	}
	char eb[224]; eb[0] = 0;
	out = in->op_affine(e, eb, (int)sizeof eb);
	if ( ! out.is_notNull() ) {
		char b[256];
		::snprintf(b, sizeof b, "rotate: OCCT transform failed%s%s",
		           eb[0] ? " — " : "", eb);
		result = oca_err(thNEW(stdString,(b)));
	}
}

sPtr<pigData>
ocaRotate_::get_result()
{
	return ( result != thNULL ) ? result : out;
}
