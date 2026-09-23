/*
 * ocaFaceAt — face_at(shape, [x,y,z]) (#3518 の 2)。指定した点に **いちばん近い面**を
 *             取り出して oc-face3d にする。★ 幾何で指す側 (2 通りのうちのもう片方)。
 *
 * ★★ 入力は **立体 (oc-brep3d) でも 2D (oc-face3d) でもよい** (2026-09-13 に 2D を追加)。
 *   投影が 2 枚になったとき「左の塔の方」を位置で指せる = 索引より書き換えに強い。
 *
 * ---- ★★ なぜ索引と 2 通りに分けるのか (測定に基づく・2026-09-12) ----
 * face(solid, i) の巡回順は決定的で、**同じ面集合なら同じ並び**になる (ocaFace.cpp の測定)。
 * ところが立体の作り方を変えると **面集合そのものが変わる**ことがある:
 *     box(2,3,4)                        6 面
 *     box(2,3,2) ||| 上に積んだ box      11 面 (z=2 の継ぎ目が残る)
 * このとき「5 番目の面」は当然別の面を指す。⇒ *順序規約をどう決めても直らない*。
 *
 * ⇒ モデルを書き換えても同じ面を指し続けたいなら、**位置で指す**しかない。
 *   face_at は「その点にいちばん近い面」を返すので、面の割れ方が変わっても
 *   「天面」「あの穴の内壁」を指し続けられる。
 *
 * ★ 距離は BRepExtrema_DistShapeShape (点 → 面の最短距離)。面の内部・境界を問わない。
 * ⚠ 同距離の面が複数あるとき (角の真上など) は **明示エラー**にする。黙って片方を選ぶと
 *   「同じ式に 2 通りの値」になり、#3516 / #3518-1 で潰してきたのと同じ穴になる。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"oc/c++/ocShape.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ocaFaceAt_.h"

#include	<TopExp_Explorer.hxx>
#include	<TopoDS.hxx>
#include	<TopoDS_Shape.hxx>
#include	<TopoDS_Face.hxx>   /* ★ TopoDS::Face() の返りを ocFace2D へ渡すので **完全型**が要る */
#include	<TopoDS_Vertex.hxx>
#include	<BRepBuilderAPI_MakeVertex.hxx>
#include	<BRepExtrema_DistShapeShape.hxx>
#include	<gp_Pnt.hxx>
#include	<Standard_Failure.hxx>
#include	<math.h>
#include	<stdio.h>
#include	<string>

CLASS_TINYSTATE(oc/c++/ocaFaceAt,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ocaFaceAt_(
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


ocaFaceAt_::ocaFaceAt_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
ocaFaceAt_::compute()
{
	ocShape::ensure_init();   /* ★ OCCT の診断出力を stdout から外す */
	int na = ( args != 0 ) ? args->length() : 0;
	/* ★★ 2026-09-13: **2D も受ける** (ocaFace.cpp と同じ理由)。★ 面の束から位置で 1 枚選べる
	 *   ⇒ 投影が 2 枚になっても「左の塔の方」を指せる。索引より書き換えに強い。 */
	sPtr<ocShape>  in = ( na > 0 ) ? sPtr<ocShape>::d_cast((*args)[0])  : sPtr<ocShape>();
	sPtr<ocFace2D> f2 = ( na > 0 && ! in.is_notNull() ) ? sPtr<ocFace2D>::d_cast((*args)[0])
	                                                   : sPtr<ocFace2D>();
	TopoDS_Shape src;
	if      ( in.is_notNull() ) src = in->shape();
	else if ( f2.is_notNull() ) src = f2->shape();
	if ( src.IsNull() ) {
		result = oca_err(thNEW(stdString,(
		    "face_at: input must be an OCCT solid (oc-brep3d) or 2D region (oc-face3d)")));
		return;
	}
	sPtr<pigDataArray> v = ( na > 1 ) ? (*args)[1]->obt_array() : sPtr<pigDataArray>();
	if ( ! v.is_notNull() || v->length() < 3 ) {
		result = oca_err(thNEW(stdString,(
		    "face_at: needs a point [x,y,z] to pick the face nearest to")));
		return;
	}
	double p[3];
	for ( int k = 0 ; k < 3 ; ++k )
		p[k] = v->get_ix(thNEW(pigDataInteger,((INTEGER64)k)))->get_flt();

	try {
		TopoDS_Vertex pv = BRepBuilderAPI_MakeVertex(gp_Pnt(p[0], p[1], p[2]));
		/* 最短距離の面を探す。★ 2 位との差も覚えておく (同距離を断るため)。 */
		TopoDS_Shape best;
		double d0 = 0.0, d1 = 0.0;   /* 1 位 / 2 位の距離 */
		int n = 0;
		for ( TopExp_Explorer e(src, TopAbs_FACE) ; e.More() ; e.Next() ) {
			BRepExtrema_DistShapeShape dss(pv, e.Current());
			if ( ! dss.IsDone() ) continue;
			double d = dss.Value();
			if      ( n == 0 )  { d0 = d; best = e.Current(); }
			else if ( d < d0 )  { d1 = d0; d0 = d; best = e.Current(); }   /* 1 位を押し下げる */
			else if ( n == 1 || d < d1 )  d1 = d;                           /* 2 位を更新 */
			++n;
		}
		if ( n == 0 || best.IsNull() ) {
			result = oca_err(thNEW(stdString,("face_at: the input has no face")));
			return;
		}
		/* ⚠ 同距離 (角・稜の真上) は黙って片方を選ばない。★ 許容は距離の大きさに対する相対。 */
		if ( n > 1 ) {
			double s = ( d0 > 1.0 ) ? d0 : 1.0;
			if ( ::fabs(d1 - d0) <= 1e-9 * s ) {
				char b[224];
				::snprintf(b, sizeof b,
				    "face_at: the point [%g,%g,%g] is the same distance (%g) from more than "
				    "one face, so it does not name a single face; move it off the edge or "
				    "corner, or use face(solid, i)", p[0], p[1], p[2], d0);
				result = oca_err(thNEW(stdString,(b)));
				return;
			}
		}
		out = thNEW(ocFace2D,());
		out->set_shape(TopoDS::Face(best));
	} catch ( const Standard_Failure& e ) {
		std::string m = std::string("face_at: OCCT failed [") + e.DynamicType()->Name() + "] (" +
		    ( ( e.GetMessageString() && e.GetMessageString()[0] ) ? e.GetMessageString() : "no message" ) + ")";
		result = oca_err(thNEW(stdString,(m.c_str())));
	}
}

sPtr<pigData>
ocaFaceAt_::get_result()
{
	return ( result != thNULL ) ? result : sPtr<pigData>::d_cast(out);
}
