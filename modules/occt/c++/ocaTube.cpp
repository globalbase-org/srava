/*
 * ocaTube — occt の tube(path[, opts]) (#3470)。path = [[[x,y,z], r], …]。
 *
 * ★★ **他カーネルの tube とは形が違う** (ひさ判断 2026-09-01)。同じ op 名だが同じ形ではない:
 *
 *     cgal / manifold   折れ線の背骨 + segs 角形近似の断面   (共通ヘッダ src/h/common/tube.h)
 *     occt (これ)       点を通る **C2 の B-spline** の背骨 + **厳密な円**の断面
 *
 *   起票時 (#3470) は「occt の tube がスプラインだと同じ入力から違う形が出るので折れ線のまま
 *   にする」方針だったが、**その懸念を承知のうえで形が変わる側を採った**。理由は、occt を使う
 *   価値がまさに「解析曲面として持てる」ことにあるため:
 *     - offset が厳密になる (occt の offset は Steiner の公式と 9〜10 桁一致する実績)
 *     - fillet / chamfer が効く
 *     - STEP に実物の曲面が載る
 *   ⇒ **cg/mf の tube と厳密に一致させることはできない**。カーネル一致の表には入れない
 *     (#3461 で occt の sphere を入れられなかったのと同じ形)。
 *   ⇒ 折れ線の管が欲しければ "cgal"::tube(…) / "manifold"::tube(…) と指名する (#3467)。
 *
 * ★ segs は **無視する**。背骨も断面も滑らかなので分割数に意味が無い
 *   (occt の sphere が seg を無視しているのと同じ扱い)。互換のため受け取るだけ。
 *
 * ★ 自己交差する背骨は **明示エラー**。既存の tube は自己交差を許容する仕様 (とぐろを値として
 *   作れる) だが、OCCT の MakePipeShell は失敗するか不正な B-rep を作るので、黙って壊れた
 *   shape を返さない。★ カーネルによって挙動が変わる点なのでエラー文にもそう書く。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"oc/c++/ocShape.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ocaTube_.h"

#include	<BRepOffsetAPI_MakePipeShell.hxx>
#include	<BRepBuilderAPI_MakeEdge.hxx>
#include	<BRepBuilderAPI_MakeWire.hxx>
#include	<GeomAPI_Interpolate.hxx>
#include	<GeomAPI_ProjectPointOnCurve.hxx>
#include	<Geom_BSplineCurve.hxx>
#include	<Geom_Circle.hxx>
#include	<TColgp_HArray1OfPnt.hxx>
#include	<gp_Pnt.hxx>
#include	<gp_Dir.hxx>
#include	<gp_Ax2.hxx>
#include	<gp_Vec.hxx>
#include	<TopoDS_Shape.hxx>
#include	<TopoDS_Wire.hxx>
#include	<Standard_Failure.hxx>
#include	<vector>
#include	<cmath>
#include	<string>

CLASS_TINYSTATE(oc/c++/ocaTube,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ocaTube_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	/* ⚠ get_result は **public** に書く。protected に書くと外側クラスへの転送 thunk が
	 *   生成されず、基底の get_result が使われて **値 (TEXT) が返る** (実際に踏んだ)。
	 *   ocaTorus 等の既存 op と同じ位置に揃えること。 */
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

ocaTube_::ocaTube_(TS_ARGS0)
	: ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
	TS_CPARGS0
}

static sPtr<pigData> terr(const char *m) { return oca_err(thNEW(stdString,(m))); }

/* 背骨の自己交差判定。
 *
 * ★★ 判定は 2 条件の **積**: 「空間で近い」かつ「**経路上で離れている**」。
 *   空間距離だけで見ると **曲がった管を誤検知する** (実測: 半径 5 の輪を 32 点で書くと、
 *   隣の隣の区間が 1 弦 = 0.98 しか離れていないので、管半径 0.5 の和 1.0 を下回って落ちた)。
 *   管が自分に触れて戻ってくるには最低でも半円 = 経路長 pi*r が要るので、
 *   **最接近点どうしの経路上の隔たりが pi*r 未満なら「ただの曲がり」**として除外する。
 *
 *   U 字 (0,0,0)->(10,0,0)->(10,0.2,0)->(0,0.2,0) は、平行な 2 区間の最接近が
 *   端どうし (経路上 20.2 隔たり) に出るので正しく捕まる。
 *
 * ⚠ 保守的な近似であって厳密判定ではない。B-spline は制御点の折れ線に沿うので、折れ線で
 *   重なっていればスプラインも重なる (逆は必ずしも成り立たない)。取りこぼしは OCCT 側の
 *   失敗として現れ、そちらも明示エラーになる。 */
static bool spine_self_intersects(const std::vector<gp_Pnt>& P, const std::vector<double>& R,
                                  bool closed)
{
	int n = (int)P.size();
	/* 各頂点までの累積経路長 (最接近点の経路上位置を出すため)。 */
	std::vector<double> arc((size_t)n, 0.0);
	for ( int i = 1 ; i < n ; ++i ) arc[(size_t)i] = arc[(size_t)i-1] + P[i-1].Distance(P[i]);
	double total = arc[(size_t)n-1];

	for ( int i = 0 ; i + 1 < n ; ++i )
	for ( int j = i + 2 ; j + 1 < n ; ++j ) {
		gp_Vec u(P[i], P[i+1]), v(P[j], P[j+1]), w(P[j], P[i]);
		double a = u.Dot(u), b = u.Dot(v), c = v.Dot(v), d = u.Dot(w), e = v.Dot(w);
		double den = a*c - b*b, sp = 0, tp = 0;
		if ( std::fabs(den) > 1e-14 ) { sp = (b*e - c*d)/den; tp = (a*e - b*d)/den; }
		else                          { sp = 0; tp = ( c > 1e-14 ) ? e/c : 0; }
		if ( sp < 0 ) sp = 0; if ( sp > 1 ) sp = 1;
		if ( tp < 0 ) tp = 0; if ( tp > 1 ) tp = 1;
		gp_Pnt ps = P[i].Translated(u * sp), pt = P[j].Translated(v * tp);
		double rr = ( 1.0 - sp )*R[i] + sp*R[i+1] + ( 1.0 - tp )*R[j] + tp*R[j+1];
		if ( ps.Distance(pt) >= rr ) continue;                 /* 空間で離れている */
		/* 経路上の隔たり。closed なら反対回りの短いほうを採る。 */
		double si = arc[(size_t)i] + sp*P[i].Distance(P[i+1]);
		double sj = arc[(size_t)j] + tp*P[j].Distance(P[j+1]);
		double gap = std::fabs(sj - si);
		if ( closed && total > 0 && gap > total*0.5 ) gap = total - gap;
		double rmax = 0.5*rr;                                  /* = 平均半径 */
		if ( gap < 3.14159265358979323846 * rmax ) continue;   /* ただの曲がり */
		return true;
	}
	return false;
}

void
ocaTube_::compute()
{
	ocShape::ensure_init();   /* ★ OCCT の診断出力を stdout から外す (ocShape.h 参照) */
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<pigDataArray> path = ( na > 0 ) ? (*args)[0]->obt_array() : sPtr<pigDataArray>();
	int nraw = path.is_notNull() ? path->length() : 0;
	if ( nraw < 2 ) {
		result = terr("tube: needs >= 2 path vertices ([[[x,y,z],r],...])"); return;
	}

	/* opts: {closed:1} は周期スプライン (srava に真偽値リテラルは無いので 0/1。
	 *   module(so,{optional:1}) と同じ書き方)。★ 2 つめの引数が数値なら segs とみなして無視する
	 *   (メッシュ系と同じ書き方をそのまま通すため。occt では分割数に意味が無い)。 */
	bool closed = false;
	if ( na > 1 && (*args)[1] != thNULL ) {
		sPtr<pigData> o = (*args)[1];
		sPtr<pigData> vc = o->get_ix(thNEW(pigDataString,("closed")));
		if ( vc != thNULL && ! vc->is_error() ) closed = ( vc->get_int() != 0 );
	}

	/* ---- パス読み取り: 各要素 [位置, r]。★ 2D ([x,y]) は z=0 として受ける (メッシュ系と同じ) ---- */
	std::vector<gp_Pnt> P; P.reserve((size_t)nraw);
	std::vector<double> R; R.reserve((size_t)nraw);
	for ( int i = 0 ; i < nraw ; ++i ) {
		sPtr<pigDataArray> pr = path->get_ix(thNEW(pigDataInteger,((INTEGER64)i)))->obt_array();
		if ( ! pr.is_notNull() || pr->length() < 2 ) {
			result = terr("tube: each vertex must be [pos, r] (pos=[x,y,z] or [x,y])"); return;
		}
		sPtr<pigDataArray> pos = pr->get_ix(thNEW(pigDataInteger,((INTEGER64)0)))->obt_array();
		int pl = pos.is_notNull() ? pos->length() : 0;
		if ( pl < 2 ) { result = terr("tube: vertex position must be [x,y,z] or [x,y]"); return; }
		double x = pos->get_ix(thNEW(pigDataInteger,((INTEGER64)0)))->get_flt();
		double y = pos->get_ix(thNEW(pigDataInteger,((INTEGER64)1)))->get_flt();
		double z = ( pl >= 3 ) ? pos->get_ix(thNEW(pigDataInteger,((INTEGER64)2)))->get_flt() : 0.0;
		double r = pr->get_ix(thNEW(pigDataInteger,((INTEGER64)1)))->get_flt();
		if ( !(r > 0) ) {
			/* ⚠ メッシュ系は r=0 の尖り端を許すが、B-rep の円断面は半径 0 を作れない
			 *   (退化した Geom_Circle になる)。黙って潰さず明示エラーにする。 */
			result = terr("tube: occt requires every radius > 0 "
			              "(a zero-radius endpoint degenerates the circular section; "
			              "use \"cgal\"::tube / \"manifold\"::tube for tapered-to-a-point tubes)");
			return;
		}
		/* 直前と同一点は B-spline 補間が解けないので落とす。 */
		gp_Pnt p(x, y, z);
		if ( ! P.empty() && p.Distance(P.back()) < 1e-12 ) continue;
		P.push_back(p); R.push_back(r);
	}
	if ( P.size() < 2 ) { result = terr("tube: needs >= 2 distinct path vertices"); return; }

	if ( spine_self_intersects(P, R, closed) ) {
		result = terr("tube: occt cannot sweep a self-intersecting spine "
		              "(the pipe would overlap itself). Note this differs from "
		              "\"cgal\"::tube / \"manifold\"::tube, which allow self-intersection "
		              "and leave it to valid()/repair()");
		return;
	}

	const char *phase = "spine";
	try {
		/* ---- 背骨: 点を通る C2 の B-spline ---- */
		Handle(TColgp_HArray1OfPnt) pts = new TColgp_HArray1OfPnt(1, (Standard_Integer)P.size());
		for ( size_t i = 0 ; i < P.size() ; ++i ) pts->SetValue((Standard_Integer)(i+1), P[i]);
		GeomAPI_Interpolate interp(pts, closed ? Standard_True : Standard_False, 1e-7);
		interp.Perform();
		if ( ! interp.IsDone() ) { result = terr("tube: could not interpolate a spline through the path"); return; }
		Handle(Geom_BSplineCurve) spine = interp.Curve();

		BRepBuilderAPI_MakeEdge me(spine);
		if ( ! me.IsDone() ) { result = terr("tube: could not build the spine edge"); return; }
		BRepBuilderAPI_MakeWire mw(me.Edge());
		if ( ! mw.IsDone() ) { result = terr("tube: could not build the spine wire"); return; }
		TopoDS_Wire spineW = mw.Wire();

		phase = "sections";
		/* ---- 掃引: 各点の半径の円を Add して補間させる (可変半径が素直に入る) ---- */
		/* ★ 閉じた背骨 (closed:1) は **断面 1 枚**で掃く。OCCT の MakePipeShell は
		 *   閉じた背骨に複数断面を Add すると Build() で Standard_OutOfRange を投げる
		 *   (実測)。断面 1 枚なら背骨全体に沿って掃いてくれる。
		 *   ⇒ 閉じた輪で半径を変えることは **できない**。黙って一定にせず明示エラーにする。 */
		if ( closed ) {
			for ( size_t i = 1 ; i < R.size() ; ++i )
				if ( std::fabs(R[i] - R[0]) > 1e-12 ) {
					result = terr("tube: occt cannot vary the radius along a closed spine "
					              "(closed:1 requires one radius for every vertex); "
					              "use an open path, or \"cgal\"::tube / \"manifold\"::tube");
					return;
				}
		}
		BRepOffsetAPI_MakePipeShell shell(spineW);
		/* ★ ねじれ最小のフレーム。common/tube.h の rotation-minimizing frame と同じ思想。
		 *   ⚠ SetMode(IsFrenet) は **false で corrected Frenet** (ねじれ補正あり)。true にすると
		 *     素の Frenet になり、曲率が消える点で法線が飛んで断面が回ってしまう。 */
		shell.SetMode(Standard_False);
		size_t nsec = closed ? 1 : P.size();
		for ( size_t i = 0 ; i < nsec ; ++i ) {
			Standard_Real u = 0;
			GeomAPI_ProjectPointOnCurve pj(P[i], spine);
			u = ( pj.NbPoints() > 0 ) ? pj.LowerDistanceParameter() : spine->FirstParameter();
			gp_Pnt pos; gp_Vec tan;
			spine->D1(u, pos, tan);
			if ( tan.Magnitude() < 1e-12 ) tan = gp_Vec(0,0,1);
			gp_Ax2 ax(P[i], gp_Dir(tan));
			Handle(Geom_Circle) c = new Geom_Circle(ax, R[i]);
			BRepBuilderAPI_MakeEdge ce(c);
			BRepBuilderAPI_MakeWire cw(ce.Edge());
			shell.Add(cw.Wire(), Standard_False, Standard_False);
		}
		phase = "build";
		shell.Build();
		if ( ! shell.IsDone() ) { result = terr("tube: OCCT could not sweep the section along the spine"); return; }
		phase = "solid";
		if ( ! shell.MakeSolid() ) { result = terr("tube: swept surface could not be closed into a solid"); return; }
		TopoDS_Shape sh = shell.Shape();
		if ( sh.IsNull() ) { result = terr("tube: OCCT produced a null shape"); return; }
		out = thNEW(ocShape,());
		out->set_shape(sh);
	} catch ( const Standard_Failure& e ) {
		/* ★ OCCT は例外で失敗を知らせることがある。ワーカースレッドから throw を漏らすと
		 *   agent ごと死ぬので、ここで必ず受ける (geogram で踏んだのと同じ形)。 */
		/* ★ OCCT の例外はメッセージが空のことがあるので **型名**も出す (Standard_ConstructionError
		 *   なのか NotDone なのかで直す場所が違う)。 */
		std::string m = std::string("tube: OCCT failed to sweep at '") + phase + "' [" +
		                e.DynamicType()->Name() + "] (" +
		                ( ( e.GetMessageString() && e.GetMessageString()[0] ) ? e.GetMessageString()
		                                                                      : "no message" ) + ")";
		result = terr(m.c_str());
		return;
	}
}

sPtr<pigData>
ocaTube_::get_result()
{
	return ( result != thNULL ) ? result : out;
}
