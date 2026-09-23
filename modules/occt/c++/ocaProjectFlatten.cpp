/*
 * ocaProjectFlatten — project_flatten(area2d) の occt 版 (#3534)。
 *   world 座標の (x,y) をそのまま取り z を捨てる = **z=0 平面への直投影**。
 *
 * ★★ 「実形のまま寝かせる」ではない理由は cgaProjectFlatten.cpp の冒頭を参照
 *   (3 実装で同じ決定・説明は 1 箇所に置く)。
 *
 * ---- ★ 実装は ocaProject の **特殊化** (ひさ指摘 2026-09-14) ----
 * @project@ (#3518) は既に B 方式 = *方向へ押し出した角柱 ∩ 相手* で実装済み。
 * @project_flatten@ は「投影先 = z=0 の平面 1 枚 / 方向 = ẑ」に固定した特殊形なので、
 * 自前で @gp_GTrsf@ を分解する必要は無い。
 *
 * ★★ 特殊化すると project の難所が **2 つとも消える**:
 *   ① 選別規則 … project は「直線投影は閉曲面を 2 回当たる」ので手前の面だけを残している。
 *                 ⇒ 投影先が **平面 1 枚**なら候補が 1 枚しかないので要らない
 *   ② 遮蔽の未決 … project は「見える」ではない (陰の面も返る)。
 *                 ⇒ 投影先が平面 1 枚なら遮蔽そのものが起きない
 *
 * ---- ★ 3 分岐 ----
 *   平面が XY と平行   → **剛体移動のみ** (gp_Trsf・厳密・安い)
 *                        ★ 特例化が要る理由: 角柱の蓋と z=0 が **同一平面**になり、
 *                          OCCT のブールが最も脆い場所を踏む。しかも実形の経路はここ。
 *   傾いている         → 角柱 + z=0 の面と交差
 *                        ⚠⚠ **曲線の境界は厳密ではない** (2026-09-15 実測)。チケットは
 *                          「円 → 楕円も OCCT が厳密に処理する」と書いていたが、
 *                          @BRepAlgoAPI_Common@ は 平面 ∩ 円柱 を Ellipse と認識せず
 *                          **BSpline で近似**する (tol 5e-6)。
 *                            45 度に傾けた単位円の影   2.221448684115423
 *                            厳密 pi*cos45             2.221441469079183   差 7.2e-06
 *                        ★ それでもこの方式でよい — 取り下げられた案 (剛体で寝かせてから
 *                          面内 2x2 の @gp_GTrsf@) を対照に取ると **500 倍悪い**:
 *                            GTransform の像           2.217874987944138   差 3.6e-03
 *                          (こちらも BSpline になるので、どちらも厳密にはならない)
 *                        ⇒ **直線の境界 (多角形) は厳密**・曲線の境界だけ 1e-5 台で緩む。
 *                          回帰もその 2 つを別の許容で見る。
 *   平面が +Z を含む   → 角柱が退化 ⇒ **明示エラー** (cg/mf と同じ文言)
 *
 * ⚠ occt だけ **型が変わらない** (@(oc-face3d)->oc-face3d@)。#3533 で「occt には z=0 の
 *   簡易表現が存在しない」と決めたため。行き止まりにはならない (@polygonize@ で mf へ渡せる)
 *   が、「降格を型で表す」という #3533 の整理からは外れるので明示しておく。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"oc/c++/ocShape.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ocaProjectFlatten_.h"

#include	<BRepPrimAPI_MakePrism.hxx>
#include	<BRepBuilderAPI_Transform.hxx>
#include	<BRepBuilderAPI_MakeFace.hxx>
#include	<BRepAlgoAPI_Common.hxx>
#include	<Bnd_Box.hxx>
#include	<BRepBndLib.hxx>
#include	<BRepAdaptor_Surface.hxx>
#include	<BRep_Builder.hxx>
#include	<TopoDS_Compound.hxx>
#include	<TopExp_Explorer.hxx>
#include	<TopoDS.hxx>
#include	<TopoDS_Face.hxx>
#include	<gp_Trsf.hxx>
#include	<gp_Vec.hxx>
#include	<gp_Pln.hxx>
#include	<gp_Dir.hxx>
#include	<Standard_Failure.hxx>
#include	<math.h>
#include	<string>

CLASS_TINYSTATE(oc/c++/ocaProjectFlatten,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ocaProjectFlatten_(
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


ocaProjectFlatten_::ocaProjectFlatten_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

/* 面が平面なら法線 n を入れて 1 を返す。平面でなければ 0。 */
static int
ocpf_face_normal(const TopoDS_Face &f, double n[3])
{
	BRepAdaptor_Surface sa(f, Standard_True);
	if ( sa.GetType() != GeomAbs_Plane ) return 0;
	gp_Dir d = sa.Plane().Axis().Direction();
	n[0] = d.X(); n[1] = d.Y(); n[2] = d.Z();
	return 1;
}

void
ocaProjectFlatten_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<ocFace2D> in = ( na > 0 ) ? sPtr<ocFace2D>::d_cast((*args)[0]) : sPtr<ocFace2D>();
	if ( ! in.is_notNull() || in->shape().IsNull() ) {
		result = oca_err(thNEW(stdString,("project_flatten: needs a 2D region")));
		return;
	}

	try {
		/* ---- 面をなめて「全部平面か・法線は揃っているか」を見る ---- */
		int nf = 0, all_planar = 1, aligned = 1;
		double n0[3] = {0,0,0};
		for ( TopExp_Explorer e(in->shape(), TopAbs_FACE) ; e.More() ; e.Next() ) {
			double n[3];
			if ( ! ocpf_face_normal(TopoDS::Face(e.Current()), n) ) { all_planar = 0; ++nf; continue; }
			if ( nf == 0 || (n0[0]==0 && n0[1]==0 && n0[2]==0) ) {
				n0[0]=n[0]; n0[1]=n[1]; n0[2]=n[2];
			} else {
				/* 法線が平行か (符号は問わない)。 */
				double cx = n0[1]*n[2] - n0[2]*n[1];
				double cy = n0[2]*n[0] - n0[0]*n[2];
				double cz = n0[0]*n[1] - n0[1]*n[0];
				if ( ::sqrt(cx*cx + cy*cy + cz*cz) > 1e-9 ) aligned = 0;
			}
			++nf;
		}
		if ( nf == 0 ) {
			/* 面が 1 枚も無い = 空の 2D。そのまま空を返す (エラーではない)。 */
			out = thNEW(ocFace2D,());
			out->set_shape(in->shape());
			return;
		}

		const double EPS = 1e-12;
		if ( all_planar && aligned ) {
			const double nz = ( n0[2] < 0 ? -n0[2] : n0[2] );
			if ( nz <= EPS ) {
				/* ★ 文言は cgal / manifold と **同じ**にしてある — 同じ状況なので。
				 *   ⚠ 片方だけ直さないこと (同じ式を書いた利用者が別の説明を読むことになる)。 */
				result = oca_err(thNEW(stdString,(
				    "project_flatten: the 2D region stands on a plane that contains world +Z, so "
				    "its shadow on z=0 collapses to a line; rotate the region so its plane is not "
				    "parallel to the projection direction")));
				return;
			}
			if ( nz >= 1.0 - 1e-12 ) {
				/* ---- ★ 分岐 1: XY と平行 ⇒ **剛体移動だけ**。
				 *   ⚠ ここを角柱 ∩ 平面でやると、角柱の蓋と z=0 が同一平面になり
				 *     OCCT のブールが最も脆い場所を踏む。しかも実形の経路はここ。 */
				Bnd_Box bb;
				BRepBndLib::Add(in->shape(), bb, Standard_False);
				double x1,y1,z1,x2,y2,z2;
				bb.Get(x1,y1,z1,x2,y2,z2);
				gp_Trsf t;
				t.SetTranslation(gp_Vec(0.0, 0.0, -0.5*(z1+z2)));
				out = thNEW(ocFace2D,());
				out->set_shape(BRepBuilderAPI_Transform(in->shape(), t).Shape());
				return;
			}
		}

		/* ---- ★ 分岐 2: 角柱 ∩ z=0 の面。傾いた平面・曲面の面はこちら ---- */
		Bnd_Box bb;
		BRepBndLib::Add(in->shape(), bb, Standard_False);
		double x1,y1,z1,x2,y2,z2;
		bb.Get(x1,y1,z1,x2,y2,z2);
		double L = 2.0 * ::sqrt((x2-x1)*(x2-x1) + (y2-y1)*(y2-y1) + (z2-z1)*(z2-z1));
		if ( !(L > 0) ) L = 1.0;
		/* ⚠ 投影先の平面の寸法は **入力の bbox から自前で作る** (project は target を
		 *   受け取るので要らなかった)。影は xy の bbox に収まるので、余裕を付けて広げる。 */
		double pad = L + 1.0;
		TopoDS_Face tgt = BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,0), gp_Dir(0,0,1)),
		                                          x1-pad, x2+pad, y1-pad, y2+pad).Face();

		gp_Trsf back;
		back.SetTranslation(gp_Vec(0.0, 0.0, -L));
		TopoDS_Shape moved = BRepBuilderAPI_Transform(in->shape(), back).Shape();
		/* ⚠ Compound をそのまま MakePrism に渡すと Solid にならないことがあるので Face 単位で。 */
		BRep_Builder bld;
		TopoDS_Compound prism;
		bld.MakeCompound(prism);
		int np = 0;
		for ( TopExp_Explorer e(moved, TopAbs_FACE) ; e.More() ; e.Next() ) {
			BRepPrimAPI_MakePrism mk(TopoDS::Face(e.Current()), gp_Vec(0.0, 0.0, 2*L));
			TopoDS_Shape s = mk.Shape();
			if ( ! s.IsNull() ) { bld.Add(prism, s); ++np; }
		}
		if ( np == 0 ) {
			result = oca_err(thNEW(stdString,("project_flatten: the 2D region has no face to project")));
			return;
		}
		BRepAlgoAPI_Common co(tgt, prism);
		if ( ! co.IsDone() ) {
			result = oca_err(thNEW(stdString,(
			    "project_flatten: OCCT could not intersect the z=0 plane with the projected prism")));
			return;
		}
		/* ★ 手前/奥の選別は要らない — 投影先が **平面 1 枚**なので候補が 1 枚しかない。 */
		out = thNEW(ocFace2D,());
		out->set_shape(co.Shape());
	} catch ( const Standard_Failure& e ) {
		std::string m = std::string("project_flatten: OCCT failed [") + e.DynamicType()->Name() + "] (" +
		    ( ( e.GetMessageString() && e.GetMessageString()[0] ) ? e.GetMessageString() : "no message" ) + ")";
		result = oca_err(thNEW(stdString,(m.c_str())));
	}
}

sPtr<pigData>
ocaProjectFlatten_::get_result()
{
	return ( result != thNULL ) ? result : sPtr<pigData>::d_cast(out);
}
