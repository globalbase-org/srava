/*
 * ocaHlr — hlr(solid, dir[, up][, mode]) — **陰線処理** (#3544 段 2)。
 *   立体から遮蔽を解いた 2D 図面 (oc-cross2d) を起こす。
 *
 * ★★ @project@ / @project_flatten@ とは **別物**。3 つ並べて覚える (#3544 本文 §2):
 *
 *     project(drawing,target,dir)   平面図形を **曲面へ** 投影して切る (形が変わる)
 *                                   ⚠ 面しか受けないので **遮蔽を解けない**
 *     project_flatten(area2d)       **既にある 2D** を z=0 へ寝かせる (影)。立体は受けない
 *     hlr(solid,dir[,up])           **立体から** 遮蔽を解いた 2D 図面を起こす  ← これ
 *
 * ★ 厳密 (@HLRBRep_Algo@) で確定。段 0 の実測で @HLRBRep_PolyAlgo@ は
 *   *速くもないのに折れ線になる* (球の輪郭が 1 本の真円 対 82 本の折れ線・費用は同程度)。
 *   ⇒ 引数でも別 op でも出さない。大きな形で逆転するなら、そのとき足せばよい。
 *
 * ⚠⚠ **出てくる稜は 3D 曲線を持たない** (段 0 の実測)。@BRep_Tool::Curve@ は null を返し、
 *   @BRepAdaptor_Curve@ でしか素性が取れない。⇒ 3D 曲線が在ると決め打ちしている下流へ
 *   渡すと落ちうる。段 3 (dxf/svg の書き手) は必ず BRepAdaptor_Curve で分類すること。
 *
 * ⚠ HLR 自身は **中断できない** (HLRBRep_Algo に進捗の口が無い)。⇒ 前後で brk_ を見るだけ。
 *   長い形で Ctrl+C を押しても、その 1 回の Update()/Hide() は走り切る。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"oc/c++/ocShape.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ocaHlr_.h"

#include	<HLRBRep_Algo.hxx>
#include	<HLRBRep_HLRToShape.hxx>
#include	<HLRAlgo_Projector.hxx>
#include	<BRep_Builder.hxx>
#include	<TopoDS_Compound.hxx>
#include	<gp_Ax2.hxx>
#include	<gp_Dir.hxx>
#include	<gp_Pnt.hxx>
#include	<gp_Vec.hxx>
#include	<Standard_Failure.hxx>
#include	<math.h>
#include	<string>
#include	<cstring>

CLASS_TINYSTATE(oc/c++/ocaHlr,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ocaHlr_(
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


ocaHlr_::ocaHlr_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

/* 3 成分の配列を読む。配列でなければ 0 を返す (呼び手が明示エラーにする)。 */
static int
read3(sPtr<pigData> a, double v[3])
{
	sPtr<pigDataArray> arr = a.is_notNull() ? a->obt_array() : sPtr<pigDataArray>();
	if ( ! arr.is_notNull() || arr->length() < 3 ) return 0;
	for ( int i = 0 ; i < 3 ; ++i )
		v[i] = arr->get_ix(thNEW(pigDataInteger,((INTEGER64)i)))->get_flt();
	return 1;
}

static double
norm3(const double v[3]) { return ::sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]); }

void
ocaHlr_::compute()
{
	ocShape::ensure_init();
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<ocShape> in = ( na > 0 ) ? sPtr<ocShape>::d_cast((*args)[0]) : sPtr<ocShape>();
	if ( ! in.is_notNull() || in->shape().IsNull() ) {
		result = oca_err(thNEW(stdString,("hlr: needs a 3D shape (oc-brep3d)")));
		return;
	}

	/* ---- 視線 ------------------------------------------------------------------
	 * ★ @dir@ は **見る向き** (視点から形へ向かうベクトル)。@project@ の投影方向と同じ流儀。 */
	double dir[3];
	if ( ! read3( (na > 1) ? (*args)[1] : sPtr<pigData>(), dir ) ) {
		result = oca_err(thNEW(stdString,("hlr: the view direction must be a 3-element array, e.g. hlr(s,[0,-1,0])")));
		return;
	}
	if ( norm3(dir) < 1e-12 ) {
		result = oca_err(thNEW(stdString,("hlr: the view direction must not be [0,0,0]")));
		return;
	}

	/* ---- 省略できる 2 つ (up と mode) ------------------------------------------
	 * ⚠ 位置で決めず **literal の種類**で分ける — 3 要素配列なら up ・ 文字列なら mode。
	 *   理由: 真横から見る図面では既定の up で足りるので @hlr(s,dir,"hidden")@ と書けないと
	 *   不便だが、up を「省いたことにして」黙って別の向きを入れるのは *図面の回転が黙って
	 *   変わる* ということ (段 0 の④)。⇒ 取り違えようのない区別だけを使う。 */
	double up[3] = { 0.0, 0.0, 1.0 };   /* 既定 = world の +Z */
	int    haveUp = 0, haveMode = 0;
	std::string mode = "visible";
	for ( int i = 2 ; i < na ; ++i ) {
		sPtr<pigData> a = (*args)[i];
		if ( ! a.is_notNull() ) continue;
		double t[3];
		if ( read3(a, t) ) {
			/* ⚠ 2 つ目の配列は **黙って上書きしない**。書いた人はどちらかを別の意味の
			 *   つもりで書いている ⇒ 図面が黙って回るより断るほうがよい。 */
			if ( haveUp ) {
				result = oca_err(thNEW(stdString,(
				    "hlr: the up direction was given twice; hlr takes at most one "
				    "3-element array after the view direction")));
				return;
			}
			up[0] = t[0]; up[1] = t[1]; up[2] = t[2]; haveUp = 1;
			continue;
		}
		sPtr<stdString> s = a->get_str();
		if ( s.is_notNull() && s->get_str() != 0 && s->get_str()[0] != '\0' ) {
			if ( haveMode ) {
				result = oca_err(thNEW(stdString,(
				    "hlr: the mode was given twice; hlr takes at most one of "
				    "\"visible\" / \"hidden\"")));
				return;
			}
			mode = s->get_str(); haveMode = 1;
			continue;
		}
	}
	const int wantHidden = ( mode == "hidden" );
	if ( ! wantHidden && mode != "visible" ) {
		char m[224];
		::snprintf(m, sizeof m,
		    "hlr: mode must be \"visible\" or \"hidden\", not \"%s\"", mode.c_str());
		result = oca_err(thNEW(stdString,(m)));
		return;
	}
	if ( norm3(up) < 1e-12 ) {
		result = oca_err(thNEW(stdString,("hlr: the up direction must not be [0,0,0]")));
		return;
	}

	/* ★★ 段 0 の④: @gp_Ax2@ は dir と up が平行だと **例外**で落ちる
	 *   (gp_Dir::CrossCross() - result vector has zero norm)。⇒ 先に断る。
	 *   ⚠ 黙って別の up に差し替えてはいけない — 図面の回転が黙って変わる。 */
	{
		const double c[3] = { up[1]*dir[2] - up[2]*dir[1],
		                      up[2]*dir[0] - up[0]*dir[2],
		                      up[0]*dir[1] - up[1]*dir[0] };
		if ( norm3(c) <= 1e-12 * norm3(up) * norm3(dir) ) {
			result = oca_err(thNEW(stdString,(
			    "hlr: the up direction is parallel to the view direction, so the drawing's "
			    "rotation is undefined; pass an up direction that is not parallel "
			    "(e.g. hlr(s,[0,0,-1],[0,1,0]))")));
			return;
		}
	}

	if ( (result = oc_abort_err(brk_, "hlr")) != thNULL ) return;

	try {
		/* ★ 投影の座標系: Z = **視点の側** = -dir (見る向きの逆)。
		 *   OCCT の HLR は CS の +Z 側に視点が在るものとして解くので、
		 *   「dir の向きに見る」= 視点は形の -dir 側 ⇒ CS の Z は -dir。
		 *   ★ X は up から作る: X = up × Z とすると gp_Ax2 の Y = Z × X が up の
		 *     視線に直交する成分になる (= 図面の上が up)。 */
		const gp_Dir zd(-dir[0], -dir[1], -dir[2]);
		const gp_Vec xv = gp_Vec(up[0], up[1], up[2]).Crossed(gp_Vec(zd));
		const gp_Ax2 cs(gp_Pnt(0.0, 0.0, 0.0), zd, gp_Dir(xv));

		Handle(HLRBRep_Algo) algo = new HLRBRep_Algo();
		algo->Add(in->shape());
		algo->Projector(HLRAlgo_Projector(cs));
		algo->Update();
		algo->Hide();

		HLRBRep_HLRToShape toShape(algo);
		/* ★★ 何を図面に載せるか (本文 §3 の②)。
		 *   稜 (Sharp) と **輪郭線 (silhouette)** の 2 つ。⚠ 輪郭を落とすと **球が消える**
		 *   (曲面には稜が無いので) — 段 0 で実測した負の対照そのもの。
		 *   ⚠ 継ぎ目 (Rg1Line = 接線連続 / RgNLine = 曲率不連続) は **載せない**。
		 *     球には RgNLine が 1 本出る (段 0) が、それは表現の継ぎ目であって図面の線ではない。
		 *   ⚠ 等parameter線 (IsoLine) も同様に載せない。 */
		TopoDS_Compound comp;
		BRep_Builder    bb;
		bb.MakeCompound(comp);
		const TopoDS_Shape a = wantHidden ? toShape.HCompound()        : toShape.VCompound();
		const TopoDS_Shape b = wantHidden ? toShape.OutLineHCompound() : toShape.OutLineVCompound();
		if ( ! a.IsNull() ) bb.Add(comp, a);
		if ( ! b.IsNull() ) bb.Add(comp, b);

		/* ⚠ 線が 1 本も無いことは在る (真上から見た平板の不可視など)。
		 *   ★ それは **空の 2D** であってエラーではない (空集合は正当な答え)。 */
		out = thNEW(ocFace2D,());
		out->set_shape(comp);
	} catch ( const Standard_Failure& e ) {
		std::string m = std::string("hlr: OCCT failed [") + e.DynamicType()->Name() + "] (" +
		    ( ( e.GetMessageString() && e.GetMessageString()[0] ) ? e.GetMessageString() : "no message" ) + ")";
		result = oca_err(thNEW(stdString,(m.c_str())));
		out = thNULL;
		return;
	}
	if ( (result = oc_abort_err(brk_, "hlr")) != thNULL ) { out = thNULL; return; }
}

sPtr<pigData>
ocaHlr_::get_result()
{
	return ( result != thNULL ) ? result : sPtr<pigData>::d_cast(out);
}
