/*
 * cgMesh2D — 2D 多角形領域(EPECK, 穴あき多角形の集合)の多態メソッド実装(Step C)。
 * ブール演算(Boolean_set_operations_2)・2D アフィン・codec(repr_type=32 "PLY2")。
 * extrude/revolve で 3D(cgMesh3D)へ持ち上がる(変換本体は cgaExtrude 等)。
 */
#include	"cg/c++/cgMeshCgal.h"
#include	"common/tube.h"   /* ★ #3535②: 2D tube の共通生成器 */
#include	<math.h>   /* ★ #3526: 枠の正規化 (sqrt) */
#include	"common/affine.h"   /* ★ #3533: DXF の任意軸は読み手と共有する (dxf_ocs_axes) */
#include	"cg/c++/cgaMeshCodec.h"   /* put_u32/put_coord/get_u32/get_coord(Sink/Source 越し)を再利用 */
#include	"ts2/c++/stdString.h"
#include	<string>
#include	<vector>
#include	<utility>

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

typedef K			K2;
typedef Polygon_2		Poly2;
typedef Pwh_2			Pwh2;
typedef CGAL::Polygon_set_2<K2>		PSet2;

/* ★★ #3545: ctor / dtor の実体はここ (ヘッダに置かない理由は cgMesh.h の宣言のところ)。 */
cgMesh2D::cgMesh2D(sPtr<pigInfo> i)
    : cgMesh(i), box_(new cgMesh2DBox()) {}

cgMesh2D::~cgMesh2D() { delete box_; }

/* ⚠ #3545: 箱 (regions) を触るのでヘッダの inline から降ろした。中身は元のまま。 */
int
cgMesh2D::op_topology(int *nshells, int *nparts, int *genus)
{
	if ( nshells ) *nshells = 0;
	if ( nparts  ) *nparts  = (int)box().regions.size();
	if ( genus   ) *genus   = 0;
	return 0;
}

/* ★★ #3545 段 2: 閉じたリングを 1 本 **外周として**足す (circle / rect / ngon / polygon)。
 *   ⚠ 向きはここで CCW に直す — 呼び手 (op) は点を並べるだけでよい。
 *   ⚠ 単純でないリングは捨てる (呼び手が自分の文言でエラーにできるよう、足した数は返さないが
 *     regions の増減で分かる)。 */
void
cgMesh2D::add_region_ring(const double *xy, int n)
{
	Poly2 p;
	for ( int i = 0 ; i < n ; ++i )
		p.push_back(K2::Point_2(K2::FT(xy[2*i]), K2::FT(xy[2*i+1])));
	if ( p.is_simple() && p.is_clockwise_oriented() ) p.reverse_orientation();
	box().regions.push_back(Pwh2(p));
}

/* ★★ #3545 段 2: リング列を **包含関係で組んで**足す (import の SVG / DXF 経路)。
 *   ⚠⚠ これを op 側に置くと @c Polygon_2::is_simple / bounded_side の述語がアサーション経由で
 *     @c _error_handler / @c _error_behaviour の実体を引き込む ⇒ CGAL はここでしか触らない。
 *   nest = 0 … 先頭が外周・残りは穴 (SVG の 1 <path> は必ずこの形)
 *   nest = 1 … 包含の深さで組む (偶数 = 外周 / 奇数 = 直近外周の穴。DXF の LWPOLYLINE 群) */
int
cgMesh2D::add_regions_from_rings(const double *xy, const int *ringLen, int nrings, int nest)
{
	std::vector<Poly2> polys;
	int off = 0;
	for ( int i = 0 ; i < nrings ; ++i ) {
		Poly2 p;
		for ( int k = 0 ; k < ringLen[i] ; ++k )
			p.push_back(K2::Point_2(K2::FT(xy[2*(off+k)]), K2::FT(xy[2*(off+k)+1])));
		off += ringLen[i];
		polys.push_back(p);
	}
	int added = 0;
	if ( nest == 0 ) {
		if ( polys.empty() ) return 0;
		Poly2 outer = polys[0];
		if ( outer.is_simple() && outer.is_clockwise_oriented() ) outer.reverse_orientation();
		std::vector<Poly2> holes;
		for ( std::size_t i = 1 ; i < polys.size() ; ++i ) {
			Poly2 hr = polys[i];
			if ( hr.is_simple() && hr.is_counterclockwise_oriented() ) hr.reverse_orientation();
			holes.push_back(hr);
		}
		box().regions.push_back(Pwh2(outer, holes.begin(), holes.end()));
		return 1;
	}
	/* 包含で nest: 各多角形の「内側にある他多角形」の数 = depth。偶数=外周、奇数=直近外周の穴。 */
	int n = (int)polys.size();
	std::vector<int> depth(n, 0);
	for ( int i = 0 ; i < n ; ++i ) {
		if ( ! polys[i].is_simple() ) continue;
		K2::Point_2 pt = *polys[i].vertices_begin();
		for ( int j = 0 ; j < n ; ++j ) {
			if ( j == i || ! polys[j].is_simple() ) continue;
			if ( polys[j].bounded_side(pt) == CGAL::ON_BOUNDED_SIDE )
				depth[i]++;
		}
	}
	for ( int i = 0 ; i < n ; ++i ) {
		if ( ! polys[i].is_simple() ) continue;
		if ( depth[i] % 2 != 0 ) continue;       /* 穴は外周側で拾う */
		Poly2 outer = polys[i];
		if ( outer.is_clockwise_oriented() ) outer.reverse_orientation();
		std::vector<Poly2> holes;
		for ( int k = 0 ; k < n ; ++k ) {        /* depth = i.depth+1 で i に含まれる = 直近の穴 */
			if ( k == i || ! polys[k].is_simple() || depth[k] != depth[i] + 1 ) continue;
			K2::Point_2 kp = *polys[k].vertices_begin();
			if ( polys[i].bounded_side(kp) != CGAL::ON_BOUNDED_SIDE ) continue;
			Poly2 hr = polys[k];
			if ( hr.is_counterclockwise_oriented() ) hr.reverse_orientation();
			holes.push_back(hr);
		}
		box().regions.push_back(Pwh2(outer, holes.begin(), holes.end()));
		added++;
	}
	return added;
}

/* ★★ #3545 段 2: ガイド (開ポリライン) を 1 本足す。 */
void
cgMesh2D::add_guide(const double *xy, int n)
{
	Guide g;
	for ( int i = 0 ; i < n ; ++i )
		g.push_back(K2::Point_2(K2::FT(xy[2*i]), K2::FT(xy[2*i+1])));
	box().guides.push_back(g);
}

/* ★★ #3545 段 2: 中身 (regions / guides) を丸ごと複製する。⚠ 枠 / placed_ は呼び手の担当。 */
void
cgMesh2D::copy_contents_from(sPtr<cgMesh2D> src)
{
	box().regions = src->box().regions;
	box().guides  = src->box().guides;
}

/* ---- get_str ---- */
/* ★★ #3535②: 2D の tube (cgaTube.cpp から移した)。折れ線を半径 r で太らせた帯領域を
 *   スタンプして union する。⚠ @c CGAL::Polygon_set_2 は内部で arrangement を使い、
 *   **乱数の大域変数 (@c get_default_random) の実体**まで引き込むので op 側には置けない
 *   (理由は cgMesh.h の宣言のところ)。 */
namespace {
struct CgRibbonSink {
	typedef K::Point_2                     P2;
	typedef CGAL::Polygon_2<K>             Poly2;
	typedef CGAL::Polygon_set_2<K>         PSet2;
	PSet2 acc;
	void add_ring(const double *xy, int npts) {
		Poly2 p;
		for ( int i = 0 ; i < npts ; ++i )
			p.push_back(P2(K::FT(xy[2*i]), K::FT(xy[2*i+1])));
		if ( ! p.is_simple() ) return;                 /* 退化(線分に潰れた等)は捨てる */
		if ( p.is_clockwise_oriented() ) p.reverse_orientation();
		acc.join(p);
	}
};
} /* anonymous namespace */

void
cgMesh2D::build_tube(const std::vector<srava_geo::TubeV3>& P,
                     const std::vector<double>& R, int segs)
{
	CgRibbonSink sink;
	srava_geo::make_tube_2d(P, R, segs, sink);
	std::vector<Pwh_2> res;
	res.resize(sink.acc.number_of_polygons_with_holes());
	sink.acc.polygons_with_holes(res.begin());
	box().regions = res;
}

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
	cgaMeshCodec::put_u32(sink, (uint32_t)box().regions.size());
	for ( std::size_t i = 0 ; i < box().regions.size() ; ++i ) {
		const Pwh2& pwh = box().regions[i];
		put_ring(sink, pwh.outer_boundary());
		cgaMeshCodec::put_u32(sink, (uint32_t)pwh.number_of_holes());
		for ( Pwh2::Hole_const_iterator h = pwh.holes_begin() ; h != pwh.holes_end() ; ++h )
			put_ring(sink, *h);
	}
	/* ガイド層(開ポリライン群)。regions の後ろに追記。後方互換: 旧 blob(この節が無い)は
	 * decode 側が src.more()==false で読み飛ばす。 */
	cgaMeshCodec::put_u32(sink, (uint32_t)box().guides.size());
	for ( std::size_t i = 0 ; i < box().guides.size() ; ++i ) {
		cgaMeshCodec::put_u32(sink, (uint32_t)box().guides[i].size());
		for ( std::size_t j = 0 ; j < box().guides[i].size() ; ++j ) {
			cgaMeshCodec::put_coord(sink, box().guides[i][j].x());
			cgaMeshCodec::put_coord(sink, box().guides[i][j].y());
		}
	}
	/* ★★ #3526: 枠 (frame) の節。**既定 (z=0 平面) なら書かない** ので、平面の 2D の blob は
	 *   従来とバイト単位で同じになる (既存キャッシュがそのまま効く)。
	 *   ⚠ ガイド層の節は **常に書かれる** ので、枠はその後ろに置く (manifold は 2D の節が
	 *     1 つだけなので順序の問題が無かった — こちらは順序が意味を持つ)。
	 *   ⚠ 逆向きの互換は無い — 古いバイナリはこの節を読まずに終わり、空間に置いた 2D が
	 *     **黙って z=0 に戻る**。⇒ cache_version を上げてある。
	 *   ★ 枠は double なので put_coord (有理数) ではなく put_f64 で書く。 */
	/* ★★ #3533: 条件が「枠が既定でない」から「**face3d である**」に変わった。
	 *   @rotate(rect,"z",90)@ は枠が既定のままでも型は face3d (規約①) なので、節を書かないと
	 *   *読み直したときに cross2d に戻ってしまう* (型スタンプはキャッシュ側のメモリにしか
	 *   載らないので、ブロブは自分で名乗れる必要がある)。
	 *   ⚠ 既定の枠のまま face3d な値は「既定の枠を明示的に書く」形になる ⇒ 同じ式のブロブが
	 *     #3526 時代と変わるので **cache_version を上げてある**。
	 *   ★ 逆向き (古いブロブ) は節が無い = cross2d で、それが正しい (face3d は無かった)。 */
	if ( placed_ || ! frame_is_default() ) {
		cgaMeshCodec::put_u32(sink, 0x4652414dU);   /* "MARF" = frame マーカ */
		for ( int i = 0 ; i < 3 ; ++i ) cgaMeshCodec::put_f64(sink, fo_[i]);
		for ( int i = 0 ; i < 3 ; ++i ) cgaMeshCodec::put_f64(sink, fu_[i]);
		for ( int i = 0 ; i < 3 ; ++i ) cgaMeshCodec::put_f64(sink, fv_[i]);
	}
}

void
cgMesh2D::decode(cgChunkSource& src)
{
	if ( mfc2Input_ ) { decode_mfc2(src); return; }   /* ★ Manifold 2D cache → 無損失昇格(#3404) */
	box().regions.clear();
	box().guides.clear();
	uint32_t npwh = cgaMeshCodec::get_u32(src);
	for ( uint32_t i = 0 ; i < npwh ; ++i ) {
		Poly2 outer = get_ring(src);
		uint32_t nh = cgaMeshCodec::get_u32(src);
		std::vector<Poly2> holes;
		for ( uint32_t j = 0 ; j < nh ; ++j )
			holes.push_back(get_ring(src));
		box().regions.push_back(Pwh2(outer, holes.begin(), holes.end()));
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
			box().guides.push_back(g);
		}
	}
	/* ★ #3526: 枠の節 (あれば)。旧 blob には無いので more() で判定する。 */
	if ( src.more() ) {
		uint32_t mark = cgaMeshCodec::get_u32(src);
		if ( mark == 0x4652414dU ) {
			for ( int i = 0 ; i < 3 ; ++i ) fo_[i] = cgaMeshCodec::get_f64(src);
			for ( int i = 0 ; i < 3 ; ++i ) fu_[i] = cgaMeshCodec::get_f64(src);
			for ( int i = 0 ; i < 3 ; ++i ) fv_[i] = cgaMeshCodec::get_f64(src);
			placed_ = 1;   /* ★ #3533: 節が在る = face3d (encode と対) */
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
	box().regions.clear();
	box().guides.clear();
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
		box().regions.push_back(Pwh2(outers[o], outerHoles[o].begin(), outerHoles[o].end()));
	/* ★ #3526: **枠も受け取る** (cast mf-cross2d → cg-cross2d)。mfCross::encode はリング列の
	 *   後ろに同じ "MARF" 節を書くので、そのまま読める。
	 *   ⚠ 読まないと **空間に置いた 2D が黙って z=0 に戻る** — cgal に枠が入るまでは
	 *     それが #3526 の残タスクだった。 */
	if ( src.more() ) {
		uint32_t mark = cgaMeshCodec::get_u32(src);
		if ( mark == 0x4652414dU ) {
			for ( int i = 0 ; i < 3 ; ++i ) fo_[i] = cgaMeshCodec::get_f64(src);
			for ( int i = 0 ; i < 3 ; ++i ) fu_[i] = cgaMeshCodec::get_f64(src);
			for ( int i = 0 ; i < 3 ; ++i ) fv_[i] = cgaMeshCodec::get_f64(src);
			placed_ = 1;   /* ★ #3533: 節が在る = face3d (encode と対) */
		}
	}
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
	cg_regions(out) = res;
	return out;
}
/* ★ #3526: ブール結果に枠を引き継ぐ (呼び手が枠の一致を確かめてから呼ぶ)。
 * ★ #3533: 併せて **型** も引き継ぐ。sig の規約③ は「どちらかが face3d なら結果は face3d」
 *   なので、@placed@ は 2 つの被演算子の **論理和**を渡す (呼び手が計算する)。
 *   ⚠ 表し直し (reexpress) の前に読むこと — reexpress は枠を相手に合わせた *新しい値* を作る。 */
static void carry_frame(sPtr<cgMesh> out, const double o[3], const double u[3], const double v[3],
                        int placed) {
	sPtr<cgMesh2D> p = sPtr<cgMesh2D>::d_cast(out);
	if ( p.is_notNull() ) { p->set_frame(o, u, v); p->set_placed(placed); }
}
/* ブール結果にガイド層を引き継ぐ(ガイドはブール演算の対象外=両被演算子のガイドをそのまま残す)。 */
static void carry_guides(sPtr<cgMesh> out, const std::vector<Guide>& ga,
                                            const std::vector<Guide>& gb) {
	sPtr<cgMesh2D> o = sPtr<cgMesh2D>::d_cast(out);
	if ( ! o.is_notNull() ) return;
	cg_guides(o) = ga;
	for ( std::size_t i = 0 ; i < gb.size() ; ++i ) cg_guides(o).push_back(gb[i]);
}

sPtr<cgMesh>
cgMesh2D::op_union(sPtr<cgMesh> b)
{
	sPtr<cgMesh2D> mb = sPtr<cgMesh2D>::d_cast(b);
	if ( ! mb.is_notNull() ) return sPtr<cgMesh>();
	/* ★ #3533: 規約③ — どちらかが face3d なら結果も face3d。**表し直しの前に**読む。 */
	const int outPlaced = ( placed_ || mb->is_placed() ) ? 1 : 0;
	/* ★★ #3526: 枠が違うときの扱いは **2 段**。
	 *   ① **同じ平面で軸の取り方だけが違う** なら、こちらの枠で **表し直してから** 計算する
	 *      (幾何は動かない・局所座標の読み方だけ揃える)。⇒ 使う側は枠を意識しなくてよい。
	 *   ② **本当に別の平面** なら null — 別の平面にある 2 つの平面領域の交わりは *線分以下* に
	 *      落ちるので 2D 領域として表せない (ひさ判断 ②)。呼び手が @cg_2d_planes_differ@ で
	 *      理由を言い分けて明示エラーにする。
	 *   ⚠ 枠が完全に一致しているときは表し直さない (既存の値を動かさないため)。 */
	if ( ! same_frame(mb->frame_o(), mb->frame_u(), mb->frame_v()) ) {
		sPtr<cgMesh2D> rb = mb->reexpress(fo_, fu_, fv_);
		if ( ! rb.is_notNull() ) return sPtr<cgMesh>();
		mb = rb;
	}
	PSet2 sa, sb;
	load_set(sa, box().regions);  load_set(sb, mb->box().regions);
	sa.join(sb);
	sPtr<cgMesh> out = extract(sa);
	carry_guides(out, box().guides, mb->box().guides);
	carry_frame(out, fo_, fu_, fv_, outPlaced);   /* ★ #3526/#3533: 枠と型を引き継ぐ */
	return out;
}
sPtr<cgMesh>
cgMesh2D::op_intersection(sPtr<cgMesh> b)
{
	sPtr<cgMesh2D> mb = sPtr<cgMesh2D>::d_cast(b);
	if ( ! mb.is_notNull() ) return sPtr<cgMesh>();
	/* ★ #3533: 規約③ — どちらかが face3d なら結果も face3d。**表し直しの前に**読む。 */
	const int outPlaced = ( placed_ || mb->is_placed() ) ? 1 : 0;
	/* ★★ #3526: 枠が違うときの扱いは **2 段**。
	 *   ① **同じ平面で軸の取り方だけが違う** なら、こちらの枠で **表し直してから** 計算する
	 *      (幾何は動かない・局所座標の読み方だけ揃える)。⇒ 使う側は枠を意識しなくてよい。
	 *   ② **本当に別の平面** なら null — 別の平面にある 2 つの平面領域の交わりは *線分以下* に
	 *      落ちるので 2D 領域として表せない (ひさ判断 ②)。呼び手が @cg_2d_planes_differ@ で
	 *      理由を言い分けて明示エラーにする。
	 *   ⚠ 枠が完全に一致しているときは表し直さない (既存の値を動かさないため)。 */
	if ( ! same_frame(mb->frame_o(), mb->frame_u(), mb->frame_v()) ) {
		sPtr<cgMesh2D> rb = mb->reexpress(fo_, fu_, fv_);
		if ( ! rb.is_notNull() ) return sPtr<cgMesh>();
		mb = rb;
	}
	PSet2 sa, sb;
	load_set(sa, box().regions);  load_set(sb, mb->box().regions);
	sa.intersection(sb);
	sPtr<cgMesh> out = extract(sa);
	carry_guides(out, box().guides, mb->box().guides);
	carry_frame(out, fo_, fu_, fv_, outPlaced);   /* ★ #3526/#3533: 枠と型を引き継ぐ */
	return out;
}
sPtr<cgMesh>
cgMesh2D::op_difference(sPtr<cgMesh> b)
{
	sPtr<cgMesh2D> mb = sPtr<cgMesh2D>::d_cast(b);
	if ( ! mb.is_notNull() ) return sPtr<cgMesh>();
	/* ★ #3533: 規約③ — どちらかが face3d なら結果も face3d。**表し直しの前に**読む。 */
	const int outPlaced = ( placed_ || mb->is_placed() ) ? 1 : 0;
	/* ★★ #3526: 枠が違うときの扱いは **2 段**。
	 *   ① **同じ平面で軸の取り方だけが違う** なら、こちらの枠で **表し直してから** 計算する
	 *      (幾何は動かない・局所座標の読み方だけ揃える)。⇒ 使う側は枠を意識しなくてよい。
	 *   ② **本当に別の平面** なら null — 別の平面にある 2 つの平面領域の交わりは *線分以下* に
	 *      落ちるので 2D 領域として表せない (ひさ判断 ②)。呼び手が @cg_2d_planes_differ@ で
	 *      理由を言い分けて明示エラーにする。
	 *   ⚠ 枠が完全に一致しているときは表し直さない (既存の値を動かさないため)。 */
	if ( ! same_frame(mb->frame_o(), mb->frame_u(), mb->frame_v()) ) {
		sPtr<cgMesh2D> rb = mb->reexpress(fo_, fu_, fv_);
		if ( ! rb.is_notNull() ) return sPtr<cgMesh>();
		mb = rb;
	}
	PSet2 sa, sb;
	load_set(sa, box().regions);  load_set(sb, mb->box().regions);
	sa.difference(sb);
	sPtr<cgMesh> out = extract(sa);
	carry_guides(out, box().guides, mb->box().guides);
	carry_frame(out, fo_, fu_, fv_, outPlaced);   /* ★ #3526/#3533: 枠と型を引き継ぐ */
	return out;
}
/* ---- combine: 両領域の Pwh + ガイド層をそのまま集めるだけ(ブール演算しない・重なり許容)。
 * ブール前に重なり具合を viewer で確認する用途、および line(ガイド)を部品に重ねる用途(`a +++ b`)。 ---- */
sPtr<cgMesh>
cgMesh2D::op_combine(sPtr<cgMesh> b)
{
	sPtr<cgMesh2D> mb = sPtr<cgMesh2D>::d_cast(b);
	if ( ! mb.is_notNull() ) return sPtr<cgMesh>();
	/* ★ #3533: 規約③ — どちらかが face3d なら結果も face3d。**表し直しの前に**読む。 */
	const int outPlaced = ( placed_ || mb->is_placed() ) ? 1 : 0;
	/* ★★ #3526: combine もブールと同じ 2 段 — 同じ平面で軸の取り方だけ違うなら表し直し、
	 *   本当に別の平面なら null (まとめた後の局所座標がどちらの平面を指すのか決まらない)。 */
	if ( ! same_frame(mb->frame_o(), mb->frame_u(), mb->frame_v()) ) {
		sPtr<cgMesh2D> rb = mb->reexpress(fo_, fu_, fv_);
		if ( ! rb.is_notNull() ) return sPtr<cgMesh>();
		mb = rb;
	}
	sPtr<cgMesh2D> out = thNEW(cgMesh2D,());
	out->set_frame(fo_, fu_, fv_);   /* ★ #3526: 枠は引き継ぐ */
	out->set_placed(outPlaced);      /* ★ #3533: 規約③ */
	cg_regions(out) = box().regions;
	for ( std::size_t i = 0 ; i < mb->box().regions.size() ; ++i )
		cg_regions(out).push_back(mb->box().regions[i]);
	cg_guides(out) = box().guides;
	for ( std::size_t i = 0 ; i < mb->box().guides.size() ; ++i )
		cg_guides(out).push_back(mb->box().guides[i]);
	return out;
}

/* ---- i 番目の片 (#3525) ----
 * ★ 片 = @box().regions@ の 1 要素。@op_topology@ が返す nparts と同じ列を同じ順で見ているので、
 *   @nparts@ で数えて @part@ で取り出す、が **索引として閉じる**。
 * ★ 枠と placed_ は引き継ぐ (同じ平面の上で一部を取り出すだけ = 型も置き場所も動かない)。
 * ⚠ ガイド層 (line) は片に属さないので持ち込まない。 */
sPtr<cgMesh2D>
cgMesh2D::op_part(int i)
{
	if ( i < 0 || (std::size_t)i >= box().regions.size() ) return sPtr<cgMesh2D>();
	sPtr<cgMesh2D> out = thNEW(cgMesh2D,());
	out->set_frame(fo_, fu_, fv_);
	out->set_placed(placed_);
	out->box().regions.push_back(box().regions[(std::size_t)i]);
	return out;
}

/* ---- オフセット(straight skeleton・面取り)。d>0 アウトセット / d<0 インセット / d=0 恒等。
 *      skeleton は内部 EPICK(sqrt 要)・出力 OfK=EPECK で有理座標に戻る。消滅/分裂は集合で表現。 ---- */
sPtr<cgMesh>
cgMesh2D::op_offset(double d, int /*subdiv*/)   /* 2D は subdiv 無視(skeleton は面取り) */
{
	sPtr<cgMesh2D> out = thNEW(cgMesh2D,());
	out->set_frame(fo_, fu_, fv_);   /* ★ #3526: 同じ平面の上で形を変える op なので枠はそのまま */
	out->set_placed(placed_);        /* ★ #3533: 型も動かない */
	if ( d == 0.0 ) { out->box().regions = box().regions; return out; }
	/* 入力の単純性チェック: 非単純(自己交差/重複頂点/零長エッジ)な多角形は straight skeleton が
	 * 破綻して空を返す。黙って空を返すと export が空ファイルになり原因が分からないので、null を返して
	 * 呼び出し側(cgaOffset)に明示エラーを出させる(ブール演算が失敗を明示するのと一貫)。 */
	for ( std::size_t r = 0 ; r < box().regions.size() ; ++r ) {
		if ( ! box().regions[r].outer_boundary().is_simple() )
			return thNULL;
		for ( auto h = box().regions[r].holes_begin() ; h != box().regions[r].holes_end() ; ++h )
			if ( ! h->is_simple() )
				return thNULL;
	}
	try {
		for ( std::size_t r = 0 ; r < box().regions.size() ; ++r ) {
			std::vector<std::shared_ptr<Pwh2> > res;
			if ( d > 0.0 )
				res = CGAL::create_exterior_skeleton_and_offset_polygons_with_holes_2(K2::FT(d),  box().regions[r], K2());
			else
				res = CGAL::create_interior_skeleton_and_offset_polygons_with_holes_2(K2::FT(-d), box().regions[r], K2());
			for ( std::size_t i = 0 ; i < res.size() ; ++i )
				if ( res[i] )
					out->box().regions.push_back(*res[i]);
		}
	} catch ( ... ) {
		return thNULL;   /* skeleton が退化形状で例外 → 明示エラー(クラッシュ回避) */
	}
	/* アウトセット(d>0)は非空・単純入力なら必ず成長する → 空なら失敗とみなす。
	 * インセット(d<0)の空は「収縮して消滅」= 正当な空集合なので残す(消滅/分裂は集合で表現)。 */
	if ( d > 0.0 && ! box().regions.empty() && out->box().regions.empty() )
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

/* ---- 出力座標のヘルパ ----
 * 断面の頂点は厳密有理数なので、**厳密には異なるのに double では同じ**点が隣り合うことがある
 * (corefinement 由来のごく近接した頂点。1 断面で数百4 組)。そのまま書くと SVG/DXF に
 * ゼロ長セグメントとして残り、CAM 側で自己交差やゼロ長要素として扱われる。書き出す値そのもの
 * (= %.12g に整形した文字列)で連続重複を落とす。厳密データはキャッシュ側にそのまま残る。 */
#define SEC_COORD_FMT "%.12g"
static std::string sec_fmt2(double x, double y) {
	char b[80];
	::snprintf(b, sizeof b, SEC_COORD_FMT "," SEC_COORD_FMT, x, y);
	return std::string(b);
}
/* リング(または折れ線)を「書き出す値」に落とし、連続重複と末尾=先頭の重複を除いた点列にする。 */
template<class It>
static std::vector<std::pair<double,double> > sec_out_points(It begin, It end, bool ring) {
	std::vector<std::pair<double,double> > out;
	std::vector<std::string> keys;
	for ( It v = begin ; v != end ; ++v ) {
		double x = CGAL::to_double(v->x()), y = CGAL::to_double(v->y());
		std::string k = sec_fmt2(x, y);
		if ( ! keys.empty() && k == keys.back() ) continue;
		keys.push_back(k);
		out.push_back(std::make_pair(x, y));
	}
	if ( ring ) while ( out.size() >= 2 && keys.front() == keys.back() ) { out.pop_back(); keys.pop_back(); }
	return out;
}

/* 座標は %.12g で書く。既定の %g は **有効 6 桁**しかなく、100mm 級の座標では 0.001mm 単位に
 * 量子化されて隣り合う頂点が同一点に潰れる (実測: 断面の頂点のうち相当数が連続重複点に
 * なっていた)。曲線の細かい起伏が消えたり、CAM 側で自己交差・ゼロ長セグメントとして扱われる。
 * 12 桁あれば 100mm 座標で 1e-10mm まで表現でき、ファイルサイズも %.17g ほど膨らまない。
 * (3MF/AMF ライタは元から %.17g = round-trip 桁。mesh3mf.h) */
static void write_svg(FILE* f, const std::vector<Pwh2>& regs,
                      const std::vector<Guide>& guides, const char* unit) {
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
		::fprintf(f, "width=\"%.12g%s\" height=\"%.12g%s\" ", maxx - minx, su, maxy - miny, su);
	::fprintf(f, "viewBox=\"%.12g %.12g %.12g %.12g\">\n", minx, miny, maxx - minx, maxy - miny);
	for ( std::size_t i = 0 ; i < regs.size() ; ++i ) {
		::fprintf(f, "  <path fill=\"#cccccc\" stroke=\"#000000\" stroke-width=\"0.01\" fill-rule=\"evenodd\" d=\"");
		/* 外周 + 穴を 1 つの path に(evenodd で穴が抜ける)。 */
		std::vector<const Poly2*> rs;
		rs.push_back(&regs[i].outer_boundary());
		for ( Pwh2::Hole_const_iterator h = regs[i].holes_begin() ; h != regs[i].holes_end() ; ++h )
			rs.push_back(&(*h));
		for ( std::size_t ri = 0 ; ri < rs.size() ; ++ri ) {
			const Poly2& r = *rs[ri];
			std::vector<std::pair<double,double> > pts =
			    sec_out_points(r.vertices_begin(), r.vertices_end(), true);
			for ( std::size_t k = 0 ; k < pts.size() ; ++k )
				::fprintf(f, "%s" SEC_COORD_FMT "," SEC_COORD_FMT " ",
				          (k == 0 ? "M" : "L"), pts[k].first, pts[k].second);
			::fprintf(f, "Z ");
		}
		::fprintf(f, "\"/>\n");
	}
	/* ガイド層: 塗りなしストロークの polyline(寸法線/ガイド)。色は青系・細線で部品と区別。 */
	for ( std::size_t i = 0 ; i < guides.size() ; ++i ) {
		if ( guides[i].size() < 2 ) continue;
		::fprintf(f, "  <polyline fill=\"none\" stroke=\"#0066cc\" stroke-width=\"0.05\" points=\"");
		{
			std::vector<std::pair<double,double> > pts =
			    sec_out_points(guides[i].begin(), guides[i].end(), false);
			for ( std::size_t j = 0 ; j < pts.size() ; ++j )
				::fprintf(f, SEC_COORD_FMT "," SEC_COORD_FMT " ", pts[j].first, pts[j].second);
		}
		::fprintf(f, "\"/>\n");
	}
	::fprintf(f, "</svg>\n");
}
/* 座標精度は write_svg と同じ %.12g (上の注記参照)。DXF は CAM へ流す形式なので特に効く。 */
/* ★★ #3533 規約④: DXF の **OCS (Object Coordinate System)**。
 *   LWPOLYLINE は「平面の上の閉じた折れ線」で、その平面は押し出し方向 210/220/230 (法線) と
 *   elevation 38 で表す。頂点の 10/20 は *その OCS の座標*。⇒ 空間に置かれた 2D をそのまま書ける。
 *
 * ⚠⚠ ただし **OCS の X 軸は選べない**。DXF は法線から「任意軸アルゴリズム」(Arbitrary Axis
 *   Algorithm・DXF リファレンス) で一意に決める規約なので、こちらの枠の U をそのまま OCS の X と
 *   みなして 10/20 を書くと *読み手が別の軸で解釈して図形が回る*。⇒ 法線から AAA の軸を作り直し、
 *   **world 座標を AAA の軸へ射影し直してから**書く。
 *
 *     |Nx| < 1/64 かつ |Ny| < 1/64  →  Ax = Wy x N   (Wy = (0,1,0))
 *     それ以外                      →  Ax = Wz x N   (Wz = (0,0,1))
 *     Ay = N x Ax   (どちらも正規化)
 *
 * ★ 枠が既定 (z=0) なら N=(0,0,1) で AAA は (X,Y) そのもの・elevation 0 ⇒ 10/20 は従来と
 *   同じ値になる。⇒ **既定の枠のときは 210/38 を書かない**ことで、既存の .dxf と
 *   バイト単位で同じにしてある (210 の既定値は (0,0,1) なので意味も変わらない)。 */
struct dxf_ocs {
	double n[3], ax[3], ay[3], elev;
	int    write;   /* 1 = 210/220/230 と 38 を書く (= 枠が既定でない) */
};
static void dxf_ocs_from_frame(const cgMesh2D* m, dxf_ocs* o) {
	o->write = 0;
	o->elev  = 0;
	/* 既定: N=+Z / Ax=+X / Ay=+Y (= world そのもの) */
	o->n[0] = 0; o->n[1] = 0; o->n[2] = 1;
	o->ax[0] = 1; o->ax[1] = 0; o->ax[2] = 0;
	o->ay[0] = 0; o->ay[1] = 1; o->ay[2] = 0;
	if ( m == 0 || m->frame_is_default() ) return;
	const double *u = m->frame_u(), *v = m->frame_v(), *org = m->frame_o();
	const double nraw[3] = { u[1]*v[2] - u[2]*v[1], u[2]*v[0] - u[0]*v[2], u[0]*v[1] - u[1]*v[0] };
	/* ★ #3533: 任意軸アルゴリズムは **読み手 (cgaImport の parse_dxf) と共有**する
	 *   (片方だけ直すと *書いたものが読めなくなる*)。 */
	double n[3], ax[3], ay[3];
	if ( ! srava_affine::dxf_ocs_axes(nraw, n, ax, ay) ) return;
	for ( int k = 0 ; k < 3 ; ++k ) { o->n[k]=n[k]; o->ax[k]=ax[k]; o->ay[k]=ay[k]; }
	o->elev  = org[0]*n[0] + org[1]*n[1] + org[2]*n[2];
	o->write = 1;
}
/* 局所 (x,y) → OCS の (x,y)。枠が既定なら恒等 (world = 局所 = OCS)。 */
static void dxf_to_ocs(const cgMesh2D* m, const dxf_ocs& o, double x, double y,
                       double* ox, double* oy) {
	if ( ! o.write ) { *ox = x; *oy = y; return; }
	double w[3];
	m->to_world(x, y, w);
	*ox = w[0]*o.ax[0] + w[1]*o.ax[1] + w[2]*o.ax[2];
	*oy = w[0]*o.ay[0] + w[1]*o.ay[1] + w[2]*o.ay[2];
}
static void dxf_put_ocs(FILE* f, const dxf_ocs& o) {
	if ( ! o.write ) return;
	::fprintf(f, "38\n" SEC_COORD_FMT "\n210\n" SEC_COORD_FMT "\n220\n" SEC_COORD_FMT
	             "\n230\n" SEC_COORD_FMT "\n", o.elev, o.n[0], o.n[1], o.n[2]);
}

static void write_dxf(FILE* f, const std::vector<Pwh2>& regs,
                      const std::vector<Guide>& guides, const char* unit,
                      const cgMesh2D* owner) {
	dxf_ocs ocs;
	dxf_ocs_from_frame(owner, &ocs);
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
			std::vector<std::pair<double,double> > pts =
			    sec_out_points(r.vertices_begin(), r.vertices_end(), true);
			if ( pts.size() < 3 ) continue;
			::fprintf(f, "0\nLWPOLYLINE\n8\n0\n90\n%d\n70\n1\n", (int)pts.size());  /* 70=1 閉じ */
			dxf_put_ocs(f, ocs);
			for ( std::size_t k = 0 ; k < pts.size() ; ++k ) {
				double ox, oy;
				dxf_to_ocs(owner, ocs, pts[k].first, pts[k].second, &ox, &oy);
				::fprintf(f, "10\n" SEC_COORD_FMT "\n20\n" SEC_COORD_FMT "\n", ox, oy);
			}
		}
	}
	/* ガイド層: 開いた LWPOLYLINE(70=0)。レイヤ "GUIDES" に分けて寸法線/ガイドと分かるように。 */
	for ( std::size_t i = 0 ; i < guides.size() ; ++i ) {
		if ( guides[i].size() < 2 ) continue;
		std::vector<std::pair<double,double> > pts =
		    sec_out_points(guides[i].begin(), guides[i].end(), false);
		if ( pts.size() < 2 ) continue;
		::fprintf(f, "0\nLWPOLYLINE\n8\nGUIDES\n90\n%d\n70\n0\n", (int)pts.size());  /* 70=0 開 */
		dxf_put_ocs(f, ocs);
		for ( std::size_t j = 0 ; j < pts.size() ; ++j ) {
			double ox, oy;
			dxf_to_ocs(owner, ocs, pts[j].first, pts[j].second, &ox, &oy);
			::fprintf(f, "10\n" SEC_COORD_FMT "\n20\n" SEC_COORD_FMT "\n", ox, oy);
		}
	}
	::fprintf(f, "0\nENDSEC\n0\nEOF\n");
}
/* ---- 計測: 頂点数 / 面数 (#3443) ----
 * 2D は「面」を持たないので op_nfaces()=0。頂点数は外周 + 穴の点の総数 (全リージョン合計)。
 * ★ガイド (開ポリライン) の点も数える (line() で作った寸法線などが空にならないように)。 */
int
cgMesh2D::op_nverts()
{
	int n = 0;
	for ( size_t i = 0 ; i < box().regions.size() ; ++i ) {
		n += (int)box().regions[i].outer_boundary().size();
		for ( Pwh_2::Hole_const_iterator h = box().regions[i].holes_begin() ;
		      h != box().regions[i].holes_end() ; ++h )
			n += (int)h->size();
	}
	for ( size_t i = 0 ; i < box().guides.size() ; ++i )
		n += (int)box().guides[i].size();
	return n;
}

int cgMesh2D::op_nfaces() { return 0; }

/* ---- ★ #3527: i 番目の点 (枠の中の x,y) ----
 * ★ op_nverts と **同じ順**で歩く: 各リージョンの 外周 → 穴、そのあと ガイド。
 *   ⇒ 片方だけ順序を変えると「数えられるのに取り出すと別物」になるので、**必ず対で直す**。
 * ⚠ 返すのは **枠の中の (x,y)** (返り 2)。cg-face3d が平面の外に居ても、ここでは枠座標。
 *   ⇒ 世界座標が要るなら、枠を掛ける op が別に要る (#3526 / #3534 の話)。 */
/* ---- ★ #3527 段 5: 局所 (x,y) を **型に応じた成分数** で書き出す共通部 ----------------
 * face3d (placed) … world の 3 成分   /   cross2d … 局所の 2 成分
 * ⚠⚠ op_vert と op_verts で **必ずここを通す**。片方だけ直すと
 *   「verts(m) の i 番目 == vert(m,i)」が黙って壊れる (test/srava_vert.sh が見ている等式)。 */
template<class FT>
static int cg2d_emit_vert(const cgMesh2D &m, const FT &x, const FT &y, int world, double out[3])
{
	const double px = CGAL::to_double(CGAL::exact(x));
	const double py = CGAL::to_double(CGAL::exact(y));
	if ( world ) { m.to_world(px, py, out); return 3; }
	out[0] = px; out[1] = py; out[2] = 0.0;
	return 2;
}

template<class FT>
static void cg2d_push_vert(const cgMesh2D &m, const FT &x, const FT &y, int world,
                           std::vector<double> &out)
{
	double p[3];
	const int n = cg2d_emit_vert(m, x, y, world, p);
	for ( int k = 0 ; k < n ; ++k ) out.push_back(p[k]);
}

int
cgMesh2D::op_vert(int i, double out[3])
{
	if ( i < 0 ) return 0;
	/* ★★ #3527 段 5 (ひさ判断 2026-09-17): **face3d は world の 3 成分**・cross2d は局所 2 成分。
	 *   op_bbox / op_centroid が #3533 で先に決めた約束をここへ揃えたもの。根拠も同じ:
	 *   局所座標のまま返すと **world では同じ図形が違う答えを返す** (経路依存)。
	 *   ⚠⚠ 揃えるまでは食い違っていた — 実測 (2026-09-17):
	 *       var b = box(4,4,10);
	 *       bbox(section(b,[0,0,2],[0,0,1],0))  [[0,0,2],[4,4,2]]   ★ world で **違う**
	 *       bbox(section(b,[0,0,7],[0,0,1],0))  [[0,0,7],[4,4,7]]
	 *       vert(section(b,[0,0,2],…),0)        [0,0]               ⚠ **同一** = z が消える
	 *       vert(section(b,[0,0,7],…),0)        [0,0]
	 *     ⇒ hull(verts(sec)) が常に z=0 に出るという形で *置き場所が黙って落ちて* いた。
	 *   ★ 揃える先が world なのは occt (oc-face3d) が元から world の 3 成分だから (#3533)。 */
	const int world = placed_;
	int k = 0;
	for ( size_t r = 0 ; r < box().regions.size() ; ++r ) {
		const Polygon_2& ob = box().regions[r].outer_boundary();
		for ( size_t t = 0 ; t < ob.size() ; ++t, ++k )
			if ( k == i ) return cg2d_emit_vert(*this, ob[t].x(), ob[t].y(), world, out);
		for ( Pwh_2::Hole_const_iterator h = box().regions[r].holes_begin() ;
		      h != box().regions[r].holes_end() ; ++h )
			for ( size_t t = 0 ; t < h->size() ; ++t, ++k )
				if ( k == i ) return cg2d_emit_vert(*this, (*h)[t].x(), (*h)[t].y(), world, out);
	}
	for ( size_t g = 0 ; g < box().guides.size() ; ++g )
		for ( size_t t = 0 ; t < box().guides[g].size() ; ++t, ++k )
			if ( k == i )
				return cg2d_emit_vert(*this, box().guides[g][t].x(), box().guides[g][t].y(), world, out);
	return 0;
}

/* ---- ★ #3527: 全点を平坦な配列へ ----
 * ⚠⚠ **op_vert / op_nverts と同じ歩き方**: 各リージョンの 外周 → 穴、そのあと ガイド。
 * ★★ 成分数は op_vert と同じ約束 — **face3d は world の 3 成分 (pt-cloud3d)**・
 *   cross2d は局所 2 成分 (pt-cloud2d)。片方だけ変えると verts(m)[i] == vert(m,i) が壊れる。 */
int
cgMesh2D::op_verts(std::vector<double> &out)
{
	out.clear();
	const int world = placed_;
	for ( size_t r = 0 ; r < box().regions.size() ; ++r ) {
		const Polygon_2& ob = box().regions[r].outer_boundary();
		for ( size_t t = 0 ; t < ob.size() ; ++t )
			cg2d_push_vert(*this, ob[t].x(), ob[t].y(), world, out);
		for ( Pwh_2::Hole_const_iterator h = box().regions[r].holes_begin() ;
		      h != box().regions[r].holes_end() ; ++h )
			for ( size_t t = 0 ; t < h->size() ; ++t )
				cg2d_push_vert(*this, (*h)[t].x(), (*h)[t].y(), world, out);
	}
	for ( size_t g = 0 ; g < box().guides.size() ; ++g )
		for ( size_t t = 0 ; t < box().guides[g].size() ; ++t )
			cg2d_push_vert(*this, box().guides[g][t].x(), box().guides[g][t].y(), world, out);
	return world ? 3 : 2;
}

/* ---- 計測: 囲み面積(各 Pwh の 外周面積 − 穴面積 の総和)。Polygon::area は符号付き ---- */
double
cgMesh2D::op_area()
{
	/* ★★ #3525: 面積も **厳密なまま積む** (重心と同じ理由・cgMesh2D.cpp の ring_moment 参照)。
	 *   ⚠⚠ 以前は片ごとに double へ落としてから足していたため、**片に分けると和が全体と
	 *     一致しなかった** (2026-09-15 に実測):
	 *
	 *       area(delaunay(p))   8.5449999999999982      ★ 同じ図形 (三角形 8 枚の和)
	 *       area(hull(p))       8.5450000000000017      ★ 1 枚の多角形として
	 *
	 *   「**片の測度の和 = 全体の測度**」は片へのアクセス規約 (#3527) が守ろうとしている
	 *   性質そのものなので、分け方で答えが変わってはいけない。⇒ 有理数のまま足して
	 *   最後に 1 回だけ double にする。⚠ 値が 1 ulp 級で動く ⇒ cache_version を上げてある。
	 *   ⚠ 3D の表面積は √ が要るので同じ直し方はできない (そちらは元から double)。 */
	K::FT total = 0;
	for ( std::size_t i = 0 ; i < box().regions.size() ; ++i ) {
		const Pwh_2& pwh = box().regions[i];
		total += CGAL::abs(pwh.outer_boundary().area());
		for ( Pwh_2::Hole_const_iterator h = pwh.holes_begin() ; h != pwh.holes_end() ; ++h )
			total -= CGAL::abs(h->area());
	}
	/* ⚠⚠ **@to_double@ だけでは足りない**。EPECK の FT は Lazy_exact_nt で、@to_double@ は
	 *   *区間近似*を返すので **正しく丸められていない**。⇒ 積む式の形が違うだけで 1〜2 ulp 動く
	 *   (2026-09-15 実測: 同じ四辺形が 1 枚だと 8.5450000000000017 ・ 三角形 2 枚に分けると
	 *    8.5449999999999999 = こちらが厳密値 8.545)。
	 *   ⇒ @CGAL::exact()@ で有理数を確定させてから落とす。これで初めて *式の形に依らない*。 */
	return CGAL::to_double(CGAL::exact(total));
}

/* ---- 2D に「体積」はない(呼び元 cgaVolume が dim==2 をエラーにする)---- */
double
cgMesh2D::op_volume()
{
	return 0.0;
}

/* 1 リング(Polygon_2)の境界長を double で足す(√は double)。 */
static double ring_perimeter(const Polygon_2& p)
{
	double total = 0.0;
	int n = (int)p.size();
	for ( int i = 0 ; i < n ; ++i ) {
		const K::Point_2& a = p[i];
		const K::Point_2& b = p[(i+1) % n];
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
	for ( std::size_t i = 0 ; i < box().regions.size() ; ++i ) {
		const Pwh_2& pwh = box().regions[i];
		if ( pwh.outer_boundary().size() > 0 )
			total += ring_perimeter(pwh.outer_boundary());
		for ( Pwh_2::Hole_const_iterator h = pwh.holes_begin() ; h != pwh.holes_end() ; ++h )
			total += ring_perimeter(*h);
	}
	return total;
}

/* 1 リングの符号付き面積 a と 1 次モーメント (mx,my) を shoelace で加算(向きで符号が付く)。 */
/* ★★ #3525: モーメントは **厳密なまま積む** (K::FT)。割り算は最後に 1 回だけ。
 *   ⚠⚠ 以前は各項を double にして @/6.0@ してから足していたため、**同じ図形でも頂点を
 *     どこから書き出したかで答えが変わっていた** (2026-09-15 に実測):
 *
 *       centroid(polygon([[1,1],[2,1],[2,2],[1,2]]))   [1.5,1.5]
 *       centroid(polygon([[2,1],[2,2],[1,2],[1,1]]))   [1.4999999999999998,…]   ★ 同じ正方形
 *
 *   同じ値に 2 つの答えが出る = bbox / centroid を world に揃えたときに潰したのと同じ病気
 *   (#3529 / #3533)。そちらは *置き場所* の経路依存で、これは *頂点の書き出し順* の経路依存。
 *   ⇒ 積む順序に依らないのは有理数のままのとき。⇒ 座標が厳密な cgal では直せる。
 *   ⚠ manifold の @mfCross::op_centroid@ は座標が double なので **同じ直し方はできない**
 *     (「片方だけ直さないこと」はここでは *world/局所の出し分け* にかかる約束で、
 *      算術の精度は表現が違う以上そろわない)。
 *   ⚠ 戻す値は 1 ulp 級で動く ⇒ cgal の cache_version を上げてある。
 *
 *   a2 = 2A ・ mx6 = 6A·cx ・ my6 = 6A·cy (割らずに溜める)。 */
static void ring_moment(const Polygon_2& p,
                        K::FT& a2, K::FT& mx6, K::FT& my6)
{
	int n = (int)p.size();
	for ( int i = 0 ; i < n ; ++i ) {
		const K::FT &xi = p[i].x(),       &yi = p[i].y();
		const K::FT &xj = p[(i+1)%n].x(), &yj = p[(i+1)%n].y();
		const K::FT cross = xi*yj - xj*yi;
		a2  += cross;
		mx6 += (xi + xj) * cross;
		my6 += (yi + yj) * cross;
	}
}

/* ---- 計測: 面積重心。外周(CCW=正)と穴(CW=負)の符号付きモーメントを合算して加重平均 ---- */
int
cgMesh2D::op_centroid(double out[3])
{
	K::FT a2 = 0, mx6 = 0, my6 = 0;
	for ( std::size_t i = 0 ; i < box().regions.size() ; ++i ) {
		const Pwh_2& pwh = box().regions[i];
		if ( pwh.outer_boundary().size() > 0 )
			ring_moment(pwh.outer_boundary(), a2, mx6, my6);
		for ( Pwh_2::Hole_const_iterator h = pwh.holes_begin() ; h != pwh.holes_end() ; ++h )
			ring_moment(*h, a2, mx6, my6);
	}
	/* cx = mx/a = (mx6/6)/(a2/2) = mx6/(3·a2)。★ 厳密に約分してから 1 回だけ double にする。 */
	double cx = 0, cy = 0;
	if ( a2 != 0 ) {
		/* ⚠ @exact()@ を通す理由は op_area と同じ (to_double は区間近似で正しく丸められない)。 */
		cx = CGAL::to_double(CGAL::exact(mx6 / (3 * a2)));
		cy = CGAL::to_double(CGAL::exact(my6 / (3 * a2)));
	}
	/* ★★ #3533 (ひさ判断 2026-09-15): **face3d の重心は world の 3 成分**・cross2d は局所 2 成分。
	 *   @op_bbox@ とまったく同じ理由 — 局所座標のまま返すと world では同じ図形に 2 つの答えが出る:
	 *
	 *     centroid(rotate(U,"x",90))    [1,1.5]     ★ 割れる (局所 2 成分)
	 *     centroid(rotate(U,"x",180))   [1,-1.5]
	 *
	 *   ⇒ world で返せば値が *world の幾何だけ* で決まる。occt (oc-face3d) は元から world の
	 *     3 成分なので、揃える先はそちら。⚠ mfCross::op_centroid と対 — 片方だけ直さないこと。 */
	if ( placed_ ) {
		to_world(cx, cy, out);
		return 3;
	}
	out[0] = cx; out[1] = cy; out[2] = 0.0;
	return 2;
}

/* ---- 計測: 軸平行バウンディングボックス(全 region の外周頂点走査で min/max。穴は内側なので不要)。
 *      空集合は全 0。返り=次元 2。---- */
int
cgMesh2D::op_bbox(double mn[3], double mx[3])
{
	/* ★★ #3533 (ひさ判断): **face3d の bbox は world の 3 成分**・cross2d は従来どおり局所 2 成分。
	 *   ⚠ これは「型集合に 1 行足すだけ」ではなく **返す値の形が変わる**変更 (チケットの波及表の訂正)。
	 *
	 *   根拠は *経路依存を消すこと*。局所座標のまま返すと、world では同じ図形が違う答えを返す:
	 *
	 *     var U = rect(2,3);
	 *     bbox(rotate(U,"x",180))              [[0,-3],[2,0]]     ★ 割れる (局所 2 成分)
	 *     bbox(rotate(rotate(U,"x",90),"x",90)) [[0,0],[2,3]]
	 *
	 *   x 軸まわり 180 度 = 90 度 x 2 は world では同じ図形なので、これは #3529 と同じ病気。
	 *   ⇒ world で返せば値が *world の幾何だけ* で決まる。occt (oc-face3d) は元から world の
	 *     3 成分なので、揃える先はそちら = 局所 2 成分の cg/mf の方が外れ値だった。
	 *   ★ 副産物: #3526 で「面積では置き場所を検出できない」ため使っていた回避策
	 *     (押し出せないことで置き場所を見る) が要らなくなる。 */
	const int world = placed_;
	bool first = true;
	for ( std::size_t i = 0 ; i < box().regions.size() ; ++i ) {
		const Poly2& o = box().regions[i].outer_boundary();
		for ( Poly2::Vertex_const_iterator v = o.vertices_begin() ; v != o.vertices_end() ; ++v ) {
			double p[3];
			if ( world ) to_world(CGAL::to_double(v->x()), CGAL::to_double(v->y()), p);
			else { p[0] = CGAL::to_double(v->x()); p[1] = CGAL::to_double(v->y()); p[2] = 0.0; }
			const int nd = world ? 3 : 2;
			if ( first ) {
				for ( int k = 0 ; k < nd ; ++k ) { mn[k] = mx[k] = p[k]; }
				first = false;
			} else {
				for ( int k = 0 ; k < nd ; ++k ) {
					if ( p[k] < mn[k] ) mn[k] = p[k];
					if ( p[k] > mx[k] ) mx[k] = p[k];
				}
			}
		}
	}
	if ( first ) { mn[0]=mn[1]=mn[2]=mx[0]=mx[1]=mx[2] = 0.0; }
	if ( ! world ) { mn[2] = mx[2] = 0.0; }
	return world ? 3 : 2;
}

/* ---- 検査: 全 region の外周/穴が単純(自己交差なし)---- */
int
cgMesh2D::op_valid()
{
	for ( std::size_t i = 0 ; i < box().regions.size() ; ++i ) {
		const Pwh_2& pwh = box().regions[i];
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
	out->set_frame(fo_, fu_, fv_);   /* ★ #3526: 同じ平面の上で形を変える op なので枠はそのまま */
	out->set_placed(placed_);        /* ★ #3533: 型も動かない */
	for ( std::size_t i = 0 ; i < box().regions.size() ; ++i ) {
		auto mp = CGAL::Polygon_repair::repair(box().regions[i]);
		for ( const auto& pwh : mp.polygons_with_holes() )
			out->box().regions.push_back(pwh);
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
	/* ★★ #3533 規約④: **SVG は置き場所を表せない**。#3526 以前はここで枠を黙って捨てており、
	 *   @export("flat.svg", rect(2,3))@ と @export("tilt.svg", rotate(rect(2,3),"x",45))@ が
	 *   **バイト単位で同じファイル**になっていた (2026-09-14 実測)。⇒ 断るのが正しい。
	 *   ★ 行き止まりにはならない: 幾何が z=0 に居るなら cast("cg-cross2d", …) で降ろせるし、
	 *     傾いているなら project_flatten(…) (#3534) で影として落とせる。
	 *   ⚠ DXF は OCS (210/220/230) で置き場所を書けるので **通す**。 */
	if ( isSvg && placed_ && ! frame_is_default() )
		return false;
	FILE* f = ::fopen(path, "wb");
	if ( f == 0 )
		return false;
	if ( isSvg ) write_svg(f, box().regions, box().guides, unit);
	else         write_dxf(f, box().regions, box().guides, unit, this);
	::fclose(f);
	return true;
}

/* ---- 2D アフィン(double[12] の xy 2x2 + xy 平行移動を使う。z 行・列は無視)---- */
/* ★ #3526: 枠 (double) の小道具。厳密演算とは混ぜない (枠は原理的に厳密にできない)。 */
static void cg2d_cross3(const double a[3], const double b[3], double o[3]) {
	o[0] = a[1]*b[2] - a[2]*b[1];
	o[1] = a[2]*b[0] - a[0]*b[2];
	o[2] = a[0]*b[1] - a[1]*b[0];
}
static double cg2d_dot3(const double a[3], const double b[3]) {
	return a[0]*b[0] + a[1]*b[1] + a[2]*b[2];
}
static double cg2d_norm3(double a[3]) {
	double n = ::sqrt(cg2d_dot3(a,a));
	if ( n > 0 ) { a[0]/=n; a[1]/=n; a[2]/=n; }
	return n;
}

static void xform_ring(Poly2& ring, const CGAL::Aff_transformation_2<K2>& aff, bool flip) {
	Poly2 out;
	for ( Poly2::Vertex_const_iterator it = ring.vertices_begin() ; it != ring.vertices_end() ; ++it )
		out.push_back(aff.transform(*it));
	if ( flip )
		out.reverse_orientation();   /* 反射(det<0)で向きが裏返るので戻す(外周 CCW 規約維持) */
	ring = out;
}

/* ★★ #3526: 同じ平面か (軸の取り方は違ってよい)。 */
int
cgMesh2D::same_plane(const double o[3], const double u[3], const double v[3]) const
{
	double n1[3], n2[3];
	cg2d_cross3(fu_, fv_, n1);
	cg2d_cross3(u, v, n2);
	if ( cg2d_norm3(n1) == 0.0 || cg2d_norm3(n2) == 0.0 ) return 0;
	double cr[3];
	cg2d_cross3(n1, n2, cr);
	const double EPS = 1e-12;
	if ( ::sqrt(cg2d_dot3(cr, cr)) > EPS ) return 0;          /* 法線が平行でない = 別の平面 */
	double d[3] = { o[0]-fo_[0], o[1]-fo_[1], o[2]-fo_[2] };
	double h = cg2d_dot3(d, n1);
	return ( ( h < 0 ? -h : h ) <= EPS ) ? 1 : 0;             /* 原点が同じ平面に載っているか */
}

/* ★★ #3526: 相手の枠で表し直す (幾何は動かさない・局所座標の取り方だけ変える)。
 *     x' = (O-o)·u + x(U·u) + y(V·u)      y' = (O-o)·v + x(U·v) + y(V·v)
 * ★ 枠は正規直交なので内積で射影できる。⚠ 2 つの枠の向きが逆だと det<0 = 鏡像になるので、
 *   apply_affine と同じく輪の巻き方を戻す (xform_ring の flip)。 */
sPtr<cgMesh2D>
cgMesh2D::reexpress(const double o[3], const double u[3], const double v[3]) const
{
	if ( ! same_plane(o, u, v) ) return sPtr<cgMesh2D>();
	double d[3] = { fo_[0]-o[0], fo_[1]-o[1], fo_[2]-o[2] };
	double a00 = cg2d_dot3(fu_, u), a01 = cg2d_dot3(fv_, u), a02 = cg2d_dot3(d, u);
	double a10 = cg2d_dot3(fu_, v), a11 = cg2d_dot3(fv_, v), a12 = cg2d_dot3(d, v);
	K2::FT a(a00), b(a01), tx(a02);
	K2::FT c(a10), dd(a11), ty(a12);
	CGAL::Aff_transformation_2<K2> aff(a, b, tx, c, dd, ty);
	bool flip = ( a*dd - b*c ) < K2::FT(0);

	sPtr<cgMesh2D> out = thNEW(cgMesh2D,());
	out->set_frame(o, u, v);
	out->set_placed(placed_);   /* ★ #3533: 同じ値を別の枠で読み直すだけ = 型は動かない */
	out->box().regions.reserve(box().regions.size());
	for ( std::size_t i = 0 ; i < box().regions.size() ; ++i ) {
		Poly2 outer = box().regions[i].outer_boundary();
		xform_ring(outer, aff, flip);
		std::vector<Poly2> holes;
		for ( Pwh2::Hole_const_iterator h = box().regions[i].holes_begin() ; h != box().regions[i].holes_end() ; ++h ) {
			Poly2 hr = *h;
			xform_ring(hr, aff, flip);
			holes.push_back(hr);
		}
		out->box().regions.push_back(Pwh2(outer, holes.begin(), holes.end()));
	}
	out->box().guides.reserve(box().guides.size());
	for ( std::size_t i = 0 ; i < box().guides.size() ; ++i ) {
		Guide g;
		g.reserve(box().guides[i].size());
		for ( std::size_t j = 0 ; j < box().guides[i].size() ; ++j )
			g.push_back(aff(box().guides[i][j]));
		out->box().guides.push_back(g);
	}
	return out;
}

/* ★★ #3534: world の (x,y) を取り z を捨てる (project_flatten)。
 *   局所 (x,y) → world → z を捨てる、は平面上の 2x2 アフィンに畳める (詳細はヘッダ)。
 *   ★ 構造は reexpress と同じ — 違うのは行き先の枠が **既定に固定**で、同一平面を要求しないこと。 */
sPtr<cgMesh2D>
cgMesh2D::project_flatten() const
{
	const double det = fu_[0]*fv_[1] - fu_[1]*fv_[0];   /* = (U x V)・ẑ */
	if ( ( det < 0 ? -det : det ) <= 1e-12 ) return sPtr<cgMesh2D>();   /* 平面が ẑ を含む */
	K2::FT a(fu_[0]), b(fv_[0]), tx(fo_[0]);
	K2::FT c(fu_[1]), d(fv_[1]), ty(fo_[1]);
	CGAL::Aff_transformation_2<K2> aff(a, b, tx, c, d, ty);
	bool flip = ( a*d - b*c ) < K2::FT(0);

	sPtr<cgMesh2D> out = thNEW(cgMesh2D,());
	/* 枠は既定のまま・placed_ は 0 ⇒ 型は cg-cross2d (SVG へ書ける)。 */
	out->box().regions.reserve(box().regions.size());
	for ( std::size_t i = 0 ; i < box().regions.size() ; ++i ) {
		Poly2 outer = box().regions[i].outer_boundary();
		xform_ring(outer, aff, flip);
		std::vector<Poly2> holes;
		for ( Pwh2::Hole_const_iterator h = box().regions[i].holes_begin() ; h != box().regions[i].holes_end() ; ++h ) {
			Poly2 hr = *h;
			xform_ring(hr, aff, flip);
			holes.push_back(hr);
		}
		out->box().regions.push_back(Pwh2(outer, holes.begin(), holes.end()));
	}
	out->box().guides.reserve(box().guides.size());
	for ( std::size_t i = 0 ; i < box().guides.size() ; ++i ) {
		Guide g;
		g.reserve(box().guides[i].size());
		for ( std::size_t j = 0 ; j < box().guides[i].size() ; ++j )
			g.push_back(aff(box().guides[i][j]));
		out->box().guides.push_back(g);
	}
	return out;
}

sPtr<cgMesh>
cgMesh2D::apply_affine(const double e[12])
{
	/* ---- ★★ #3526: 枠と局所座標に分けて当てる (manifold の mfCross::apply_affine と同じ規約) ----
	 * 2D の世界での姿は { O + xU + yV : (x,y) ∈ P }。これに (M,t) を当てると
	 *     O' = M O + t   U' = M U   V' = M V
	 * になる。⇒ **多角形 P は本来そのまま**でよい。ところが U',V' は正規直交とは限らず、
	 * 枠が歪むと面積だけでなく周長 / offset が局所座標で計算できなくなる (非等方な写像は
	 * 長さを変える) ⇒ *枠は正規直交に保ち、歪みは局所座標が持つ*。
	 *
	 * ★ 2 つに分かれる (どちらも同じ式 — 違うのは「新しい枠をどう選ぶか」だけ):
	 *   ① 平面が動かない … 枠は **そのまま**。A = (U',V') を旧 (U,V) で表した 2x2。
	 *      ⇒ z 軸回転・XY 平行移動・XY スケールは *今日とビット単位で同じ* になる
	 *        (既定の枠なら a00=e[0] a01=e[1] a02=e[3] a10=e[4] a11=e[5] a12=e[7] に戻る)。
	 *   ② 平面が動く    … 枠を動かす。U'' = normalize(U')・V'' = n x U'' を新しい軸にする。
	 *      ★ U'' の選び方を固定してあるので **同じ式は同じ枠**になる (再現しないと
	 *        キャッシュに焼き付いた値が版ごとに別物になる)。
	 *
	 * ⚠ 枠の計算は **double** (正規化に sqrt が要る)。局所座標の 2x2 は double から K2::FT へ
	 *   上げるので、そこから先の多角形演算は従来どおり厳密。
	 *   ⇒ *平面の上での計算は厳密・平面の置き場所は double* という切り分け。
	 * ⚠ 線形部が退化 (U' と V' が平行 = 平面が線に潰れる) なら null を返す。呼び手が明示エラーにする。 */
	double O2[3], U2[3], V2[3];
	for ( int i = 0 ; i < 3 ; ++i ) {
		O2[i] = e[4*i+0]*fo_[0] + e[4*i+1]*fo_[1] + e[4*i+2]*fo_[2] + e[4*i+3];
		U2[i] = e[4*i+0]*fu_[0] + e[4*i+1]*fu_[1] + e[4*i+2]*fu_[2];
		V2[i] = e[4*i+0]*fv_[0] + e[4*i+1]*fv_[1] + e[4*i+2]*fv_[2];
	}
	double n2[3];
	cg2d_cross3(U2, V2, n2);
	if ( cg2d_norm3(n2) == 0.0 ) return sPtr<cgMesh>();   /* 平面が潰れた */

	/* ① 平面が動かないか: 法線が同じ向き (符号は問わない) かつ O2 が元の平面に載っている。 */
	double n1[3];
	cg2d_cross3(fu_, fv_, n1);
	cg2d_norm3(n1);
	double cr[3];
	cg2d_cross3(n1, n2, cr);
	double dv[3] = { O2[0]-fo_[0], O2[1]-fo_[1], O2[2]-fo_[2] };
	const double FEPS = 1e-12;
	double dn = cg2d_dot3(dv, n1);
	int same_plane = ( ::sqrt(cg2d_dot3(cr,cr)) <= FEPS && ( dn < 0 ? -dn : dn ) <= FEPS );

	double nu[3], nv[3], no[3];
	if ( same_plane ) {                       /* 枠はそのまま */
		for ( int i = 0 ; i < 3 ; ++i ) { nu[i]=fu_[i]; nv[i]=fv_[i]; no[i]=fo_[i]; }
	} else {                                  /* 枠を動かす */
		for ( int i = 0 ; i < 3 ; ++i ) { nu[i]=U2[i]; no[i]=O2[i]; }
		if ( cg2d_norm3(nu) == 0.0 ) return sPtr<cgMesh>();
		cg2d_cross3(n2, nu, nv);
		if ( cg2d_norm3(nv) == 0.0 ) return sPtr<cgMesh>();
	}
	/* A = (U',V') を新しい軸 (nu,nv) で表した 2x2。平行移動は (O2-no) を新軸へ射影したもの。 */
	double dvo[3] = { O2[0]-no[0], O2[1]-no[1], O2[2]-no[2] };
	double a00 = cg2d_dot3(U2,nu), a01 = cg2d_dot3(V2,nu), a02 = cg2d_dot3(dvo,nu);
	double a10 = cg2d_dot3(U2,nv), a11 = cg2d_dot3(V2,nv), a12 = cg2d_dot3(dvo,nv);

	K2::FT a(a00), b(a01), tx(a02);
	K2::FT c(a10), d(a11), ty(a12);
	CGAL::Aff_transformation_2<K2> aff(a, b, tx, c, d, ty);
	bool flip = ( a*d - b*c ) < K2::FT(0);   /* 反射 */

	sPtr<cgMesh2D> out = thNEW(cgMesh2D,());
	out->set_frame(no, nu, nv);
	/* ★★ #3533 規約①: **transform 系は常に face3d を返す**。結果がたまたま z=0 に留まって
	 *   いても (z 軸回転・XY 平行移動) 型は face3d。⇒ 「軸によって出力型が変わる」を
	 *   書かずに済ませるための規約で、sig 側と 1 対 1 に対応させる必要がある。
	 *   ★ *幾何* として本当に z=0 に居るかは @frame_is_default()@ が別に答える
	 *     (cast("cg-cross2d", …) はそちらを見る)。 */
	out->set_placed(1);
	out->box().regions.reserve(box().regions.size());
	for ( std::size_t i = 0 ; i < box().regions.size() ; ++i ) {
		Poly2 outer = box().regions[i].outer_boundary();
		xform_ring(outer, aff, flip);
		std::vector<Poly2> holes;
		for ( Pwh2::Hole_const_iterator h = box().regions[i].holes_begin() ; h != box().regions[i].holes_end() ; ++h ) {
			Poly2 hr = *h;
			xform_ring(hr, aff, flip);
			holes.push_back(hr);
		}
		out->box().regions.push_back(Pwh2(outer, holes.begin(), holes.end()));
	}
	/* ガイド層も同じアフィンで動かす(line(...) >>> v 等の寸法線移動)。向きの概念はないので flip 無視。 */
	out->box().guides.reserve(box().guides.size());
	for ( std::size_t i = 0 ; i < box().guides.size() ; ++i ) {
		Guide g;
		g.reserve(box().guides[i].size());
		for ( std::size_t j = 0 ; j < box().guides[i].size() ; ++j )
			g.push_back(aff(box().guides[i][j]));
		out->box().guides.push_back(g);
	}
	return out;
}

/* ---- ★ #3527: 点を **含む** 片 (2D・2026-09-18) --------------------------------
 * ★★ 3D の @cgMesh3D::op_part_at@ と **同じ約束** — 「いちばん近い」ではなく「**含む**」。
 * ★ 点は **world** で受ける。#3533 (bbox / centroid) と 段 5 (vert / verts) が face3d を
 *   world に揃えたので、ここだけ局所座標にすると *同じ値に 2 通りの座標系* ができる。
 *   ⇒ face3d では「その平面上に在るか」を先に見て、無ければ断る (黙って射影しない)。
 * ★ 判定は **厳密** (@Polygon_2::bounded_side@ / EPECK)。穴の中は「材料が無い」ので外側。
 * 返り: >=0 片の番号 / -1 含まない / -2 2 つ以上が含む / -3 その平面上に無い。 */
int
cgMesh2D::op_part_at(const double p[3])
{
	double x, y;
	if ( placed_ ) {
		/* world → 枠。枠は正規直交 (affine.h の plane_frame) なので内積 3 本。 */
		const double d[3] = { p[0]-fo_[0], p[1]-fo_[1], p[2]-fo_[2] };
		const double n[3] = { fu_[1]*fv_[2] - fu_[2]*fv_[1],
		                      fu_[2]*fv_[0] - fu_[0]*fv_[2],
		                      fu_[0]*fv_[1] - fu_[1]*fv_[0] };
		const double outp = d[0]*n[0] + d[1]*n[1] + d[2]*n[2];
		const double len  = std::sqrt(d[0]*d[0] + d[1]*d[1] + d[2]*d[2]);
		/* ⚠ 相対許容差。絶対値で見ると大きな模型で落ちる。 */
		if ( std::fabs(outp) > 1e-9 * ( len > 1.0 ? len : 1.0 ) ) return -3;
		x = d[0]*fu_[0] + d[1]*fu_[1] + d[2]*fu_[2];
		y = d[0]*fv_[0] + d[1]*fv_[1] + d[2]*fv_[2];
	} else {
		/* cross2d は z=0 の表現。⇒ 面外の点は断る (face3d と同じ線引き)。 */
		const double len = std::sqrt(p[0]*p[0] + p[1]*p[1] + p[2]*p[2]);
		if ( std::fabs(p[2]) > 1e-9 * ( len > 1.0 ? len : 1.0 ) ) return -3;
		x = p[0]; y = p[1];
	}
	const K2::Point_2 q(x, y);
	int found = -1, nfound = 0;
	for ( std::size_t r = 0 ; r < box().regions.size() ; ++r ) {
		const Polygon_2& ob = box().regions[r].outer_boundary();
		if ( ob.size() < 3 ) continue;
		if ( ob.bounded_side(q) == CGAL::ON_UNBOUNDED_SIDE ) continue;
		/* 穴の **中** なら材料は無い ⇒ その片には含まれない。 */
		int inHole = 0;
		for ( Pwh_2::Hole_const_iterator h = box().regions[r].holes_begin() ;
		      !inHole && h != box().regions[r].holes_end() ; ++h )
			if ( h->size() >= 3 && h->bounded_side(q) == CGAL::ON_BOUNDED_SIDE ) inHole = 1;
		if ( inHole ) continue;
		found = (int)r; ++nfound;
	}
	if ( nfound == 0 ) return -1;
	if ( nfound >  1 ) return -2;
	return found;
}
