/*
 * ocmPolygonize — polygonize(cross2d, defl) (#3472)。occt の 2D 領域 (oc-cross2d) の
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
#include	<Standard_Failure.hxx>
#include	<vector>
#include	<algorithm>
#include	<cmath>
#include	<string>
#include	"pig/c++/pigModuleError.h"
/* ★ #3475: このモジュール専用のエラー生成子 (共有ヘッダを持たないので
 *   ここで定義する)。文言は "[TAG] occt_mf/op: message" になる。 */
PIG_DEFINE_MODULE_ERR(ocm_err, "occt_mf")


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
		    "polygonize: input must be a 2D region (oc-cross2d)")));
		return;
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
						ring.push_back(manifold::vec2(p.X(), p.Y()));
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
		if ( ps.empty() ) {
			result = ocm_err(thNEW(stdString,(
			    "polygonize: the 2D region has no wire to polygonize")));
			return;
		}
		out = thNEW(mfCross,(manifold::CrossSection(ps, manifold::CrossSection::FillRule::NonZero)));
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
