/*
 * ocaSection — section(m, P, N, mode) の計算本体 (occt 版・#3514)。3D → 2D (oc-face3d)。
 *   ★ 約束は cgal / manifold と **完全に同じ** (3 要素配列の規約・docs の section の節):
 *       共面なし … mode 0 が断面・±1 は空集合
 *       共面あり … mode 0 は空集合・±1 が両側の極限
 *     ⇒ ここで判定するのではなく、ocShape::op_section が表のとおりに返す
 *       (どちらの極限かは **立体自身の共面の面の外向き法線**で決まるので、幾何側の仕事)。
 *   ★★ occt の取り柄は **解析曲面のまま切れる**こと。球の断面は真円 (Geom_Circle) で出るので
 *     面積が π(r²-h²) に丸め誤差の範囲で一致する (メッシュ系は内接多角形なので構造的に小さい)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"oc/c++/ocShape.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ocaSection_.h"

CLASS_TINYSTATE(oc/c++/ocaSection,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ocaSection_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<ocFace2D>	out;
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
class ocFace2D;
TS_END_INTERFACE

#endif


ocaSection_::ocaSection_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

/* 配列引数を 3 成分で読む (z を省いたら dflt2)。cgaSection と同じ規約。 */
static void read3(sPtr<pigData> a, double out[3], double dflt2) {
	sPtr<pigDataArray> v = a.is_notNull() ? a->obt_array() : sPtr<pigDataArray>();
	if ( ! v.is_notNull() ) { out[0] = out[1] = 0.0; out[2] = dflt2; return; }
	out[0] = v->get_ix(thNEW(pigDataInteger,((INTEGER64)0)))->get_flt();
	out[1] = v->get_ix(thNEW(pigDataInteger,((INTEGER64)1)))->get_flt();
	out[2] = ( v->length() >= 3 ) ? v->get_ix(thNEW(pigDataInteger,((INTEGER64)2)))->get_flt() : dflt2;
}

void
ocaSection_::compute()
{
	ocShape::ensure_init();
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<ocShape> in = ( na > 0 ) ? sPtr<ocShape>::d_cast((*args)[0]) : sPtr<ocShape>();
	if ( ! in.is_notNull() ) {
		result = oca_err(thNEW(stdString,("section: needs a 3D shape")));
		return;
	}
	double P[3], N[3];
	read3( (na > 1) ? (*args)[1] : sPtr<pigData>(), P, 0.0 );   /* 点 (z 省略 = 0) */
	read3( (na > 2) ? (*args)[2] : sPtr<pigData>(), N, 1.0 );   /* 法線 (省略 = z 軸) */
	int mode = ( na > 3 ) ? (int)(*args)[3]->get_int() : 0;
	if ( mode > 0 ) mode = 1; else if ( mode < 0 ) mode = -1;

	char eb[256]; eb[0] = '\0';
	out = in->op_section(P, N, mode, 0, eb, (int)sizeof eb, &brk_);
	if ( (result = oc_abort_err(brk_, "section")) != thNULL ) { out = thNULL; return; }
	if ( ! out.is_notNull() ) {
		char m[360];
		::snprintf(m, sizeof m,
		    "section: could not cut the shape (needs a 3D solid and a non-degenerate normal)%s%s",
		    eb[0] ? " — OCCT reported: " : "", eb[0] ? eb : "");
		result = oca_err(thNEW(stdString,(m)));
		return;
	}
}

/* この演算の結果。エラー時は compute() が result にエラー値を残して本体未設定で return するので
 * result 優先。 */
sPtr<pigData>
ocaSection_::get_result()
{
	return ( result != thNULL ) ? result : sPtr<pigData>::d_cast(out);
}
