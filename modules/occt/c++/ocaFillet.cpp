/*
 * ocaFillet — fillet(shape, r) の計算本体 (#3437)。**全ての稜**を半径 r の転がり球で丸める。
 * ★ これは **B-rep でしか厳密に書けない加工**である。転がり球の接触軌跡は解析曲面
 *   (平面どうしの稜なら円筒、頂点なら球) なので、三角形分割の上では定義そのものが近似になる。
 * ★ 検算できる: 直方体 (a,b,c) の全稜を半径 r で丸めた形は、**内側の直方体 (a-2r,b-2r,c-2r) を
 *   半径 r のボールで Minkowski 和したもの**とちょうど一致するので、Steiner の公式で真値が出る。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"oc/c++/ocShape.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ocaFillet_.h"


CLASS_TINYSTATE(oc/c++/ocaFillet,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ocaFillet_(
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


ocaFillet_::ocaFillet_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/


void
ocaFillet_::compute()
{
	ocShape::ensure_init();   /* ★ OCCT の診断出力を stdout から外す (ocShape.h 参照) */
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<ocShape> in = ( na > 0 ) ? sPtr<ocShape>::d_cast((*args)[0]) : sPtr<ocShape>();
	if ( ! in.is_notNull() ) {
		result = thNEW(pigDataError,(thNEW(stdString,("fillet: needs an OCCT shape"))));
		return;
	}
	double r = ( na > 1 ) ? (*args)[1]->get_flt() : 0.0;
	if ( r < 0.0 ) {
		result = thNEW(pigDataError,(thNEW(stdString,("fillet: radius must be >= 0"))));
		return;
	}
		/* ★ 理由の受け皿は **この compute のローカル** (モジュール大域の static を置かない・
	 * in-proc では複数 op が同居しうるため。ひさ指示 2026-08-26)。 */
	char why[512];
	why[0] = '\0';
out = in->op_fillet(r, why, (int)sizeof why);
	if ( ! out.is_notNull() ) {
		/* ★ OCCT が例外で失敗した場合はその理由を載せる (ocShape の oc_guard が捕まえている)。
		 * 例外でなく IsDone()==false の場合は理由が無いので従来の文言。 */
		char b[600];
		const char *msg = "fillet: OCCT MakeFillet failed (radius too large, or no edge to fillet)";
		if ( why[0] != '\0' ) {
			::snprintf(b, sizeof b, "fillet: OCCT failed: %s", why);
			msg = b;
		}
		result = thNEW(pigDataError,(thNEW(stdString,(msg))));
	}
}

sPtr<pigData>
ocaFillet_::get_result()
{
	return ( result != thNULL ) ? result : out;
}
