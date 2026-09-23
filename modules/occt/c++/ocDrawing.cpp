/*
 * ocDrawing — occt の 2D 図面の入出力 (DXF / SVG)。#3544 段 3。設計は oc/c++/ocDrawing.h。
 */
#include "oc/c++/ocDrawing.h"

#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Compound.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <BRep_Builder.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <Geom_BSplineCurve.hxx>
#include <GCPnts_QuasiUniformDeflection.hxx>
#include <TColgp_Array1OfPnt.hxx>
#include <TColStd_Array1OfReal.hxx>
#include <TColStd_Array1OfInteger.hxx>
#include <gp_Circ.hxx>
#include <gp_Elips.hxx>
#include <gp_Pnt.hxx>
#include <gp_Dir.hxx>
#include <Standard_Failure.hxx>

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <string>
#include <vector>

/* ★ 座標は %.12g。既定の %g は有効 6 桁しかなく、100mm 級の座標が 0.001mm に量子化されて
 *   隣り合う点が潰れる。⇒ cgal 側 (cgMesh2D.cpp の SEC_COORD_FMT) と **同じ約束**にしてある。 */
#define OCD_FMT "%.12g"

static const double OCD_2PI = 6.283185307179586476925286766559;

/* ---- 単位 — ★ cgal の表をそのまま使う (二重に決めない) --------------------------------
 * ⚠ 読み手は $INSUNITS を **見ない** (cgal も見ていない)。単位で座標を換算する約束を
 *   片方のカーネルだけが持つと、同じ .dxf が読み手次第で別の大きさになる。 */
static int ocd_insunits(const char *u) {
	if ( u == 0 ) return 0;
	if ( ::strcmp(u,"in")==0 ) return 1;
	if ( ::strcmp(u,"ft")==0 ) return 2;
	if ( ::strcmp(u,"mm")==0 ) return 4;
	if ( ::strcmp(u,"cm")==0 ) return 5;
	if ( ::strcmp(u,"m") ==0 ) return 6;
	return 0;
}
static const char* ocd_svg_unit(const char *u) {
	if ( u == 0 ) return "";
	if ( ::strcmp(u,"mm")==0 || ::strcmp(u,"cm")==0 || ::strcmp(u,"in")==0 ||
	     ::strcmp(u,"px")==0 || ::strcmp(u,"pt")==0 || ::strcmp(u,"pc")==0 ) return u;
	return "";   /* m / ft 等は SVG が解釈できない → 無単位 (viewBox のみ) */
}

static void ocd_err(char *err, int errsz, const char *m) {
	if ( err != 0 && errsz > 0 ) ::snprintf(err, (size_t)errsz, "%s", m);
}

/* ---- ★★ 図面は z=0 に居ること -----------------------------------------------------
 * ⚠ 平面の外にある 2D (oc-face3d) は **黙って潰さない**。DXF は OCS で置き場所を書けるが、
 *   occt の 2D は *曲面上の面* でもありうるので「平面なら OCS」では覆えない。
 *   ⇒ ここでは断って、project_flatten (z=0 へ落とす) か STEP を案内する。
 *   ★ 段 1 の on_z0_plane() と **同じ判定**にしてある (名乗りと図面の可否が離れないように)。 */
static bool ocd_on_z0(const TopoDS_Shape &s, double tol = 1e-9) {
	if ( s.IsNull() ) return true;
	Bnd_Box b;
	BRepBndLib::Add(s, b, Standard_False);
	if ( b.IsVoid() ) return true;
	b.SetGap(0.0);
	double mn[3], mx[3];
	b.Get(mn[0], mn[1], mn[2], mx[0], mx[1], mx[2]);
	return ( ::fabs(mn[2]) <= tol && ::fabs(mx[2]) <= tol );
}
static const char *OCD_OFFPLANE =
    "the 2D region is not on the z=0 plane, so it cannot be written as a drawing; "
    "drop it with project_flatten(...) first, or write it as STEP (which keeps the placement)";

/* ---- 角度: OCCT の媒介変数 → world XY の角度 (度) ---------------------------------
 * ★ 円 / 楕円の法線が -Z のとき、DXF (押し出し +Z) から見ると **回る向きが逆**になる。
 *   OCCT: P(t) = C + a cos t X + b sin t Y   (Y = N x X)
 *   DXF : P(s) = C + a cos s X + b sin s Y'  (Y' = Z x X)
 *   N = -Z なら Y' = -Y ⇒ **s = -t**。⇒ 始点と終点を入れ替えて符号を反転する。
 * ⚠ ここを間違えると「同じ円弧の裏側」が書かれる。⇒ 検定は往復の bbox で見る。 */
static void ocd_param_range(const gp_Dir &n, double f, double l, double *s0, double *s1) {
	if ( n.Z() >= 0.0 ) { *s0 = f; *s1 = l; return; }
	*s0 = -l; *s1 = -f;
}
static double ocd_norm2pi(double a) {
	while ( a < 0.0 )        a += OCD_2PI;
	while ( a >= OCD_2PI )   a -= OCD_2PI;
	return a;
}

/* ---- 折れ線への退避 (上の 4 種のどれでもない曲線) -----------------------------------
 * ⚠ **ここに落ちたことは黙らない** — 呼び手が数えて、検定が「落ちていない」ことを見る。
 *   たわみは図面の大きさに対する相対値。線分・円・楕円・B-spline は *厳密*に書かれるので、
 *   この値が効くのは本当にこの退避路へ来た曲線だけ。 */
static int ocd_sample(const BRepAdaptor_Curve &c, double defl, std::vector<gp_Pnt> &out) {
	out.clear();
	GCPnts_QuasiUniformDeflection d(const_cast<BRepAdaptor_Curve&>(c), defl);
	if ( ! d.IsDone() || d.NbPoints() < 2 ) return 0;
	for ( int i = 1 ; i <= d.NbPoints() ; ++i ) out.push_back(d.Value(i));
	return 1;
}

/* =====================================================================================
 *   DXF 書き手
 * ===================================================================================*/
bool
oc_write_dxf(const TopoDS_Shape &s, const char *path, const char *unit, char *err, int errsz)
{
	if ( s.IsNull() ) { ocd_err(err, errsz, "the drawing is empty"); return false; }
	if ( ! ocd_on_z0(s) ) { ocd_err(err, errsz, OCD_OFFPLANE); return false; }

	TopTools_IndexedMapOfShape em;
	TopExp::MapShapes(s, TopAbs_EDGE, em);
	if ( em.Extent() == 0 ) { ocd_err(err, errsz, "the drawing has no edge to write"); return false; }

	/* 退避路のたわみ = 図面の対角の 1e-4 (下限つき)。 */
	double defl = 1e-6;
	{
		Bnd_Box b; BRepBndLib::Add(s, b, Standard_False);
		if ( ! b.IsVoid() ) {
			double mn[3], mx[3]; b.SetGap(0.0); b.Get(mn[0],mn[1],mn[2],mx[0],mx[1],mx[2]);
			const double dx = mx[0]-mn[0], dy = mx[1]-mn[1];
			const double diag = ::sqrt(dx*dx + dy*dy);
			if ( diag > 0.0 ) defl = diag * 1e-4;
		}
	}

	FILE *f = ::fopen(path, "wb");
	if ( f == 0 ) { ocd_err(err, errsz, "could not open the file for writing"); return false; }

	const int iu = ocd_insunits(unit);
	if ( iu != 0 )
		::fprintf(f, "0\nSECTION\n2\nHEADER\n9\n$INSUNITS\n70\n%d\n0\nENDSEC\n", iu);
	::fprintf(f, "0\nSECTION\n2\nENTITIES\n");

	for ( int i = 1 ; i <= em.Extent() ; ++i ) {
		const TopoDS_Edge e = TopoDS::Edge(em(i));
		BRepAdaptor_Curve c(e);
		const double f0 = c.FirstParameter(), l0 = c.LastParameter();
		try {
			switch ( c.GetType() ) {
			case GeomAbs_Line: {
				const gp_Pnt a = c.Value(f0), b = c.Value(l0);
				::fprintf(f, "0\nLINE\n8\n0\n10\n" OCD_FMT "\n20\n" OCD_FMT "\n30\n0\n"
				             "11\n" OCD_FMT "\n21\n" OCD_FMT "\n31\n0\n",
				         a.X(), a.Y(), b.X(), b.Y());
				continue;
			}
			case GeomAbs_Circle: {
				const gp_Circ k = c.Circle();
				const gp_Pnt  o = k.Location();
				if ( ::fabs((l0 - f0) - OCD_2PI) < 1e-9 ) {   /* 全円 */
					::fprintf(f, "0\nCIRCLE\n8\n0\n10\n" OCD_FMT "\n20\n" OCD_FMT "\n30\n0\n"
					             "40\n" OCD_FMT "\n", o.X(), o.Y(), k.Radius());
					continue;
				}
				/* 円弧: DXF の 50/51 は **OCS の +X から反時計回りの角度 (度)**。
				 *   媒介変数は円自身の X 軸から測るので、その X 軸の world 角度を足す。 */
				double s0, s1;
				ocd_param_range(k.Axis().Direction(), f0, l0, &s0, &s1);
				const gp_Dir xd = k.XAxis().Direction();
				const double base = ::atan2(xd.Y(), xd.X());
				::fprintf(f, "0\nARC\n8\n0\n10\n" OCD_FMT "\n20\n" OCD_FMT "\n30\n0\n"
				             "40\n" OCD_FMT "\n50\n" OCD_FMT "\n51\n" OCD_FMT "\n",
				         o.X(), o.Y(), k.Radius(),
				         ocd_norm2pi(base + s0) * 180.0 / M_PI,
				         ocd_norm2pi(base + s1) * 180.0 / M_PI);
				continue;
			}
			case GeomAbs_Ellipse: {
				const gp_Elips el = c.Ellipse();
				const gp_Pnt   o  = el.Location();
				const gp_Dir   xd = el.XAxis().Direction();
				const double   a  = el.MajorRadius(), b = el.MinorRadius();
				double s0, s1;
				ocd_param_range(el.Axis().Direction(), f0, l0, &s0, &s1);
				if ( ::fabs((l0 - f0) - OCD_2PI) < 1e-9 ) { s0 = 0.0; s1 = OCD_2PI; }
				/* 11/21/31 = **中心からの相対**で長軸の端点。40 = 短軸/長軸。41/42 = 媒介変数。 */
				::fprintf(f, "0\nELLIPSE\n8\n0\n10\n" OCD_FMT "\n20\n" OCD_FMT "\n30\n0\n"
				             "11\n" OCD_FMT "\n21\n" OCD_FMT "\n31\n0\n"
				             "40\n" OCD_FMT "\n41\n" OCD_FMT "\n42\n" OCD_FMT "\n",
				         o.X(), o.Y(), a * xd.X(), a * xd.Y(),
				         ( a != 0.0 ) ? b / a : 1.0, s0, s1);
				continue;
			}
			case GeomAbs_BSplineCurve: {
				Handle(Geom_BSplineCurve) bs = c.BSpline();
				if ( ! bs.IsNull() ) {
					/* ⚠ 媒介変数が曲線自身の範囲より狭ければ **切り出してから**書く
					 *   (DXF の SPLINE に trim の口が無い)。⇒ 複製に Segment を当てる。 */
					if ( f0 > bs->FirstParameter() + 1e-12 || l0 < bs->LastParameter() - 1e-12 ) {
						Handle(Geom_BSplineCurve) cut =
						    Handle(Geom_BSplineCurve)::DownCast(bs->Copy());
						cut->Segment(f0, l0);
						bs = cut;
					}
					const int deg = bs->Degree(), np = bs->NbPoles(), nk = bs->NbKnots();
					int nkTotal = 0;
					for ( int k = 1 ; k <= nk ; ++k ) nkTotal += bs->Multiplicity(k);
					int flags = 8;                       /* 8 = planar */
					if ( bs->IsClosed() )   flags |= 1;  /* 1 = closed */
					if ( bs->IsPeriodic() ) flags |= 2;  /* 2 = periodic */
					if ( bs->IsRational() ) flags |= 4;  /* 4 = rational */
					::fprintf(f, "0\nSPLINE\n8\n0\n70\n%d\n71\n%d\n72\n%d\n73\n%d\n74\n0\n",
					         flags, deg, nkTotal, np);
					for ( int k = 1 ; k <= nk ; ++k ) {
						const double kv = bs->Knot(k);
						for ( int m = 0 ; m < bs->Multiplicity(k) ; ++m )
							::fprintf(f, "40\n" OCD_FMT "\n", kv);
					}
					if ( bs->IsRational() )
						for ( int k = 1 ; k <= np ; ++k )
							::fprintf(f, "41\n" OCD_FMT "\n", bs->Weight(k));
					for ( int k = 1 ; k <= np ; ++k ) {
						const gp_Pnt p = bs->Pole(k);
						::fprintf(f, "10\n" OCD_FMT "\n20\n" OCD_FMT "\n30\n0\n", p.X(), p.Y());
					}
					continue;
				}
				break;   /* Handle が取れなければ下の退避路へ */
			}
			default: break;
			}
		} catch ( const Standard_Failure& ) {
			/* 素性が取れなければ退避路へ。⚠ 黙って落とすのではなく折れ線として残す。 */
		}
		/* 退避路: 折れ線 (LWPOLYLINE)。 */
		std::vector<gp_Pnt> pts;
		if ( ! ocd_sample(c, defl, pts) ) continue;
		::fprintf(f, "0\nLWPOLYLINE\n8\n0\n90\n%d\n70\n0\n", (int)pts.size());
		for ( std::size_t k = 0 ; k < pts.size() ; ++k )
			::fprintf(f, "10\n" OCD_FMT "\n20\n" OCD_FMT "\n", pts[k].X(), pts[k].Y());
	}
	::fprintf(f, "0\nENDSEC\n0\nEOF\n");
	::fclose(f);
	return true;
}

/* =====================================================================================
 *   SVG 書き手
 *   ⚠ SVG の語彙には **B-spline が無い** (path は 2/3 次ベジエと円弧だけ)。
 *     ⇒ 線分・円弧・楕円弧は厳密に書けるが、B-spline は **折れ線に落ちる**。
 *     これは実装の手抜きではなく形式の限界 — 曲線を保ったまま外へ出したいなら DXF か STEP。
 *   ⚠ y 軸は **下向きのまま** (cgal の write_svg と同じ)。二重に決めない。
 * ===================================================================================*/
bool
oc_write_svg(const TopoDS_Shape &s, const char *path, const char *unit, char *err, int errsz)
{
	if ( s.IsNull() ) { ocd_err(err, errsz, "the drawing is empty"); return false; }
	if ( ! ocd_on_z0(s) ) { ocd_err(err, errsz, OCD_OFFPLANE); return false; }
	TopTools_IndexedMapOfShape em;
	TopExp::MapShapes(s, TopAbs_EDGE, em);
	if ( em.Extent() == 0 ) { ocd_err(err, errsz, "the drawing has no edge to write"); return false; }

	double mn[3] = {0,0,0}, mx[3] = {0,0,0}, defl = 1e-6;
	{
		Bnd_Box b; BRepBndLib::Add(s, b, Standard_False);
		if ( b.IsVoid() ) { ocd_err(err, errsz, "the drawing has no extent"); return false; }
		b.SetGap(0.0); b.Get(mn[0],mn[1],mn[2],mx[0],mx[1],mx[2]);
		const double dx = mx[0]-mn[0], dy = mx[1]-mn[1];
		const double diag = ::sqrt(dx*dx + dy*dy);
		if ( diag > 0.0 ) defl = diag * 1e-4;
	}
	FILE *f = ::fopen(path, "wb");
	if ( f == 0 ) { ocd_err(err, errsz, "could not open the file for writing"); return false; }

	const char *su = ocd_svg_unit(unit);
	::fprintf(f, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<svg xmlns=\"http://www.w3.org/2000/svg\" ");
	if ( su[0] != '\0' )
		::fprintf(f, "width=\"" OCD_FMT "%s\" height=\"" OCD_FMT "%s\" ",
		         mx[0]-mn[0], su, mx[1]-mn[1], su);
	::fprintf(f, "viewBox=\"" OCD_FMT " " OCD_FMT " " OCD_FMT " " OCD_FMT "\">\n",
	         mn[0], mn[1], mx[0]-mn[0], mx[1]-mn[1]);

	for ( int i = 1 ; i <= em.Extent() ; ++i ) {
		BRepAdaptor_Curve c(TopoDS::Edge(em(i)));
		const double f0 = c.FirstParameter(), l0 = c.LastParameter();
		const GeomAbs_CurveType ty = c.GetType();
		try {
			if ( ty == GeomAbs_Line ) {
				const gp_Pnt a = c.Value(f0), b = c.Value(l0);
				::fprintf(f, "  <path fill=\"none\" stroke=\"#000000\" stroke-width=\"" OCD_FMT "\" "
				             "d=\"M " OCD_FMT " " OCD_FMT " L " OCD_FMT " " OCD_FMT "\"/>\n",
				         defl * 5.0, a.X(), a.Y(), b.X(), b.Y());
				continue;
			}
			if ( ty == GeomAbs_Circle || ty == GeomAbs_Ellipse ) {
				/* ★ 円弧は SVG の A で **厳密**に書ける。全周は 1 つの A では書けないので
				 *   (始点と終点が同じ) 半周ずつ 2 つに割る。 */
				double rx, ry, rot;
				gp_Dir nrm;
				if ( ty == GeomAbs_Circle ) {
					const gp_Circ k = c.Circle();
					rx = ry = k.Radius(); rot = 0.0; nrm = k.Axis().Direction();
				} else {
					const gp_Elips el = c.Ellipse();
					rx = el.MajorRadius(); ry = el.MinorRadius();
					const gp_Dir xd = el.XAxis().Direction();
					rot = ::atan2(xd.Y(), xd.X()) * 180.0 / M_PI;
					nrm = el.Axis().Direction();
				}
				const int sweep = ( nrm.Z() >= 0.0 ) ? 1 : 0;   /* SVG の y は下向き = 数学の反時計は 1 */
				const double span = l0 - f0;
				const gp_Pnt p0 = c.Value(f0);
				::fprintf(f, "  <path fill=\"none\" stroke=\"#000000\" stroke-width=\"" OCD_FMT "\" "
				             "d=\"M " OCD_FMT " " OCD_FMT, defl * 5.0, p0.X(), p0.Y());
				const int nseg = ( span >= M_PI ) ? (int)::ceil(span / M_PI) : 1;
				for ( int k = 1 ; k <= nseg ; ++k ) {
					const double t = f0 + span * (double)k / (double)nseg;
					const gp_Pnt p = c.Value(t);
					::fprintf(f, " A " OCD_FMT " " OCD_FMT " " OCD_FMT " 0 %d " OCD_FMT " " OCD_FMT,
					         rx, ry, rot, sweep, p.X(), p.Y());
				}
				::fprintf(f, "\"/>\n");
				continue;
			}
		} catch ( const Standard_Failure& ) { /* 下の折れ線へ */ }
		std::vector<gp_Pnt> pts;
		if ( ! ocd_sample(c, defl, pts) ) continue;
		::fprintf(f, "  <polyline fill=\"none\" stroke=\"#000000\" stroke-width=\"" OCD_FMT "\" points=\"",
		         defl * 5.0);
		for ( std::size_t k = 0 ; k < pts.size() ; ++k )
			::fprintf(f, "%s" OCD_FMT "," OCD_FMT, (k ? " " : ""), pts[k].X(), pts[k].Y());
		::fprintf(f, "\"/>\n");
	}
	::fprintf(f, "</svg>\n");
	::fclose(f);
	return true;
}

/* =====================================================================================
 *   DXF 読み手 — ★★ 語彙は上の書き手と **1:1**
 *
 *   LINE / CIRCLE / ARC / ELLIPSE / SPLINE / LWPOLYLINE / POLYLINE
 *
 *   ⚠⚠ 「書けるが読めない」を作らないこと自体が段 3 の受け入れ条件。
 *     ⇒ 円を書いて読み戻すと **円のまま** (折れ線にならない) を検定で固定する。
 *   ⚠ $INSUNITS は **読まない** (cgal の読み手も読んでいない)。単位で座標を換算する約束を
 *     片方のカーネルだけが持つと、同じ .dxf が読み手次第で別の大きさになる。
 * ===================================================================================*/

/* group code / value のペア列。DXF (ASCII) は 1 行おきに code と値が並ぶ。 */
struct ocd_pair { int code; std::string val; };

static bool ocd_slurp_pairs(const char *path, std::vector<ocd_pair> &out)
{
	FILE *f = ::fopen(path, "rb");
	if ( f == 0 ) return false;
	std::string s;
	char rbuf[65536]; size_t rn;
	while ( (rn = ::fread(rbuf, 1, sizeof rbuf, f)) > 0 ) s.append(rbuf, rn);
	::fclose(f);
	std::vector<std::string> lines;
	std::size_t i = 0;
	while ( i < s.size() ) {
		std::size_t j = s.find('\n', i);
		if ( j == std::string::npos ) j = s.size();
		std::string ln = s.substr(i, j - i);
		while ( ! ln.empty() && (ln[ln.size()-1]=='\r' || ln[ln.size()-1]==' ' || ln[ln.size()-1]=='\t') )
			ln.erase(ln.size()-1);
		std::size_t b = 0; while ( b < ln.size() && (ln[b]==' '||ln[b]=='\t') ) ++b;
		lines.push_back(ln.substr(b));
		i = j + 1;
	}
	for ( std::size_t k = 0 ; k + 1 < lines.size() ; k += 2 ) {
		ocd_pair p;
		p.code = ::atoi(lines[k].c_str());
		p.val  = lines[k+1];
		out.push_back(p);
	}
	return true;
}

/* 1 エンティティぶんの生データ。 */
struct ocd_ent {
	std::string          name;
	std::vector<double>  v10, v20, v11, v21, v40, v41, v42, v50, v51;
	std::vector<int>     v70, v71, v72, v73, v90;
};

static double ocd_get(const std::vector<double> &v, std::size_t i, double dflt = 0.0) {
	return ( i < v.size() ) ? v[i] : dflt;
}

/* エンティティ 1 つ → 稜 (0 本以上) を builder へ。 */
static void ocd_ent_to_edges(const ocd_ent &e, BRep_Builder &bb, TopoDS_Compound &comp, int *nmade)
{
	try {
		if ( e.name == "LINE" ) {
			const gp_Pnt a(ocd_get(e.v10,0), ocd_get(e.v20,0), 0.0);
			const gp_Pnt b(ocd_get(e.v11,0), ocd_get(e.v21,0), 0.0);
			if ( a.Distance(b) <= 0.0 ) return;
			BRepBuilderAPI_MakeEdge me(a, b);
			if ( me.IsDone() ) { bb.Add(comp, me.Edge()); ++*nmade; }
			return;
		}
		if ( e.name == "CIRCLE" || e.name == "ARC" ) {
			const double r = ocd_get(e.v40,0);
			if ( r <= 0.0 ) return;
			const gp_Circ k(gp_Ax2(gp_Pnt(ocd_get(e.v10,0), ocd_get(e.v20,0), 0.0),
			                       gp_Dir(0,0,1), gp_Dir(1,0,0)), r);
			if ( e.name == "CIRCLE" ) {
				BRepBuilderAPI_MakeEdge me(k);
				if ( me.IsDone() ) { bb.Add(comp, me.Edge()); ++*nmade; }
				return;
			}
			double a0 = ocd_get(e.v50,0) * M_PI / 180.0;
			double a1 = ocd_get(e.v51,0) * M_PI / 180.0;
			if ( a1 <= a0 ) a1 += OCD_2PI;   /* DXF の円弧は 50 → 51 へ反時計回り */
			BRepBuilderAPI_MakeEdge me(k, a0, a1);
			if ( me.IsDone() ) { bb.Add(comp, me.Edge()); ++*nmade; }
			return;
		}
		if ( e.name == "ELLIPSE" ) {
			const double mx = ocd_get(e.v11,0), my = ocd_get(e.v21,0);
			const double a  = ::sqrt(mx*mx + my*my);
			const double ratio = ocd_get(e.v40,0,1.0);
			if ( a <= 0.0 || ratio <= 0.0 ) return;
			const gp_Elips el(gp_Ax2(gp_Pnt(ocd_get(e.v10,0), ocd_get(e.v20,0), 0.0),
			                         gp_Dir(0,0,1), gp_Dir(mx, my, 0.0)), a, a * ratio);
			double t0 = ocd_get(e.v41,0,0.0), t1 = ocd_get(e.v42,0,OCD_2PI);
			if ( ::fabs((t1 - t0) - OCD_2PI) < 1e-9 ) {
				BRepBuilderAPI_MakeEdge me(el);
				if ( me.IsDone() ) { bb.Add(comp, me.Edge()); ++*nmade; }
				return;
			}
			if ( t1 <= t0 ) t1 += OCD_2PI;
			BRepBuilderAPI_MakeEdge me(el, t0, t1);
			if ( me.IsDone() ) { bb.Add(comp, me.Edge()); ++*nmade; }
			return;
		}
		if ( e.name == "SPLINE" ) {
			const int deg = e.v71.empty() ? 3 : e.v71[0];
			const int np  = (int)e.v10.size();
			if ( np < 2 || deg < 1 || (int)e.v40.size() < np + deg + 1 ) return;
			/* ⚠ DXF は knot を **展開して**並べる (重複込み)。OCCT は (値, 多重度) を要る
			 *   ⇒ 畳み直す。片方だけ直すと書いたものが読めなくなる場所その 1。 */
			std::vector<double> kv;
			std::vector<int>    km;
			for ( std::size_t i = 0 ; i < e.v40.size() ; ++i ) {
				if ( ! kv.empty() && ::fabs(e.v40[i] - kv.back()) <= 1e-12 ) { ++km.back(); continue; }
				kv.push_back(e.v40[i]); km.push_back(1);
			}
			TColgp_Array1OfPnt      poles(1, np);
			for ( int i = 0 ; i < np ; ++i )
				poles.SetValue(i+1, gp_Pnt(e.v10[i], ocd_get(e.v20,(std::size_t)i), 0.0));
			TColStd_Array1OfReal    knots(1, (int)kv.size());
			TColStd_Array1OfInteger mults(1, (int)km.size());
			for ( std::size_t i = 0 ; i < kv.size() ; ++i ) {
				knots.SetValue((int)i+1, kv[i]);
				mults.SetValue((int)i+1, km[i]);
			}
			Handle(Geom_BSplineCurve) bs;
			if ( (int)e.v41.size() == np ) {   /* 有理 (重みつき) */
				TColStd_Array1OfReal w(1, np);
				for ( int i = 0 ; i < np ; ++i ) w.SetValue(i+1, e.v41[i]);
				bs = new Geom_BSplineCurve(poles, w, knots, mults, deg);
			} else {
				bs = new Geom_BSplineCurve(poles, knots, mults, deg);
			}
			BRepBuilderAPI_MakeEdge me(bs);
			if ( me.IsDone() ) { bb.Add(comp, me.Edge()); ++*nmade; }
			return;
		}
		if ( e.name == "LWPOLYLINE" || e.name == "POLYLINE" ) {
			const int closed = ( ! e.v70.empty() && (e.v70[0] & 1) ) ? 1 : 0;
			const std::size_t n = e.v10.size() < e.v20.size() ? e.v10.size() : e.v20.size();
			if ( n < 2 ) return;
			for ( std::size_t i = 0 ; i + 1 < n ; ++i ) {
				const gp_Pnt a(e.v10[i], e.v20[i], 0.0), b(e.v10[i+1], e.v20[i+1], 0.0);
				if ( a.Distance(b) <= 0.0 ) continue;
				BRepBuilderAPI_MakeEdge me(a, b);
				if ( me.IsDone() ) { bb.Add(comp, me.Edge()); ++*nmade; }
			}
			if ( closed ) {
				const gp_Pnt a(e.v10[n-1], e.v20[n-1], 0.0), b(e.v10[0], e.v20[0], 0.0);
				if ( a.Distance(b) > 0.0 ) {
					BRepBuilderAPI_MakeEdge me(a, b);
					if ( me.IsDone() ) { bb.Add(comp, me.Edge()); ++*nmade; }
				}
			}
			return;
		}
	} catch ( const Standard_Failure& ) {
		/* ⚠ 1 つのエンティティが壊れていても **黙って全体を捨てない**。作れたものは残す。 */
	}
}

bool
oc_read_dxf(const char *path, TopoDS_Shape &out, char *err, int errsz)
{
	std::vector<ocd_pair> ps;
	if ( ! ocd_slurp_pairs(path, ps) ) { ocd_err(err, errsz, "could not open the file for reading"); return false; }

	BRep_Builder    bb;
	TopoDS_Compound comp;
	bb.MakeCompound(comp);
	int nmade = 0;

	ocd_ent cur;
	int inEnt = 0, sawOcs = 0;
	for ( std::size_t i = 0 ; i < ps.size() ; ++i ) {
		const int code = ps[i].code;
		const std::string &v = ps[i].val;
		if ( code == 0 ) {
			/* ★★ 旧 POLYLINE は頂点を **後続の VERTEX 実体**として並べる。
			 *   ⇒ VERTEX / SEQEND では *いま組み立て中の POLYLINE を畳まない*。
			 *   ⚠ ここを素直に「0 が来たら実体の切れ目」と書くと、POLYLINE が
			 *     **頂点 0 個で確定**して黙って消える (LWPOLYLINE しか読めない読み手になる)。 */
			if ( inEnt && cur.name == "POLYLINE" && ( v == "VERTEX" || v == "SEQEND" ) )
				continue;
			if ( inEnt ) ocd_ent_to_edges(cur, bb, comp, &nmade);
			cur = ocd_ent();
			cur.name = v;
			inEnt = ( v == "LINE" || v == "CIRCLE" || v == "ARC" || v == "ELLIPSE" ||
			          v == "SPLINE" || v == "LWPOLYLINE" || v == "POLYLINE" );
			continue;
		}
		if ( ! inEnt ) continue;
		/* ★★ OCS (置き場所) を持つ .dxf は **読まずに断る**。
		 *   occt 側の図面は z=0 の 2D (oc-cross2d) なので、OCS を無視して読むと
		 *   *置き場所が黙って落ちて z=0 に戻る* — #3533 で cgal が実際に踏んだ形。
		 *   ⇒ 置かれた .dxf は cgal の import (cg-face3d) が読む。 */
		if ( code == 210 || code == 220 ) {
			if ( ::fabs(::strtod(v.c_str(), 0)) > 1e-12 ) sawOcs = 1;
		} else if ( code == 230 ) {
			if ( ::fabs(::strtod(v.c_str(), 0) - 1.0) > 1e-12 ) sawOcs = 1;
		} else if ( code == 38 ) {
			if ( ::fabs(::strtod(v.c_str(), 0)) > 1e-12 ) sawOcs = 1;
		}
		switch ( code ) {
		case 10: cur.v10.push_back(::strtod(v.c_str(), 0)); break;
		case 20: cur.v20.push_back(::strtod(v.c_str(), 0)); break;
		case 11: cur.v11.push_back(::strtod(v.c_str(), 0)); break;
		case 21: cur.v21.push_back(::strtod(v.c_str(), 0)); break;
		case 40: cur.v40.push_back(::strtod(v.c_str(), 0)); break;
		case 41: cur.v41.push_back(::strtod(v.c_str(), 0)); break;
		case 42: cur.v42.push_back(::strtod(v.c_str(), 0)); break;
		case 50: cur.v50.push_back(::strtod(v.c_str(), 0)); break;
		case 51: cur.v51.push_back(::strtod(v.c_str(), 0)); break;
		case 70: cur.v70.push_back(::atoi(v.c_str())); break;
		case 71: cur.v71.push_back(::atoi(v.c_str())); break;
		case 72: cur.v72.push_back(::atoi(v.c_str())); break;
		case 73: cur.v73.push_back(::atoi(v.c_str())); break;
		case 90: cur.v90.push_back(::atoi(v.c_str())); break;
		default: break;
		}
	}
	if ( inEnt ) ocd_ent_to_edges(cur, bb, comp, &nmade);

	if ( sawOcs ) {
		ocd_err(err, errsz,
		    "this DXF places its entities on another plane (OCS 210/220/230 or elevation 38); "
		    "occt reads drawings on the z=0 plane only — import it with the cgal module, "
		    "which keeps the placement as a cg-face3d");
		return false;
	}
	if ( nmade == 0 ) {
		ocd_err(err, errsz, "no drawing entity could be read (occt reads LINE / CIRCLE / ARC / "
		                    "ELLIPSE / SPLINE / LWPOLYLINE / POLYLINE)");
		return false;
	}
	out = comp;
	return true;
}
