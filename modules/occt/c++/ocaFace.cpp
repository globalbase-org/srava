/*
 * ocaFace — face(shape, i) (#3518 の 2)。**i 番目の面**を取り出して oc-face3d
 *           (= TopoDS_Face) にする。★ 索引で取る側 (2 通りのうちの片方)。
 *
 * ★★ 入力は **立体 (oc-brep3d) でも 2D (oc-face3d) でもよい** (2026-09-13 に 2D を追加)。
 *   2D が要るのは、project やブールの結果が *1 枚とは限らない* から:
 *   凹んだ立体 (U 字) の手前の面へ帯を投影すると、塔 2 本のところで **2 枚**に切り取られる
 *   (実測)。束から 1 枚を取り出せないと、受け取った側が使えない値になる。
 *
 * ---- ★★ なぜ索引で取れるのか (着手前に測った・2026-09-12) ----
 * 巡回は TopExp_Explorer(TopAbs_FACE) の順。OCCT の内部順なので「くじ引きではないか」を
 * 先に測った。結果は **決定的**だった:
 *     ① 同じ形を 2 回作る                    同じ並び
 *     ② 同じブールを 2 回                    同じ並び
 *     ③ BinTools で往復 (= キャッシュ相当)    同じ並び (2 往復も)
 *     ④ 同じ面集合を別の式で作る              同じ並び ((box-A)-B と box-(A+B))
 * ⇒ **同じ面集合なら同じ並び**。キャッシュに焼き付けても版ごとに別物にはならない。
 *
 * ⚠ ただし **面集合そのものが変わる**書き換えはある。box(2,3,4) を「2 つの箱の融合」で
 *   作ると z=2 に継ぎ目が残り 6 面 → 11 面になる。このとき「i 番目」は当然別の面を指す。
 *   ★ これは順序規約をどう決めても直らない (順序の問題ではなく **面の集合の問題**)。
 *   ⇒ 位置で指したいときは **face_at(solid, [x,y,z])** を使う (#3518 の 2・もう片方)。
 *
 * ★ 取り出した面は多くが曲面 or z=0 でない平面なので polygonize は断る (7c80fbd の検査)。
 *   検証は area と閉形式で行う。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"oc/c++/ocShape.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ocaFace_.h"

#include	<TopExp_Explorer.hxx>
#include	<TopoDS.hxx>
#include	<TopoDS_Shape.hxx>
#include	<TopoDS_Face.hxx>   /* ★ TopoDS::Face() の返りを ocFace2D へ渡すので **完全型**が要る */
#include	<Standard_Failure.hxx>
#include	<stdio.h>
#include	<string>

CLASS_TINYSTATE(oc/c++/ocaFace,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ocaFace_(
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


ocaFace_::ocaFace_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
ocaFace_::compute()
{
	ocShape::ensure_init();   /* ★ OCCT の診断出力を stdout から外す */
	int na = ( args != 0 ) ? args->length() : 0;
	/* ★★ 2026-09-13: **2D も受ける**。project / ブールの結果は 1 枚とは限らない
	 *   (凹んだ立体の 2 か所に当たる投影は 2 枚になる — 実測)。束から 1 枚を取り出せないと
	 *   受け取った側が使えない値になるので、3D と同じ語彙で開ける。 */
	sPtr<ocShape>  in = ( na > 0 ) ? sPtr<ocShape>::d_cast((*args)[0])  : sPtr<ocShape>();
	sPtr<ocFace2D> f2 = ( na > 0 && ! in.is_notNull() ) ? sPtr<ocFace2D>::d_cast((*args)[0])
	                                                   : sPtr<ocFace2D>();
	TopoDS_Shape src;
	if      ( in.is_notNull() ) src = in->shape();
	else if ( f2.is_notNull() ) src = f2->shape();
	if ( src.IsNull() ) {
		result = oca_err(thNEW(stdString,(
		    "face: input must be an OCCT solid (oc-brep3d) or 2D region (oc-face3d)")));
		return;
	}
	/* ⚠ 索引は **必須**。既定の 0 を黙って使わない (どの面を指しているかが式に出ない)。
	 *   ★ 実際には planner の arity 検査が先に答える ("expected 2 argument(s), got 1")。
	 *     ここは op を直接叩く経路のための保険。 */
	if ( na < 2 ) {
		result = oca_err(thNEW(stdString,(
		    "face: needs a face index (face(shape, i); 0 <= i < nfaces(shape))")));
		return;
	}
	double fi = (*args)[1]->get_flt();
	if ( fi != (double)(long)fi ) {
		result = oca_err(thNEW(stdString,("face: the face index must be a whole number")));
		return;
	}
	long i = (long)fi;
	try {
		int n = 0;
		for ( TopExp_Explorer e(src, TopAbs_FACE) ; e.More() ; e.Next() ) ++n;
		if ( i < 0 || i >= (long)n ) {
			char b[192];
			::snprintf(b, sizeof b,
			    "face: index %ld is out of range (this %s has %d face%s; "
			    "valid indices are 0..%d)", i, in.is_notNull() ? "solid" : "2D region",
			    n, (n == 1) ? "" : "s", n - 1);
			result = oca_err(thNEW(stdString,(b)));
			return;
		}
		long k = 0;
		for ( TopExp_Explorer e(src, TopAbs_FACE) ; e.More() ; e.Next(), ++k ) {
			if ( k != i ) continue;
			out = thNEW(ocFace2D,());
			out->set_shape(TopoDS::Face(e.Current()));
			return;
		}
		/* nfaces() と巡回が食い違ったとき (起きないはずだが黙らせない)。 */
		result = oca_err(thNEW(stdString,(
		    "face: the face count and the face traversal disagree (internal)")));
	} catch ( const Standard_Failure& e ) {
		std::string m = std::string("face: OCCT failed [") + e.DynamicType()->Name() + "] (" +
		    ( ( e.GetMessageString() && e.GetMessageString()[0] ) ? e.GetMessageString() : "no message" ) + ")";
		result = oca_err(thNEW(stdString,(m.c_str())));
	}
}

sPtr<pigData>
ocaFace_::get_result()
{
	return ( result != thNULL ) ? result : sPtr<pigData>::d_cast(out);
}
