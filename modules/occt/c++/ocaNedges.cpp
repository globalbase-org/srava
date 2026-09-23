/*
 * ocaNedges — nedges(shape) の計算本体 (#3544 段 2)。
 *
 * ★★ **なぜ足したか**: #3544 の受け入れ条件は *稜の本数* で書かれている
 *   (立方体を [1,1,1] から見て 可視 9 / 不可視 3 ・ 球は 稜 0 + 輪郭 1)。
 *   ⇒ これを srava の言葉で検定できないと、@hlr@ は *絵が出た* ことしか確かめられない。
 *   ★ 「球が消えていないこと」は **輪郭を取れているか**の唯一の対照なので、
 *     数えられないまま通すわけにいかない。
 *
 * ★ 数え方は **共有を畳む** (@TopExp::MapShapes@)。立方体の稜は 2 面が共有するので、
 *   @TopExp_Explorer@ で素朴に回すと 24 本になり、閉形式 (12) と合わない。
 *   ⚠ @nfaces@ が Explorer でよいのは面が共有されないから — 同じ書き方にしてはいけない。
 *
 * ★ 2D も受ける。HLR の出力は **面 0 枚・稜 N 本**の値なので、nfaces では何も分からない。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"oc/c++/ocShape.h"

#include	<TopExp.hxx>
#include	<TopTools_IndexedMapOfShape.hxx>
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ocaNedges_.h"


CLASS_TINYSTATE(oc/c++/ocaNedges,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ocaNedges_(
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


ocaNedges_::ocaNedges_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

/* 稜の本数 (共有は 1 本と数える)。 */
static INTEGER64
count_edges(const TopoDS_Shape &s)
{
	if ( s.IsNull() ) return 0;
	TopTools_IndexedMapOfShape m;
	TopExp::MapShapes(s, TopAbs_EDGE, m);
	return (INTEGER64)m.Extent();
}

void
ocaNedges_::compute()
{
	ocShape::ensure_init();   /* ★ OCCT の診断出力を stdout から外す (ocShape.h 参照) */
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<ocShape> in = ( na > 0 ) ? sPtr<ocShape>::d_cast((*args)[0]) : sPtr<ocShape>();
	if ( in.is_notNull() ) {
		result = thNEW(pigDataInteger,(count_edges(in->shape())));
		return;
	}
	sPtr<ocFace2D> f2 = ( na > 0 ) ? sPtr<ocFace2D>::d_cast((*args)[0]) : sPtr<ocFace2D>();
	if ( f2.is_notNull() ) {
		result = thNEW(pigDataInteger,(count_edges(f2->shape())));
		return;
	}
	result = oca_err(thNEW(stdString,("nedges: needs an OCCT shape")));
}

sPtr<pigData>
ocaNedges_::get_result()
{
	return ( result != thNULL ) ? result : out;
}
