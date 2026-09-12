/*
 * ocaMirror — mirror(shape, axis | [nx,ny,nz]) の計算本体 (#3461)。
 * ★ 原点を通る平面での鏡映。反射行列 I - 2nnᵀ は直交 (行列式 -1) なので
 *   op_affine の中で gp_Trsf が使われ、**解析曲面のまま**運ばれる。
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
#include	"_ts2/c++/ocaMirror_.h"


CLASS_TINYSTATE(oc/c++/ocaMirror,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ocaMirror_(
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


ocaMirror_::ocaMirror_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/


void
ocaMirror_::compute()
{
	ocShape::ensure_init();
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<ocShape> in = ( na > 0 ) ? sPtr<ocShape>::d_cast((*args)[0]) : sPtr<ocShape>();
	if ( ! in.is_notNull() ) {
		result = oca_err(thNEW(stdString,("mirror: needs an OCCT shape")));
		return;
	}
	sPtr<pigData> arg = ( na > 1 ) ? (*args)[1] : sPtr<pigData>();

	/* ★ 引数の解釈と行列の組み立ては **common/affine.h** (カーネル非依存・7 モジュール共通)。
	 *   受け付ける書き方だけでなく **拒否の理由** もそこに集約してある (#3486)。
	 *   理由の受け皿 buf は呼び手が持つ (モジュール側に static を置かない)。 */
	double e[12];
	const char *why = 0;
	char buf[256];
	if ( ! srava_affine::matrix_mirror(arg, e, &why, buf, (int)sizeof buf) ) {
		result = oca_err(thNEW(stdString,(why)));
		return;
	}
	char eb[224]; eb[0] = 0;
	out = in->op_affine(e, eb, (int)sizeof eb);
	if ( ! out.is_notNull() ) {
		char b[256];
		::snprintf(b, sizeof b, "mirror: OCCT transform failed%s%s",
		           eb[0] ? " — " : "", eb);
		result = oca_err(thNEW(stdString,(b)));
	}
}

sPtr<pigData>
ocaMirror_::get_result()
{
	return ( result != thNULL ) ? result : out;
}
