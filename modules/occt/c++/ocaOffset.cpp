/*
 * ocaOffset — offset(s,d[,unused]) の計算本体。★**解析曲面を直接オフセット**する第 3 の原理 (#3437 P5)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"oc/c++/ocShape.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ocaOffset_.h"


CLASS_TINYSTATE(oc/c++/ocaOffset,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ocaOffset_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<ocShape>	out;
	sPtr<ocFace2D>	out2;   /* ★ #3547: 2D を返す経路 (面内オフセット) */
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
class ocFace2D;
TS_END_INTERFACE

#endif


ocaOffset_::ocaOffset_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/


void
ocaOffset_::compute()
{
	ocShape::ensure_init();   /* ★ OCCT の診断出力を stdout から外す (ocShape.h 参照) */
	int na = ( args != 0 ) ? args->length() : 0;
	double d2 = ( na > 1 ) ? (*args)[1]->get_flt() : 0.0;
	/* ★★ #3547 ② (2026-09-18): **2D は「面の中で」動かす** (cg / mf の offset(2D) と同じ約束)。
	 *   ⚠ 平面の面だけ。曲面上の面は距離が測地距離になるので **明示エラー**で
	 *     offset_thicken を案内する (理由は ocShape.h の op_offset の注記)。
	 *   ★ 輪郭は **曲線のまま**太る (円は円のまま) — cg / mf は折れ線に落ちるので、
	 *     ここが occt を通す値になる。 */
	sPtr<ocFace2D> f2 = ( na > 0 ) ? sPtr<ocFace2D>::d_cast((*args)[0]) : sPtr<ocFace2D>();
	if ( f2.is_notNull() ) {
		char why2[512];
		why2[0] = '\0';
		sPtr<ocFace2D> o2 = f2->op_offset(d2, why2, (int)sizeof why2);
		if ( ! o2.is_notNull() ) {
			result = oca_err(thNEW(stdString,( (why2[0] != '\0') ? why2 : "offset: failed" )));
			return;
		}
		out2 = o2;
		return;
	}
	sPtr<ocShape> in = ( na > 0 ) ? sPtr<ocShape>::d_cast((*args)[0]) : sPtr<ocShape>();
	if ( ! in.is_notNull() ) {
		result = oca_err(thNEW(stdString,(
		    "offset: input must be a 3D shape (oc-brep3d) or a 2D region (oc-face3d)")));
		return;
	}
	double d = ( na > 1 ) ? (*args)[1]->get_flt() : 0.0;
	/* ★ 第 3 引数 (近似球の細分化) は **無視する**。nef の 3D offset が球との Minkowski 和で
	 *   実装されているためのパラメータで、OCCT は近似球を使わない (稜に円筒パッチ・頂点に
	 *   球パッチを解析的に生成する = Steiner の公式を構成的にやる)。パーサが offset を常に
	 *   3 引数へ正規化するので受け取りはするが、使わないのが正しい。 */
	out = in->op_offset(d, 0, 0, &brk_);
	if ( ! out.is_notNull() ) {
		if ( (result = oc_abort_err(brk_, "offset")) != thNULL ) return;   /* ★ #3498 */
		result = oca_err(thNEW(stdString,("offset: OCCT MakeOffsetShape failed")));
	}
}

sPtr<pigData>
ocaOffset_::get_result()
{
	if ( result != thNULL ) return result;
	return ( out2 != thNULL ) ? sPtr<pigData>(out2) : sPtr<pigData>(out);
}
