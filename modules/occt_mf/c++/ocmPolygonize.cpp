/*
 * ocmPolygonize — polygonize(cross2d, defl) (#3472)。occt の 2D 領域 (oc-face3d) の
 *   **曲線の輪郭を折れ線へ落として** manifold の 2D (mf-cross2d) にする。
 *
 * ★★ **これは cast ではない** (ひさ指示・2026-09-01)。曲線を折れ線に落とすには
 *   **粒度の指定が要る**ので、cast (= 精度を変えない変換だけ) には置けない。
 *   3D で「曲面を三角形に落とす」ocmTriangulate(s, defl) が cast でないのと **同じ理由**。
 *   規約は docs/srava_module_reference.md §型変換の規約 (#conversion) に明文化してある。
 *
 * ★ defl (deflection) = **曲線と弦の最大距離**。小さいほど点が増える。triangulate と同じ単位に
 *   揃えてある (呼び分けで迷わないため)。
 *
 * ★ 穴の扱い: OCCT の Face は外周 wire と穴 wire を持ち、向き (Forward/Reversed) で区別する。
 *   manifold の CrossSection は **外周 CCW・穴 CW を NonZero で**受けるので、そこへ写す
 *   (mfCross::decode_cross_exact と同じ方針)。⇒ 'O' や 'あ' の内側がちゃんと穴になる。
 *
 * ★ 落とした先では既存の 2D 資産がそのまま使える (2D ブール / extrude / revolve)。
 *   さらに cast("cg-cross2d", …) は **無損失昇格**なので、cgal の 2D (Boolean_set_operations_2 /
 *   straight-skeleton offset / repair) へも既存経路で渡る ⇒ 行き先は mf-cross2d 1 つで足りる。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"oc/c++/ocShape.h"
#include	"mf/c++/mfMesh.h"
#include	<BRepLib_FindSurface.hxx>   /* ★ #3544 段 3: 稜だけの 2D から平面を導く */
#include	<ShapeAnalysis_FreeBounds.hxx>   /* ★ #3544 段 3: 自由な稜を輪に繋ぐ */
#include	<TopTools_HSequenceOfShape.hxx>
#include	<BRepAdaptor_Curve.hxx>
#include	<GCPnts_QuasiUniformDeflection.hxx>
#include	"common/affine.h"   /* ★ #3536: 平面 → 正準な枠 (plane_frame_canonical) */
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ocmPolygonize_.h"

#include	<BRepMesh_IncrementalMesh.hxx>
#include	<BRep_Tool.hxx>
#include	<Poly_Polygon3D.hxx>
#include	<Poly_PolygonOnTriangulation.hxx>
#include	<Poly_Triangulation.hxx>
#include	<TColgp_Array1OfPnt.hxx>
#include	<TColStd_Array1OfInteger.hxx>
#include	<TopExp_Explorer.hxx>
#include	<TopExp.hxx>
#include	<TopoDS.hxx>
#include	<TopoDS_Face.hxx>
#include	<TopoDS_Wire.hxx>
#include	<TopoDS_Edge.hxx>
#include	<BRepTools_WireExplorer.hxx>
#include	<BRepTools.hxx>
#include	<TopLoc_Location.hxx>
#include	<gp_Pnt.hxx>
#include	<Geom_Plane.hxx>            /* ★ #3518: z=0 平面に居るかの検査 */
#include	<Geom_Surface.hxx>
#include	<Precision.hxx>
#include	<Standard_Failure.hxx>
#include	<vector>
#include	<algorithm>
#include	<cmath>
#include	<string>
#include	"pig/c++/pigModuleError.h"
/* ★ #3475: このモジュール専用のエラー生成子 (共有ヘッダを持たないので
 *   ここで定義する)。文言は "[TAG] occt_mf/op: message" になる。 */
PIG_DEFINE_MODULE_ERR(ocm_err, "occt_mf")


/* ★★ #3518 → #3536: 面が載っている **平面を取り出して、正準な枠 (O,U,V) にする**。
 *
 * occt の 2D (oc-face3d) は TopoDS_Face なので、変換 op が 2D を受けるようになった時点で
 * 「傾いた断面」「持ち上げた断面」が作れるようになった (それがこの型の取り柄)。ところが
 * polygonize は輪郭の点から **x,y だけを読んで**いた = 黙って XY へ射影する。実測 (2026-09-12):
 *
 *     polygonize(rect(2,3))                    area = 6         正しい
 *     polygonize(rotate(rect(2,3),"x",45))     area = 4.2426    ← 射影 (6·cos45)
 *     polygonize(translate(rect(2,3),[0,0,5])) area = 6         ← z が消える
 *     polygonize(rotate(rect(2,3),"x",90))     area = 0         ← **空** なのにエラー無し
 *
 * ⇒ #3518 ではこれを **明示エラーで断って**いた (z=0 の面しか通さない)。当時はそれが唯一の
 *   正しい振る舞いだった — 行き先の mf-cross2d が z=0 に縛られていて、置き場所を持てなかった。
 *
 * ★★ #3526 で mf-cross2d が **枠 (平面)** を持つようになったので、いまは *射影せずに渡せる*:
 *   平面を枠にして、輪郭の点を局所座標 ((p-O)·U, (p-O)·V) で持てばよい。
 *   ⇒ **射影ではなく保つ**変換なので、#3518 の「射影は polygonize の仕事ではない」に触らない。
 *   ⇒ 落とすのは *z=0 の縛り* だけ。⚠ 「曲面上の面は折れ線にできない」は **そのまま断る**。
 *
 * ★ 枠は @srava_affine::plane_frame_canonical@ が作る = **平面だけから決まる**。
 *   OCCT の gp_Ax3 (XDirection) をそのまま使うと *面の作られ方で局所座標が変わる* ので使わない。
 * ★ z=0 平面なら枠は既定 ⇒ 局所座標は従来どおりの world (x,y) で、blob もバイト単位で同じ。
 *
 * ⚠ **全ての面が同じ平面に載っていること**も要る (従来は面ごとに z=0 かを見ていたので、
 *   別々の平面の面が混ざる形は端から存在しなかった)。混ざっていたら断る — どれか 1 つの
 *   平面へ落とせば、残りは黙って射影されることになる。
 *
 * ★★ #3544 段 3 (2026-09-17): **面が 1 枚も無い 2D** も受ける。
 *   DXF から読んだ図面と @hlr@ の出力は *稜だけ* の 2D で、面から平面を導けない。
 *   ⚠ 従来はそれを「1 = 曲面上の面」と同じ戻り値にしていたので、利用者には
 *     「the 2D region lies on a curved surface」と **事実と違うこと**を言っていた
 *     (曲面に載っているのではなく、面が 0 枚なだけ)。⇒ 戻り値を分ける。
 *   ★ 平面は @BRepLib_FindSurface@ (OnlyPlane) が稜の集合からも見つける。
 *
 * 戻り: 0 = 平面が取れて枠を作った / 1 = 平面でない面がある / 2 = 面が複数の平面に散っている
 *       3 = 中身が無い / 4 = 稜が同一平面に載っていない。 */
static int
ocm_plane_frame(const TopoDS_Shape &sh, double o[3], double u[3], double v[3])
{
	int have = 0;
	double n0[3] = { 0, 0, 0 }, d0 = 0, tol0 = Precision::Confusion();
	for ( TopExp_Explorer fx(sh, TopAbs_FACE) ; fx.More() ; fx.Next() ) {
		TopoDS_Face f = TopoDS::Face(fx.Current());
		Handle(Geom_Surface) su = BRep_Tool::Surface(f);   /* 位置 (TopLoc) 込み */
		Handle(Geom_Plane) pl = Handle(Geom_Plane)::DownCast(su);
		if ( pl.IsNull() ) return 1;                       /* 曲面上の面 — 折れ線にできない */
		const gp_Ax3 &ax = pl->Position();
		double tol = BRep_Tool::Tolerance(f);
		if ( tol < Precision::Confusion() ) tol = Precision::Confusion();
		const double n[3] = { ax.Direction().X(), ax.Direction().Y(), ax.Direction().Z() };
		const double p[3] = { ax.Location().X(),  ax.Location().Y(),  ax.Location().Z()  };
		const double d = n[0]*p[0] + n[1]*p[1] + n[2]*p[2];
		if ( ! have ) {
			for ( int k = 0 ; k < 3 ; ++k ) n0[k] = n[k];
			d0 = d; tol0 = tol; have = 1;
			continue;
		}
		/* 同じ平面か: 法線が平行 (符号は問わない) かつ原点からの距離が一致。 */
		const double cx = n0[1]*n[2] - n0[2]*n[1];
		const double cy = n0[2]*n[0] - n0[0]*n[2];
		const double cz = n0[0]*n[1] - n0[1]*n[0];
		const double dot = n0[0]*n[0] + n0[1]*n[1] + n0[2]*n[2];
		const double t = ( tol > tol0 ) ? tol : tol0;
		if ( ::sqrt(cx*cx + cy*cy + cz*cz) > 1e-9 ) return 2;
		if ( ::fabs(( dot < 0 ? -d : d ) - d0) > t )  return 2;
	}
	if ( ! have ) {
		/* ★ 面が 1 枚も無い = **図面** (稜だけの 2D)。平面は稜から導く。 */
		int nedge = 0;
		for ( TopExp_Explorer ex(sh, TopAbs_EDGE) ; ex.More() ; ex.Next() ) ++nedge;
		if ( nedge == 0 ) return 3;
		BRepLib_FindSurface fs(sh, -1.0, Standard_True /* OnlyPlane */);
		if ( ! fs.Found() ) return 4;
		Handle(Geom_Plane) pl = Handle(Geom_Plane)::DownCast(fs.Surface());
		if ( pl.IsNull() ) return 4;
		const gp_Ax3 &ax = pl->Position();
		n0[0] = ax.Direction().X(); n0[1] = ax.Direction().Y(); n0[2] = ax.Direction().Z();
		const double pp[3] = { ax.Location().X(), ax.Location().Y(), ax.Location().Z() };
		d0 = n0[0]*pp[0] + n0[1]*pp[1] + n0[2]*pp[2];
	}
	const double pt[3] = { n0[0]*d0, n0[1]*d0, n0[2]*d0 };
	return srava_affine::plane_frame_canonical(n0, pt, o, u, v) ? 0 : 1;
}


CLASS_TINYSTATE(ocm/c++/ocmPolygonize,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ocmPolygonize_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<mfCross>	out;
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
class mfCross;
TS_END_INTERFACE

#endif

ocmPolygonize_::ocmPolygonize_(TS_ARGS0)
	: ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
	TS_CPARGS0
}

/* 符号付き面積 (シューレース)。> 0 が CCW。 */
static double ring_signed_area(const manifold::SimplePolygon &r)
{
	double a = 0.0;
	for ( size_t i = 0, n = r.size() ; i < n ; ++i ) {
		const manifold::vec2 &p = r[i], &q = r[(i + 1) % n];
		a += p.x * q.y - q.x * p.y;
	}
	return 0.5 * a;
}

void
ocmPolygonize_::compute()
{
	ocShape::ensure_init();   /* ★ OCCT の診断出力を stdout から外す */
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<ocFace2D> in = ( na > 0 ) ? sPtr<ocFace2D>::d_cast((*args)[0]) : sPtr<ocFace2D>();
	if ( ! in.is_notNull() || in->shape().IsNull() ) {
		result = ocm_err(thNEW(stdString,(
		    "polygonize: input must be a 2D region (oc-face3d)")));
		return;
	}
	/* ★★ #3518 → #3536: 面外へ出た 2D を **黙って射影しない**。ただし *断る* のではなく
	 *   平面を枠として引き継ぐ (上の ocm_plane_frame)。 */
	double fo[3], fu[3], fv[3];
	{
		int bad = ocm_plane_frame(in->shape(), fo, fu, fv);
		if ( bad == 1 ) {
			result = ocm_err(thNEW(stdString,(
			    "polygonize: the 2D region lies on a curved surface, so it has no polyline "
			    "outline (only occt can hold such a region; cut it with a plane first)")));
			return;
		}
		/* ★ #3544 段 3: 面が 0 枚の 2D (図面) の断り方を **事実に合わせて**分ける。
		 *   ⚠ 以前はどちらも上の「曲面に載っている」に落ちていた。 */
		if ( bad == 3 ) {
			result = ocm_err(thNEW(stdString,("polygonize: the 2D value is empty")));
			return;
		}
		if ( bad == 4 ) {
			result = ocm_err(thNEW(stdString,(
			    "polygonize: this 2D drawing has no face, and its edges do not all lie on one "
			    "plane, so there is no plane to carry it to")));
			return;
		}
		if ( bad == 2 ) {
			result = ocm_err(thNEW(stdString,(
			    "polygonize: the 2D region has faces on more than one plane, so it cannot be "
			    "carried to a single 2D plane; take the faces apart and polygonize them one "
			    "by one")));
			return;
		}
	}

	double defl = ( na > 1 ) ? (*args)[1]->get_flt() : 0.0;
	if ( !(defl > 0) ) {
		/* ★ 既定値を黙って使わない。**粒度を書かせる**のがこの op の存在理由そのもの。 */
		result = ocm_err(thNEW(stdString,(
		    "polygonize: deflection (max chord distance) must be > 0 "
		    "(this is the granularity of the polyline; it is required, not optional)")));
		return;
	}

	try {
		/* ★★ 折れ線化は **triangulate と同じ機構** (BRepMesh_IncrementalMesh) を使う。
		 *   ⚠ 最初 GCPnts_TangentialDeflection(ac, 0.1, defl) で刻んだが、**角度誤差 0.1 rad が
		 *     律速して defl が効かなかった** (defl を 1 → 0.1 と変えても面積が 1 ビットも動かない)。
		 *     角度と弦誤差は単位が違うので、片方を定数で置くと粒度のつまみが死ぬ。
		 *   ⇒ BRepMesh に defl を渡し、**それが稜に貼った折れ線をそのまま読む**。
		 *     こうすると polygonize(x, d) と triangulate(x, d) の d が **同じ意味**になる。 */
		TopoDS_Shape sh = in->shape();
		BRepMesh_IncrementalMesh mesher(sh, defl);
		if ( ! mesher.IsDone() ) {
			result = ocm_err(thNEW(stdString,("polygonize: BRepMesh failed")));
			return;
		}
		manifold::Polygons ps;
		for ( TopExp_Explorer fx(sh, TopAbs_FACE) ; fx.More() ; fx.Next() ) {
			TopoDS_Face f = TopoDS::Face(fx.Current());
			/* ★ 外周 wire は OCCT が知っている。向きの申告から推測しない。 */
			TopoDS_Wire outerW = BRepTools::OuterWire(f);
			TopLoc_Location floc;
			for ( TopExp_Explorer wx(f, TopAbs_WIRE) ; wx.More() ; wx.Next() ) {
				TopoDS_Wire w = TopoDS::Wire(wx.Current());
				manifold::SimplePolygon ring;
				/* ★ WireExplorer は **稜を順序どおり・向きを揃えて**巡る。TopExp_Explorer で
				 *   TopAbs_EDGE を回すと順序が保証されず、輪が繋がらない (作法の要点)。 */
				for ( BRepTools_WireExplorer ex(w, f) ; ex.More() ; ex.Next() ) {
					TopoDS_Edge e = ex.Current();
					/* BRepMesh が稜に貼った折れ線を読む。面の三角形分割上の折れ線
					 * (PolygonOnTriangulation) が本命で、無ければ 3D 折れ線へ落ちる。 */
					TopLoc_Location eloc;
					Handle(Poly_Triangulation) ftri = BRep_Tool::Triangulation(f, floc);
					Handle(Poly_PolygonOnTriangulation) pt =
					    ftri.IsNull() ? Handle(Poly_PolygonOnTriangulation)()
					                  : BRep_Tool::PolygonOnTriangulation(e, ftri, floc);
					std::vector<gp_Pnt> pts;
					if ( ! pt.IsNull() && ! ftri.IsNull() ) {
						const TColStd_Array1OfInteger &nodes = pt->Nodes();
						const gp_Trsf &tr = floc.Transformation();
						for ( Standard_Integer i = nodes.Lower() ; i <= nodes.Upper() ; ++i )
							pts.push_back(ftri->Node(nodes(i)).Transformed(tr));
					} else {
						Handle(Poly_Polygon3D) p3 = BRep_Tool::Polygon3D(e, eloc);
						if ( p3.IsNull() ) continue;
						const TColgp_Array1OfPnt &nn = p3->Nodes();
						const gp_Trsf &tr = eloc.Transformation();
						for ( Standard_Integer i = nn.Lower() ; i <= nn.Upper() ; ++i )
							pts.push_back(nn(i).Transformed(tr));
					}
					int n = (int)pts.size();
					if ( n < 2 ) continue;
					/* 稜の向き。REVERSED なら逆順に積む (輪の巡り方を保つ)。
					 * ★ 末尾の点は次の稜の先頭と重なるので落とす (輪は閉じるので最後も落としてよい)。 */
					bool rev = ( e.Orientation() == TopAbs_REVERSED );
					for ( int k = 0 ; k < n - 1 ; ++k ) {
						const gp_Pnt &p = rev ? pts[(size_t)(n - 1 - k)] : pts[(size_t)k];
						/* ★ #3536: 枠の局所座標へ。⚠ 射影ではない — 面は枠の平面に載って
						 *   いる (ocm_plane_frame が確かめた) ので、捨てている成分は無い。
						 *   ★ 枠が既定 (z=0) なら U=(1,0,0) V=(0,1,0) O=0 なので
						 *     ここは従来どおり p.X() / p.Y() をそのまま積むのと同じ式になる。 */
						const double d[3] = { p.X() - fo[0], p.Y() - fo[1], p.Z() - fo[2] };
						ring.push_back(manifold::vec2(
						    d[0]*fu[0] + d[1]*fu[1] + d[2]*fu[2],
						    d[0]*fv[0] + d[1]*fv[1] + d[2]*fv[2]));
					}
				}
				if ( ring.size() < 3 ) continue;
				/* ★ 外周は CCW・穴は CW にする (manifold の NonZero が受ける形)。
				 *   ⚠ OCCT の wire の向きは Face の向きとの組み合わせで決まるので、
				 *     **向きの申告を信じず符号付き面積で揃える** — こちらのほうが頑健。 */
				bool isOuter = w.IsSame(outerW);
				double sa = ring_signed_area(ring);
				if ( ( sa > 0 ) != isOuter )
					std::reverse(ring.begin(), ring.end());
				ps.push_back(ring);
			}
		}
		/* ★★ #3544 段 3: **面が 1 枚も無い 2D (図面)** の経路。
		 *   DXF から読んだ図面と @hlr@ の出力は稜だけなので、上の FACE → WIRE の巡りでは
		 *   1 本も拾えない。⇒ 稜を繋いで輪にしてから読む。
		 *   ⚠ **閉じた輪だけ**が領域になる。開いた線 (図面の線) は mf-cross2d に
		 *     置き場所が無い (manifold は guide 層を持たない) ので、黙って捨てず断る。 */
		int openChains = 0;
		if ( ps.empty() ) {
			Handle(TopTools_HSequenceOfShape) edges = new TopTools_HSequenceOfShape();
			for ( TopExp_Explorer ex(sh, TopAbs_EDGE) ; ex.More() ; ex.Next() )
				edges->Append(ex.Current());
			Handle(TopTools_HSequenceOfShape) wires;
			if ( edges->Length() > 0 )
				ShapeAnalysis_FreeBounds::ConnectEdgesToWires(edges, Precision::Confusion(),
				                                              Standard_False, wires);
			for ( Standard_Integer wi = 1 ; ! wires.IsNull() && wi <= wires->Length() ; ++wi ) {
				TopoDS_Wire w = TopoDS::Wire(wires->Value(wi));
				if ( ! BRep_Tool::IsClosed(w) ) { ++openChains; continue; }
				manifold::SimplePolygon ring;
				for ( BRepTools_WireExplorer ex(w) ; ex.More() ; ex.Next() ) {
					TopoDS_Edge e = ex.Current();
					TopLoc_Location eloc;
					Handle(Poly_Polygon3D) p3 = BRep_Tool::Polygon3D(e, eloc);
					std::vector<gp_Pnt> pts;
					if ( ! p3.IsNull() ) {
						const TColgp_Array1OfPnt &nn = p3->Nodes();
						const gp_Trsf &tr = eloc.Transformation();
						for ( Standard_Integer i = nn.Lower() ; i <= nn.Upper() ; ++i )
							pts.push_back(nn(i).Transformed(tr));
					} else {
						/* ⚠ BRepMesh が自由な稜に折れ線を貼らない版もある。
						 *   ⇒ **黙って落とさず**、こちらで刻む (粒度は同じ defl)。 */
						BRepAdaptor_Curve ac(e);
						GCPnts_QuasiUniformDeflection d(ac, defl);
						if ( ! d.IsDone() || d.NbPoints() < 2 ) continue;
						for ( int i = 1 ; i <= d.NbPoints() ; ++i ) pts.push_back(d.Value(i));
					}
					const int n = (int)pts.size();
					if ( n < 2 ) continue;
					const bool rev = ( e.Orientation() == TopAbs_REVERSED );
					for ( int k = 0 ; k < n - 1 ; ++k ) {
						const gp_Pnt &pp = rev ? pts[(size_t)(n - 1 - k)] : pts[(size_t)k];
						const double d3[3] = { pp.X() - fo[0], pp.Y() - fo[1], pp.Z() - fo[2] };
						ring.push_back(manifold::vec2(
						    d3[0]*fu[0] + d3[1]*fu[1] + d3[2]*fu[2],
						    d3[0]*fv[0] + d3[1]*fv[1] + d3[2]*fv[2]));
					}
				}
				if ( ring.size() < 3 ) continue;
				/* ★ 図面には外周と穴の区別が無い ⇒ **すべて CCW に揃える**。
				 *   入れ子は CrossSection の NonZero が面積の符号で解く。 */
				if ( ring_signed_area(ring) < 0 ) std::reverse(ring.begin(), ring.end());
				ps.push_back(ring);
			}
		}
		if ( ps.empty() ) {
			if ( openChains > 0 ) {
				result = ocm_err(thNEW(stdString,(
				    "polygonize: this 2D drawing is made of open lines only, and the target "
				    "type (mf-cross2d) can hold regions but not lines; close the outline "
				    "first, or keep the drawing in occt (dxf / svg export)")));
				return;
			}
			result = ocm_err(thNEW(stdString,(
			    "polygonize: the 2D region has no wire to polygonize")));
			return;
		}
		/* ⚠ 閉じた輪は拾えたが **開いた線もあった** — 落としたことを黙らない。
		 *   ★ 「線が消えた」は使う側からは *図面が欠けた* に見える。既に在る穴
		 *     (mf-cross2d に guide 層が無い) の現れなので、断って理由を言う。 */
		if ( openChains > 0 ) {
			result = ocm_err(thNEW(stdString,(
			    "polygonize: this 2D drawing mixes closed outlines with open lines; the target "
			    "type (mf-cross2d) has no place for the open ones, so they would be dropped "
			    "silently — take them apart first, or keep the drawing in occt")));
			return;
		}
		out = thNEW(mfCross,(manifold::CrossSection(ps, manifold::CrossSection::FillRule::NonZero)));
		/* ★★ #3536: **元の面が載っていた平面をそのまま持たせる**。
		 *   ⚠ 既定の枠のときは set_frame を通さない — mfCross の codec は
		 *     「枠が既定なら節を書かない」ので、z=0 の面の blob が従来とバイト単位で同じになる
		 *     (#3526 の作法)。値が動くのは *そもそも従来は断られていた* 面だけ。 */
		if ( ! out->same_frame(fo, fu, fv) )     /* 作りたての mfCross の枠は既定 */
			out->set_frame(fo, fu, fv);
		/* ★★ #3544: occt の 2D は **2 型**になったので、渡した先も対で決める。
		 *   ⚠ #3533 の「occt の 2D は常に空間の面」は覆った (HLR の出力は平らな図面)。
		 *   ★ 元の値の名乗りは **幾何から導かれる** (ocShape.h の type_name) ので、
		 *     ここでも同じ述語を見る。⇒ z=0 の面 → mf-cross2d / 空間の面 → mf-face3d。
		 *     sig の 2 行 ((oc-face3d)->mf-face3d;(oc-cross2d)->mf-cross2d) と一致する。
		 *   ⚠ 上の set_frame と整合する: z=0 の面なら枠は既定のままなので、
		 *     mfCross 側の規約 (枠が既定 + placed_=0 = cross2d) と離れない。 */
		out->set_placed(in->on_z0_plane() ? 0 : 1);
	} catch ( const Standard_Failure& e ) {
		std::string m = std::string("polygonize: OCCT failed [") + e.DynamicType()->Name() + "] (" +
		    ( ( e.GetMessageString() && e.GetMessageString()[0] ) ? e.GetMessageString() : "no message" ) + ")";
		result = ocm_err(thNEW(stdString,(m.c_str())));
		return;
	}
}

sPtr<pigData>
ocmPolygonize_::get_result()
{
	return ( result != thNULL ) ? result : sPtr<pigData>::d_cast(out);
}
