/*
 * cgMesh2D — 2D 多角形領域(EPECK, 穴あき多角形の集合)の多態メソッド実装(Step C)。
 * ブール演算(Boolean_set_operations_2)・2D アフィン・codec(repr_type=32 "PLY2")。
 * extrude/revolve で 3D(cgMesh3D)へ持ち上がる(変換本体は cgaExtrude 等)。
 */
#include	"cg/c++/cgMesh.h"
#include	"cg/c++/cgaMeshCodec.h"   /* put_u32/put_coord/get_u32/get_coord(Sink/Source 越し)を再利用 */
#include	"ts2/c++/stdString.h"

#include	<CGAL/Polygon_set_2.h>
#include	<CGAL/Boolean_set_operations_2.h>
#include	<CGAL/Aff_transformation_2.h>
#include	<CGAL/create_offset_polygons_from_polygon_with_holes_2.h>   /* offset(straight skeleton) */
#include	<CGAL/Polygon_repair/repair.h>   /* repair(even-odd) → Multipolygon_with_holes_2 */
#include	<CGAL/number_utils.h>   /* CGAL::to_double */
#include	<memory>
#include	<cmath>

#include	<stdio.h>
#include	<string.h>
#include	<float.h>

typedef cgMesh::K			K2;
typedef cgMesh2D::Polygon_2		Poly2;
typedef cgMesh2D::Pwh_2			Pwh2;
typedef CGAL::Polygon_set_2<K2>		PSet2;

/* ---- get_str ---- */
sPtr<stdString>
cgMesh2D::get_str()
{
	return thNEW(stdString,("<cgMesh2D>"));
}

/* ---- codec: 1 ring を [u32 npts][pt(2 有理数)...] で。Pwh = 外周 + 穴数 + 穴 ring 群 ---- */
static void put_ring(cgChunkSink& s, const Poly2& ring) {
	cgaMeshCodec::put_u32(s, (uint32_t)ring.size());
	for ( Poly2::Vertex_const_iterator it = ring.vertices_begin() ; it != ring.vertices_end() ; ++it ) {
		cgaMeshCodec::put_coord(s, it->x());
		cgaMeshCodec::put_coord(s, it->y());
	}
}
static Poly2 get_ring(cgChunkSource& s) {
	uint32_t n = cgaMeshCodec::get_u32(s);
	Poly2 ring;
	for ( uint32_t i = 0 ; i < n ; ++i ) {
		K2::FT x = cgaMeshCodec::get_coord<cgChunkSource,K2::FT>(s);
		K2::FT y = cgaMeshCodec::get_coord<cgChunkSource,K2::FT>(s);
		ring.push_back(K2::Point_2(x, y));
	}
	return ring;
}

void
cgMesh2D::encode(cgChunkSink& sink)
{
	cgaMeshCodec::put_u32(sink, (uint32_t)regions_.size());
	for ( std::size_t i = 0 ; i < regions_.size() ; ++i ) {
		const Pwh2& pwh = regions_[i];
		put_ring(sink, pwh.outer_boundary());
		cgaMeshCodec::put_u32(sink, (uint32_t)pwh.number_of_holes());
		for ( Pwh2::Hole_const_iterator h = pwh.holes_begin() ; h != pwh.holes_end() ; ++h )
			put_ring(sink, *h);
	}
	/* ガイド層(開ポリライン群)。regions の後ろに追記。後方互換: 旧 blob(この節が無い)は
	 * decode 側が src.more()==false で読み飛ばす。 */
	cgaMeshCodec::put_u32(sink, (uint32_t)guides_.size());
	for ( std::size_t i = 0 ; i < guides_.size() ; ++i ) {
		cgaMeshCodec::put_u32(sink, (uint32_t)guides_[i].size());
		for ( std::size_t j = 0 ; j < guides_[i].size() ; ++j ) {
			cgaMeshCodec::put_coord(sink, guides_[i][j].x());
			cgaMeshCodec::put_coord(sink, guides_[i][j].y());
		}
	}
}

void
cgMesh2D::decode(cgChunkSource& src)
{
	if ( mfc2Input_ ) { decode_mfc2(src); return; }   /* ★ Manifold 2D cache → 無損失昇格(#3404) */
	regions_.clear();
	guides_.clear();
	uint32_t npwh = cgaMeshCodec::get_u32(src);
	for ( uint32_t i = 0 ; i < npwh ; ++i ) {
		Poly2 outer = get_ring(src);
		uint32_t nh = cgaMeshCodec::get_u32(src);
		std::vector<Poly2> holes;
		for ( uint32_t j = 0 ; j < nh ; ++j )
			holes.push_back(get_ring(src));
		regions_.push_back(Pwh2(outer, holes.begin(), holes.end()));
	}
	/* ガイド層(後方互換: 旧 blob には無いので、データが残っている時だけ読む)。 */
	if ( src.more() ) {
		uint32_t ng = cgaMeshCodec::get_u32(src);
		for ( uint32_t i = 0 ; i < ng ; ++i ) {
			uint32_t np = cgaMeshCodec::get_u32(src);
			Guide g;
			for ( uint32_t j = 0 ; j < np ; ++j ) {
				K2::FT x = cgaMeshCodec::get_coord<cgChunkSource,K2::FT>(src);
				K2::FT y = cgaMeshCodec::get_coord<cgChunkSource,K2::FT>(src);
				g.push_back(K2::Point_2(x, y));
			}
			guides_.push_back(g);
		}
	}
}

/* ★ MFC2(Manifold CrossSection)→ cgMesh2D 無損失昇格(#3404)。
 *   framing(mfCross::encode と一致・全 LE): [u32 nrings] リング×([u32 npts] 点×(f64 x,y))。
 *   Manifold ToPolygons は外周 CCW・穴 CW(Clipper 規約)= CGAL Pwh と同規約。面積符号で外周/穴に
 *   分け、穴は代表頂点の包含判定で属する外周へ紐付ける。double→EPECK は厳密=損失なし。 */
static uint32_t rd_u32_raw(cgChunkSource& s) {
	uint8_t b[4]; s.pull(b, 4);
	return (uint32_t)b[0] | ((uint32_t)b[1]<<8) | ((uint32_t)b[2]<<16) | ((uint32_t)b[3]<<24);
}
static double rd_f64_raw(cgChunkSource& s) {
	uint8_t b[8]; s.pull(b, 8); double d; ::memcpy(&d, b, 8); return d;
}
void
cgMesh2D::decode_mfc2(cgChunkSource& src)
{
	regions_.clear();
	guides_.clear();
	uint32_t nr = rd_u32_raw(src);
	std::vector<Poly2> outers, holes;
	for ( uint32_t r = 0 ; r < nr ; ++r ) {
		uint32_t np = rd_u32_raw(src);
		Poly2 ring;
		for ( uint32_t i = 0 ; i < np ; ++i ) {
			double x = rd_f64_raw(src), y = rd_f64_raw(src);
			ring.push_back(K2::Point_2(K2::FT(x), K2::FT(y)));
		}
		if ( ring.size() < 3 ) continue;
		if ( ring.is_clockwise_oriented() ) holes.push_back(ring);   /* CW = 穴 */
		else                                outers.push_back(ring);  /* CCW = 外周 */
	}
	std::vector<std::vector<Poly2> > outerHoles(outers.size());
	for ( size_t h = 0 ; h < holes.size() ; ++h ) {
		K2::Point_2 pt = *holes[h].vertices_begin();
		for ( size_t o = 0 ; o < outers.size() ; ++o ) {
			if ( outers[o].bounded_side(pt) == CGAL::ON_BOUNDED_SIDE ) {
				outerHoles[o].push_back(holes[h]);
				break;
			}
		}
	}
	for ( size_t o = 0 ; o < outers.size() ; ++o )
		regions_.push_back(Pwh2(outers[o], outerHoles[o].begin(), outerHoles[o].end()));
}

/* ---- ブール演算(Polygon_set_2)。b が cgMesh2D でなければ null=エラー ---- */
static void load_set(PSet2& s, const std::vector<Pwh2>& rs) {
	for ( std::size_t i = 0 ; i < rs.size() ; ++i )
		s.join(rs[i]);
}
static sPtr<cgMesh> extract(PSet2& s) {
	sPtr<cgMesh2D> out = thNEW(cgMesh2D,());
	std::vector<Pwh2> res;
	res.resize(s.number_of_polygons_with_holes());
	s.polygons_with_holes(res.begin());
	out->regions() = res;
	return out;
}
/* ブール結果にガイド層を引き継ぐ(ガイドはブール演算の対象外=両被演算子のガイドをそのまま残す)。 */
static void carry_guides(sPtr<cgMesh> out, const std::vector<cgMesh2D::Guide>& ga,
                                            const std::vector<cgMesh2D::Guide>& gb) {
	sPtr<cgMesh2D> o = sPtr<cgMesh2D>::d_cast(out);
	if ( ! o.is_notNull() ) return;
	o->guides() = ga;
	for ( std::size_t i = 0 ; i < gb.size() ; ++i ) o->guides().push_back(gb[i]);
}

sPtr<cgMesh>
cgMesh2D::op_union(sPtr<cgMesh> b)
{
	sPtr<cgMesh2D> mb = sPtr<cgMesh2D>::d_cast(b);
	if ( ! mb.is_notNull() ) return sPtr<cgMesh>();
	PSet2 sa, sb;
	load_set(sa, regions_);  load_set(sb, mb->regions_);
	sa.join(sb);
	sPtr<cgMesh> out = extract(sa);
	carry_guides(out, guides_, mb->guides_);
	return out;
}
sPtr<cgMesh>
cgMesh2D::op_intersection(sPtr<cgMesh> b)
{
	sPtr<cgMesh2D> mb = sPtr<cgMesh2D>::d_cast(b);
	if ( ! mb.is_notNull() ) return sPtr<cgMesh>();
	PSet2 sa, sb;
	load_set(sa, regions_);  load_set(sb, mb->regions_);
	sa.intersection(sb);
	sPtr<cgMesh> out = extract(sa);
	carry_guides(out, guides_, mb->guides_);
	return out;
}
sPtr<cgMesh>
cgMesh2D::op_difference(sPtr<cgMesh> b)
{
	sPtr<cgMesh2D> mb = sPtr<cgMesh2D>::d_cast(b);
	if ( ! mb.is_notNull() ) return sPtr<cgMesh>();
	PSet2 sa, sb;
	load_set(sa, regions_);  load_set(sb, mb->regions_);
	sa.difference(sb);
	sPtr<cgMesh> out = extract(sa);
	carry_guides(out, guides_, mb->guides_);
	return out;
}
/* ---- combine: 両領域の Pwh + ガイド層をそのまま集めるだけ(ブール演算しない・重なり許容)。
 * ブール前に重なり具合を viewer で確認する用途、および line(ガイド)を部品に重ねる用途(`a +++ b`)。 ---- */
sPtr<cgMesh>
cgMesh2D::op_combine(sPtr<cgMesh> b)
{
	sPtr<cgMesh2D> mb = sPtr<cgMesh2D>::d_cast(b);
	if ( ! mb.is_notNull() ) return sPtr<cgMesh>();
	sPtr<cgMesh2D> out = thNEW(cgMesh2D,());
	out->regions() = regions_;
	for ( std::size_t i = 0 ; i < mb->regions_.size() ; ++i )
		out->regions().push_back(mb->regions_[i]);
	out->guides() = guides_;
	for ( std::size_t i = 0 ; i < mb->guides_.size() ; ++i )
		out->guides().push_back(mb->guides_[i]);
	return out;
}

/* ---- オフセット(straight skeleton・面取り)。d>0 アウトセット / d<0 インセット / d=0 恒等。
 *      skeleton は内部 EPICK(sqrt 要)・出力 OfK=EPECK で有理座標に戻る。消滅/分裂は集合で表現。 ---- */
sPtr<cgMesh>
cgMesh2D::op_offset(double d, int /*subdiv*/)   /* 2D は subdiv 無視(skeleton は面取り) */
{
	sPtr<cgMesh2D> out = thNEW(cgMesh2D,());
	if ( d == 0.0 ) { out->regions_ = regions_; return out; }
	/* 入力の単純性チェック: 非単純(自己交差/重複頂点/零長エッジ)な多角形は straight skeleton が
	 * 破綻して空を返す。黙って空を返すと export が空ファイルになり原因が分からないので、null を返して
	 * 呼び出し側(cgaOffset)に明示エラーを出させる(ブール演算が失敗を明示するのと一貫)。 */
	for ( std::size_t r = 0 ; r < regions_.size() ; ++r ) {
		if ( ! regions_[r].outer_boundary().is_simple() )
			return thNULL;
		for ( auto h = regions_[r].holes_begin() ; h != regions_[r].holes_end() ; ++h )
			if ( ! h->is_simple() )
				return thNULL;
	}
	try {
		for ( std::size_t r = 0 ; r < regions_.size() ; ++r ) {
			std::vector<std::shared_ptr<Pwh2> > res;
			if ( d > 0.0 )
				res = CGAL::create_exterior_skeleton_and_offset_polygons_with_holes_2(K2::FT(d),  regions_[r], K2());
			else
				res = CGAL::create_interior_skeleton_and_offset_polygons_with_holes_2(K2::FT(-d), regions_[r], K2());
			for ( std::size_t i = 0 ; i < res.size() ; ++i )
				if ( res[i] )
					out->regions_.push_back(*res[i]);
		}
	} catch ( ... ) {
		return thNULL;   /* skeleton が退化形状で例外 → 明示エラー(クラッシュ回避) */
	}
	/* アウトセット(d>0)は非空・単純入力なら必ず成長する → 空なら失敗とみなす。
	 * インセット(d<0)の空は「収縮して消滅」= 正当な空集合なので残す(消滅/分裂は集合で表現)。 */
	if ( d > 0.0 && ! regions_.empty() && out->regions_.empty() )
		return thNULL;
	return out;
}

/* ---- ファイル書き出し: SVG / DXF(拡張子で判定)---- */
static const char* ext_of(const char* path) {
	const char* dot = ::strrchr(path, '.');
	return dot ? dot + 1 : "";
}
/* SVG が解釈できる単位だけ採用(それ以外は無単位=viewBox のみ)。 */
static const char* svg_unit(const char* u) {
	if ( u == 0 ) return "";
	if ( ::strcmp(u,"mm")==0 || ::strcmp(u,"cm")==0 || ::strcmp(u,"in")==0 ||
	     ::strcmp(u,"px")==0 || ::strcmp(u,"pt")==0 || ::strcmp(u,"pc")==0 )
		return u;
	return "";   /* m/ft 等 SVG 非対応 → 無視 */
}
/* DXF $INSUNITS コード(0=無単位/1=in/2=ft/4=mm/5=cm/6=m)。未知は 0。 */
static int dxf_insunits(const char* u) {
	if ( u == 0 ) return 0;
	if ( ::strcmp(u,"in")==0 ) return 1;
	if ( ::strcmp(u,"ft")==0 ) return 2;
	if ( ::strcmp(u,"mm")==0 ) return 4;
	if ( ::strcmp(u,"cm")==0 ) return 5;
	if ( ::strcmp(u,"m") ==0 ) return 6;
	return 0;
}

static void write_svg(FILE* f, const std::vector<Pwh2>& regs,
                      const std::vector<cgMesh2D::Guide>& guides, const char* unit) {
	double minx = DBL_MAX, miny = DBL_MAX, maxx = -DBL_MAX, maxy = -DBL_MAX;
	for ( std::size_t i = 0 ; i < regs.size() ; ++i ) {
		const Poly2& o = regs[i].outer_boundary();
		for ( Poly2::Vertex_const_iterator v = o.vertices_begin() ; v != o.vertices_end() ; ++v ) {
			double x = CGAL::to_double(v->x()), y = CGAL::to_double(v->y());
			if ( x < minx ) minx = x;  if ( x > maxx ) maxx = x;
			if ( y < miny ) miny = y;  if ( y > maxy ) maxy = y;
		}
	}
	for ( std::size_t i = 0 ; i < guides.size() ; ++i )   /* ガイドも bbox に含める */
		for ( std::size_t j = 0 ; j < guides[i].size() ; ++j ) {
			double x = CGAL::to_double(guides[i][j].x()), y = CGAL::to_double(guides[i][j].y());
			if ( x < minx ) minx = x;  if ( x > maxx ) maxx = x;
			if ( y < miny ) miny = y;  if ( y > maxy ) maxy = y;
		}
	if ( minx > maxx ) { minx = miny = 0; maxx = maxy = 1; }
	::fprintf(f, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
	const char* su = svg_unit(unit);
	::fprintf(f, "<svg xmlns=\"http://www.w3.org/2000/svg\" ");
	if ( su[0] )   /* 単位指定あり → 物理サイズ(width/height)を付与(viewBox は無単位のまま) */
		::fprintf(f, "width=\"%g%s\" height=\"%g%s\" ", maxx - minx, su, maxy - miny, su);
	::fprintf(f, "viewBox=\"%g %g %g %g\">\n", minx, miny, maxx - minx, maxy - miny);
	for ( std::size_t i = 0 ; i < regs.size() ; ++i ) {
		::fprintf(f, "  <path fill=\"#cccccc\" stroke=\"#000000\" stroke-width=\"0.01\" fill-rule=\"evenodd\" d=\"");
		/* 外周 + 穴を 1 つの path に(evenodd で穴が抜ける)。 */
		std::vector<const Poly2*> rs;
		rs.push_back(&regs[i].outer_boundary());
		for ( Pwh2::Hole_const_iterator h = regs[i].holes_begin() ; h != regs[i].holes_end() ; ++h )
			rs.push_back(&(*h));
		for ( std::size_t ri = 0 ; ri < rs.size() ; ++ri ) {
			const Poly2& r = *rs[ri];
			int k = 0;
			for ( Poly2::Vertex_const_iterator v = r.vertices_begin() ; v != r.vertices_end() ; ++v, ++k )
				::fprintf(f, "%s%g,%g ", (k == 0 ? "M" : "L"),
				          CGAL::to_double(v->x()), CGAL::to_double(v->y()));
			::fprintf(f, "Z ");
		}
		::fprintf(f, "\"/>\n");
	}
	/* ガイド層: 塗りなしストロークの polyline(寸法線/ガイド)。色は青系・細線で部品と区別。 */
	for ( std::size_t i = 0 ; i < guides.size() ; ++i ) {
		if ( guides[i].size() < 2 ) continue;
		::fprintf(f, "  <polyline fill=\"none\" stroke=\"#0066cc\" stroke-width=\"0.05\" points=\"");
		for ( std::size_t j = 0 ; j < guides[i].size() ; ++j )
			::fprintf(f, "%g,%g ", CGAL::to_double(guides[i][j].x()), CGAL::to_double(guides[i][j].y()));
		::fprintf(f, "\"/>\n");
	}
	::fprintf(f, "</svg>\n");
}
static void write_dxf(FILE* f, const std::vector<Pwh2>& regs,
                      const std::vector<cgMesh2D::Guide>& guides, const char* unit) {
	int iu = dxf_insunits(unit);
	if ( iu != 0 )   /* 単位指定あり → HEADER に $INSUNITS(ENTITIES より前) */
		::fprintf(f, "0\nSECTION\n2\nHEADER\n9\n$INSUNITS\n70\n%d\n0\nENDSEC\n", iu);
	::fprintf(f, "0\nSECTION\n2\nENTITIES\n");
	for ( std::size_t i = 0 ; i < regs.size() ; ++i ) {
		std::vector<const Poly2*> rs;
		rs.push_back(&regs[i].outer_boundary());
		for ( Pwh2::Hole_const_iterator h = regs[i].holes_begin() ; h != regs[i].holes_end() ; ++h )
			rs.push_back(&(*h));
		for ( std::size_t ri = 0 ; ri < rs.size() ; ++ri ) {
			const Poly2& r = *rs[ri];
			::fprintf(f, "0\nLWPOLYLINE\n8\n0\n90\n%d\n70\n1\n", (int)r.size());  /* 70=1 閉じ */
			for ( Poly2::Vertex_const_iterator v = r.vertices_begin() ; v != r.vertices_end() ; ++v )
				::fprintf(f, "10\n%g\n20\n%g\n", CGAL::to_double(v->x()), CGAL::to_double(v->y()));
		}
	}
	/* ガイド層: 開いた LWPOLYLINE(70=0)。レイヤ "GUIDES" に分けて寸法線/ガイドと分かるように。 */
	for ( std::size_t i = 0 ; i < guides.size() ; ++i ) {
		if ( guides[i].size() < 2 ) continue;
		::fprintf(f, "0\nLWPOLYLINE\n8\nGUIDES\n90\n%d\n70\n0\n", (int)guides[i].size());  /* 70=0 開 */
		for ( std::size_t j = 0 ; j < guides[i].size() ; ++j )
			::fprintf(f, "10\n%g\n20\n%g\n",
			          CGAL::to_double(guides[i][j].x()), CGAL::to_double(guides[i][j].y()));
	}
	::fprintf(f, "0\nENDSEC\n0\nEOF\n");
}
/* ---- 計測: 囲み面積(各 Pwh の 外周面積 − 穴面積 の総和)。Polygon::area は符号付き ---- */
double
cgMesh2D::op_area()
{
	double total = 0.0;
	for ( std::size_t i = 0 ; i < regions_.size() ; ++i ) {
		const Pwh_2& pwh = regions_[i];
		total += std::abs(CGAL::to_double(pwh.outer_boundary().area()));
		for ( Pwh_2::Hole_const_iterator h = pwh.holes_begin() ; h != pwh.holes_end() ; ++h )
			total -= std::abs(CGAL::to_double(h->area()));
	}
	return total;
}

/* ---- 2D に「体積」はない(呼び元 cgaVolume が dim==2 をエラーにする)---- */
double
cgMesh2D::op_volume()
{
	return 0.0;
}

/* 1 リング(Polygon_2)の境界長を double で足す(√は double)。 */
static double ring_perimeter(const cgMesh2D::Polygon_2& p)
{
	double total = 0.0;
	int n = (int)p.size();
	for ( int i = 0 ; i < n ; ++i ) {
		const cgMesh2D::K::Point_2& a = p[i];
		const cgMesh2D::K::Point_2& b = p[(i+1) % n];
		double dx = CGAL::to_double(b.x() - a.x());
		double dy = CGAL::to_double(b.y() - a.y());
		total += std::sqrt(dx*dx + dy*dy);
	}
	return total;
}

/* ---- 計測: 境界長(全 region の外周 + 穴の周長の総和)---- */
double
cgMesh2D::op_perimeter()
{
	double total = 0.0;
	for ( std::size_t i = 0 ; i < regions_.size() ; ++i ) {
		const Pwh_2& pwh = regions_[i];
		if ( pwh.outer_boundary().size() > 0 )
			total += ring_perimeter(pwh.outer_boundary());
		for ( Pwh_2::Hole_const_iterator h = pwh.holes_begin() ; h != pwh.holes_end() ; ++h )
			total += ring_perimeter(*h);
	}
	return total;
}

/* 1 リングの符号付き面積 a と 1 次モーメント (mx,my) を shoelace で加算(向きで符号が付く)。 */
static void ring_moment(const cgMesh2D::Polygon_2& p, double& a, double& mx, double& my)
{
	int n = (int)p.size();
	for ( int i = 0 ; i < n ; ++i ) {
		double xi = CGAL::to_double(p[i].x()),       yi = CGAL::to_double(p[i].y());
		double xj = CGAL::to_double(p[(i+1)%n].x()), yj = CGAL::to_double(p[(i+1)%n].y());
		double cross = xi*yj - xj*yi;
		a  += cross / 2.0;
		mx += (xi + xj) * cross / 6.0;
		my += (yi + yj) * cross / 6.0;
	}
}

/* ---- 計測: 面積重心。外周(CCW=正)と穴(CW=負)の符号付きモーメントを合算して加重平均 ---- */
int
cgMesh2D::op_centroid(double out[3])
{
	double a = 0, mx = 0, my = 0;
	for ( std::size_t i = 0 ; i < regions_.size() ; ++i ) {
		const Pwh_2& pwh = regions_[i];
		if ( pwh.outer_boundary().size() > 0 )
			ring_moment(pwh.outer_boundary(), a, mx, my);
		for ( Pwh_2::Hole_const_iterator h = pwh.holes_begin() ; h != pwh.holes_end() ; ++h )
			ring_moment(*h, a, mx, my);
	}
	if ( a != 0.0 ) { out[0] = mx/a; out[1] = my/a; }
	else            { out[0] = out[1] = 0.0; }
	out[2] = 0.0;
	return 2;
}

/* ---- 計測: 軸平行バウンディングボックス(全 region の外周頂点走査で min/max。穴は内側なので不要)。
 *      空集合は全 0。返り=次元 2。---- */
int
cgMesh2D::op_bbox(double mn[3], double mx[3])
{
	bool first = true;
	for ( std::size_t i = 0 ; i < regions_.size() ; ++i ) {
		const Poly2& o = regions_[i].outer_boundary();
		for ( Poly2::Vertex_const_iterator v = o.vertices_begin() ; v != o.vertices_end() ; ++v ) {
			double x = CGAL::to_double(v->x()), y = CGAL::to_double(v->y());
			if ( first ) {
				mn[0] = mx[0] = x; mn[1] = mx[1] = y;
				first = false;
			} else {
				if ( x < mn[0] ) mn[0] = x;  if ( x > mx[0] ) mx[0] = x;
				if ( y < mn[1] ) mn[1] = y;  if ( y > mx[1] ) mx[1] = y;
			}
		}
	}
	if ( first ) { mn[0]=mn[1]=mx[0]=mx[1] = 0.0; }
	mn[2] = mx[2] = 0.0;
	return 2;
}

/* ---- 検査: 全 region の外周/穴が単純(自己交差なし)---- */
int
cgMesh2D::op_valid()
{
	for ( std::size_t i = 0 ; i < regions_.size() ; ++i ) {
		const Pwh_2& pwh = regions_[i];
		if ( pwh.outer_boundary().size() > 0 && ! pwh.outer_boundary().is_simple() )
			return 0;
		for ( Pwh_2::Hole_const_iterator h = pwh.holes_begin() ; h != pwh.holes_end() ; ++h )
			if ( ! h->is_simple() )
				return 0;
	}
	return 1;
}

/* ---- 修復: even-odd ルールで repair。自己交差/重なりを解消し穴付き多角形集合に正規化。
 *      各 region を repair → 結果 Multipolygon を全 region に展開(集合の和)。---- */
sPtr<cgMesh>
cgMesh2D::op_repair()
{
	sPtr<cgMesh2D> out = thNEW(cgMesh2D,());
	for ( std::size_t i = 0 ; i < regions_.size() ; ++i ) {
		auto mp = CGAL::Polygon_repair::repair(regions_[i]);
		for ( const auto& pwh : mp.polygons_with_holes() )
			out->regions_.push_back(pwh);
	}
	return out;
}

/* ---- 着色: 2D は面色 property 非対応(SVG/DXF は別レイヤで色管理)→ null=呼び元が color の 2D エラー化 ---- */
sPtr<cgMesh>
cgMesh2D::op_color(int, int, int)
{
	return sPtr<cgMesh>();
}

bool
cgMesh2D::write_to(const char *path, const char *unit)
{
	const char* e = ext_of(path);
	int isSvg = ( ::strcasecmp(e, "svg") == 0 );
	int isDxf = ( ::strcasecmp(e, "dxf") == 0 );
	if ( ! isSvg && ! isDxf )
		return false;   /* 2D は SVG/DXF のみ(.off 等を 2D に投げたらエラー) */
	FILE* f = ::fopen(path, "wb");
	if ( f == 0 )
		return false;
	if ( isSvg ) write_svg(f, regions_, guides_, unit);
	else         write_dxf(f, regions_, guides_, unit);
	::fclose(f);
	return true;
}

/* ---- 2D アフィン(double[12] の xy 2x2 + xy 平行移動を使う。z 行・列は無視)---- */
static void xform_ring(Poly2& ring, const CGAL::Aff_transformation_2<K2>& aff, bool flip) {
	Poly2 out;
	for ( Poly2::Vertex_const_iterator it = ring.vertices_begin() ; it != ring.vertices_end() ; ++it )
		out.push_back(aff.transform(*it));
	if ( flip )
		out.reverse_orientation();   /* 反射(det<0)で向きが裏返るので戻す(外周 CCW 規約維持) */
	ring = out;
}

sPtr<cgMesh>
cgMesh2D::apply_affine(const double e[12])
{
	K2::FT a(e[0]), b(e[1]), tx(e[3]);
	K2::FT c(e[4]), d(e[5]), ty(e[7]);
	CGAL::Aff_transformation_2<K2> aff(a, b, tx, c, d, ty);
	bool flip = ( a*d - b*c ) < K2::FT(0);   /* 反射 */

	sPtr<cgMesh2D> out = thNEW(cgMesh2D,());
	out->regions_.reserve(regions_.size());
	for ( std::size_t i = 0 ; i < regions_.size() ; ++i ) {
		Poly2 outer = regions_[i].outer_boundary();
		xform_ring(outer, aff, flip);
		std::vector<Poly2> holes;
		for ( Pwh2::Hole_const_iterator h = regions_[i].holes_begin() ; h != regions_[i].holes_end() ; ++h ) {
			Poly2 hr = *h;
			xform_ring(hr, aff, flip);
			holes.push_back(hr);
		}
		out->regions_.push_back(Pwh2(outer, holes.begin(), holes.end()));
	}
	/* ガイド層も同じアフィンで動かす(line(...) >>> v 等の寸法線移動)。向きの概念はないので flip 無視。 */
	out->guides_.reserve(guides_.size());
	for ( std::size_t i = 0 ; i < guides_.size() ; ++i ) {
		Guide g;
		g.reserve(guides_[i].size());
		for ( std::size_t j = 0 ; j < guides_[i].size() ; ++j )
			g.push_back(aff(guides_[i][j]));
		out->guides_.push_back(g);
	}
	return out;
}
