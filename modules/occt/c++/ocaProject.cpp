/*
 * ocaProject — project(drawing, target, [dx,dy,dz]) (#3518 の 3)。
 *              平面図形を方向 d へ投影して、**曲面の上に切り取られた 2D** を作る。
 *
 * ---- ★★ 実装方式は測って決めた (2026-09-12) ----
 * チケットは @BRepProj_Projection@ (ワイヤ→曲面上のワイヤ) + @BRepFeat_SplitShape@ の 2 段を
 * 想定していたが、プローブで両方式を閉形式と突き合わせた結果:
 *
 *   円柱 (r=1,h=4) の側面へ、幅 1 x 高さ 2 の矩形を +X から投影する
 *   閉形式 (弧長 x 高さ) = 1 * 2*asin(0.5) * 2 = 2.0943951024
 *
 *     A  BRepProj_Projection + BRepFeat_SplitShape   側面が 4 枚に割れ、うち 1 枚が 2.0943951024
 *     B  方向へ押し出した角柱と **面 ∩ 立体** (#3518 の 4)  合計 4.1887902048
 *
 * ★★ どちらも *2 倍* の面積を含んでいた — **直線投影は閉曲面を 2 回当たる** (手前と奥)。
 *   ⇒ 方式の違いではなく **選別規則が要る**、が本質だった。
 * ⇒ B を採る (既に検証済みのブールを再利用でき、OCCT の依存も増えない)。選別は:
 *
 *   ★ 面の **外向き法線が投影方向と逆を向くもの** = 投影元に顔を向けている面だけを残す
 *     実測: 奥の 2 枚 dot=+0.966 / 手前 1 枚 dot=-1.000 ⇒ 手前だけで 2.0943951024 (閉形式と一致)
 *
 * ⚠⚠ **「見える」ではない — 遮蔽は見ていない** (2026-09-13)。縦置きのトーラスを下から投影した実測:
 *     当たるのは 6 面 / 返るのは 3 面
 *       z=-2.51..-2.35  2 枚  下の管の外側 (下向き)      ← 本当に見える
 *       z=+1.49..+1.66  1 枚  **上の管の内側** (下向き)  ← 下の管の陰。見えないが返る
 *   ⇒ 「いちばん手前の 1 枚」を返すには遮蔽の判定が要り、それには **投影先の面だけでなく
 *     立体が要る**。この op は面しか受け取らないので **原理的に決められない**。
 *   ⇒ 立体を受ける別の口にするかどうかは未決 (ひさ判断)。いまの約束は「顔を向けている面」。
 *
 * ⚠ 「奥も欲しい」は今のところ書けない。必要になったら第 4 引数で足す (黙って両方返さない —
 *   同じ式に 2 通りの意味を持たせない)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"oc/c++/ocShape.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ocaProject_.h"

#include	<BRepPrimAPI_MakePrism.hxx>
#include	<BRepBuilderAPI_Transform.hxx>
#include	<BRepAlgoAPI_Common.hxx>
#include	<BRepGProp.hxx>
#include	<GProp_GProps.hxx>
#include	<Bnd_Box.hxx>
#include	<BRepBndLib.hxx>
#include	<BRepTools.hxx>
#include	<BRepAdaptor_Surface.hxx>
#include	<BRepLProp_SLProps.hxx>
#include	<BRep_Builder.hxx>
#include	<TopoDS_Compound.hxx>
#include	<TopExp_Explorer.hxx>
#include	<TopoDS.hxx>
#include	<TopoDS_Face.hxx>
#include	<gp_Trsf.hxx>
#include	<gp_Vec.hxx>
#include	<gp_Dir.hxx>
#include	<Standard_Failure.hxx>
#include	<math.h>
#include	<string>

CLASS_TINYSTATE(oc/c++/ocaProject,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ocaProject_(
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


ocaProject_::ocaProject_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

/* 面が投影方向に対して手前を向いているか。
 *   返り 1 = すべて手前 / 2 = すべて奥 / 3 = **両方を含む (輪郭線をまたぐ)** / 0 = 法線が定まらない
 *
 * ★★ **1 点で代表してはいけない** (2026-09-13 に実測で判明)。
 *   トーラスの下面に帯を投影すると、切り取られた面は *管の断面を一周するひと続きの輪* になる
 *   (v が 0..2π)。この 1 枚が手前と奥の両方を向いていて、中央 1 点はちょうど輪郭線の上
 *   (dot = +0.0000) に来る。⇒ 中央だけを見ると「奥」と判定して **全部捨て、空が返っていた**
 *   (「交わらなかった」と区別がつかない)。
 *   ⇒ UV の格子で数点サンプルし、符号が割れたら **割れたと言う** ([[judgement-material-drifts-from-reality]])。
 * ⚠ 格子点はトリムの外に落ちることがあるが、判定したいのは「この面が向きを変えるか」なので
 *   同じ曲面パッチの上であれば足りる。 */
static int
oc_face_side(const TopoDS_Face &f, const gp_Dir &dir)
{
	Standard_Real u1, u2, v1, v2;
	BRepTools::UVBounds(f, u1, u2, v1, v2);
	BRepAdaptor_Surface sa(f);
	const int N = 4;   /* 5x5 点 */
	/* ⚠⚠ **@near@ / @far@ という名前を使わない** (2026-09-15・simu01 が box で踏んだ)。
	 *   windows.h (windef.h) が Win16 互換で **空に展開されるマクロ**として持っているので、
	 *   MinGW ではこの宣言が @int  = 0,  = 0, und = 0;@ に潰れて構文エラーになる。
	 *   ★ Linux では通るので **こちらでは永久に気づけない**種類の欠陥。
	 *   ⚠ 同じ家系: 以前 @pigPluginSDK.cpp@ の @const int IN/OUT@ が同じ理由で衝突している。 */
	int nNear = 0, nFar = 0, und = 0;
	for ( int a = 0 ; a <= N ; ++a ) {
		for ( int b = 0 ; b <= N ; ++b ) {
			BRepLProp_SLProps p(sa, u1 + (u2-u1)*a/(double)N, v1 + (v2-v1)*b/(double)N, 1, 1e-9);
			if ( ! p.IsNormalDefined() ) { ++und; continue; }
			gp_Dir n = p.Normal();
			if ( f.Orientation() == TopAbs_REVERSED ) n.Reverse();
			/* ⚠ ちょうど 0 (輪郭線の上) はどちらにも数えない — そこが割れ目なので、
			 *   両側に点があるかどうかで判定する。 */
			double d = n.Dot(dir);
			if      ( d < -1e-9 ) ++nNear;
			else if ( d >  1e-9 ) ++nFar;
		}
	}
	if ( nNear > 0 && nFar > 0 ) return 3;
	if ( nNear > 0 )             return 1;
	if ( nFar > 0 )              return 2;
	return 0;
}

void
ocaProject_::compute()
{
	ocShape::ensure_init();   /* ★ OCCT の診断出力を stdout から外す */
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<ocFace2D> draw = ( na > 0 ) ? sPtr<ocFace2D>::d_cast((*args)[0]) : sPtr<ocFace2D>();
	sPtr<ocFace2D> tgt  = ( na > 1 ) ? sPtr<ocFace2D>::d_cast((*args)[1]) : sPtr<ocFace2D>();
	if ( ! draw.is_notNull() || draw->shape().IsNull() ||
	     ! tgt.is_notNull()  || tgt->shape().IsNull() ) {
		result = oca_err(thNEW(stdString,(
		    "project: needs two 2D regions (the drawing and the surface to project onto)")));
		return;
	}
	sPtr<pigDataArray> v = ( na > 2 ) ? (*args)[2]->obt_array() : sPtr<pigDataArray>();
	if ( ! v.is_notNull() || v->length() < 3 ) {
		result = oca_err(thNEW(stdString,(
		    "project: needs a direction [dx,dy,dz] to project along")));
		return;
	}
	double d[3];
	for ( int k = 0 ; k < 3 ; ++k )
		d[k] = v->get_ix(thNEW(pigDataInteger,((INTEGER64)k)))->get_flt();
	double dl = ::sqrt(d[0]*d[0] + d[1]*d[1] + d[2]*d[2]);
	if ( dl == 0.0 ) {
		result = oca_err(thNEW(stdString,("project: the direction must not be [0,0,0]")));
		return;
	}
	d[0] /= dl; d[1] /= dl; d[2] /= dl;

	try {
		/* ★ 押し出す長さは **相手を必ず貫く**だけ要る。両者の合併の bbox の対角の 2 倍を採り、
		 *   図形を -L だけ戻してから 2L 押し出す (図形が相手の中に居ても跨げる)。 */
		Bnd_Box bb;
		BRepBndLib::Add(draw->shape(), bb);
		BRepBndLib::Add(tgt->shape(),  bb);
		double x1, y1, z1, x2, y2, z2;
		bb.Get(x1, y1, z1, x2, y2, z2);
		double L = 2.0 * ::sqrt((x2-x1)*(x2-x1) + (y2-y1)*(y2-y1) + (z2-z1)*(z2-z1));
		if ( !(L > 0) ) L = 1.0;

		gp_Trsf back;
		back.SetTranslation(gp_Vec(-L*d[0], -L*d[1], -L*d[2]));
		TopoDS_Shape moved = BRepBuilderAPI_Transform(draw->shape(), back).Shape();
		/* ⚠ Compound をそのまま MakePrism に渡すと Solid にならないことがあるので Face 単位で。 */
		BRep_Builder bld;
		TopoDS_Compound prism;
		bld.MakeCompound(prism);
		int np = 0;
		for ( TopExp_Explorer e(moved, TopAbs_FACE) ; e.More() ; e.Next() ) {
			BRepPrimAPI_MakePrism mk(TopoDS::Face(e.Current()),
			                         gp_Vec(2*L*d[0], 2*L*d[1], 2*L*d[2]));
			TopoDS_Shape s = mk.Shape();
			if ( ! s.IsNull() ) { bld.Add(prism, s); ++np; }
		}
		if ( np == 0 ) {
			result = oca_err(thNEW(stdString,(
			    "project: the drawing has no face to project")));
			return;
		}
		TopoDS_Shape cut;
		{
			BRepAlgoAPI_Common co(tgt->shape(), prism);
			if ( ! co.IsDone() ) {
				result = oca_err(thNEW(stdString,(
				    "project: OCCT could not intersect the surface with the projected prism")));
				return;
			}
			cut = co.Shape();
		}
		/* ★★ 手前側だけ残す — 直線投影は閉曲面を 2 回当たるため (上のコメント)。
		 *   ⚠ 法線が定まらない面は **落とさずに残す** (判定できないものを黙って捨てない)。
		 *   ⚠⚠ 1 枚の面が手前と奥の **両方**を向いていることがある (輪郭線をまたぐ輪)。
		 *     そのときは「手前だけ」を面の粒度では取り出せない ⇒ **明示エラー**。
		 *     ここで黙って落とすと空が返り、「交わらなかった」と区別がつかない。 */
		BRep_Builder bb2;
		TopoDS_Compound keep;
		bb2.MakeCompound(keep);
		gp_Dir dir(d[0], d[1], d[2]);
		int nk = 0;
		for ( TopExp_Explorer e(cut, TopAbs_FACE) ; e.More() ; e.Next() ) {
			const TopoDS_Face &f = TopoDS::Face(e.Current());
			int side = oc_face_side(f, dir);
			if ( side == 2 ) continue;          /* 奥側 */
			if ( side == 3 ) {                  /* 手前と奥がひと続き */
				result = oca_err(thNEW(stdString,(
				    "project: the projection wraps around the surface, so one face looks both "
				    "toward and away from the direction and the near side cannot be taken on "
				    "its own; cut the surface first, or use \"cross2d &&& brep3d\" to get the "
				    "whole region the projection passes through")));
				return;
			}
			bb2.Add(keep, f);                   /* 手前側・判定できない面 */
			++nk;
		}
		/* ★ 1 枚も残らないのは **空の 2D** であってエラーではない (相手と交わらない投影)。 */
		out = thNEW(ocFace2D,());
		out->set_shape(keep);
	} catch ( const Standard_Failure& e ) {
		std::string m = std::string("project: OCCT failed [") + e.DynamicType()->Name() + "] (" +
		    ( ( e.GetMessageString() && e.GetMessageString()[0] ) ? e.GetMessageString() : "no message" ) + ")";
		result = oca_err(thNEW(stdString,(m.c_str())));
	}
}

sPtr<pigData>
ocaProject_::get_result()
{
	return ( result != thNULL ) ? result : sPtr<pigData>::d_cast(out);
}
