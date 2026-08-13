/*
 * mfCross — 2D manifold::CrossSection ラッパの実装(cgMesh2D のミラー・CGAL 非依存)。
 *   polygon/rect/circle/ngon で断面を作り、extrude/revolve(mfaExtrude/mfaRevolve)が 3D へ持ち上げる。
 *   codec は ToPolygons() のリング列を raw double で直列化(mfMesh の raw-double 方針と一貫)。
 */
#include	"mf/c++/mfMesh.h"
#include	"ts2/c++/stdString.h"

#include	<cstdio>
#include	<cstring>
#include	<cstdint>
#include	<cstddef>
#include	<cmath>
#include	<vector>
#include	<algorithm>

using manifold::CrossSection;
using manifold::SimplePolygon;
using manifold::Polygons;
using manifold::vec2;

sPtr<stdString>
mfCross::get_str()
{
	char buf[64];
	::snprintf(buf, sizeof buf, "<cross:manifold area=%.6g>", c_.Area());
	return thNEW(stdString,(buf));
}

/* ---- codec: リング列を raw double で(little-endian)---- */
static void put_u32(mfChunkSink &s, uint32_t v) {
	uint8_t b[4] = { (uint8_t)v, (uint8_t)(v>>8), (uint8_t)(v>>16), (uint8_t)(v>>24) };
	s.chunk(b, 4);
}
static void put_f64(mfChunkSink &s, double d) {
	uint8_t b[8]; ::memcpy(b, &d, 8); s.chunk(b, 8);
}
static uint32_t get_u32(mfChunkSource &s) {
	uint8_t b[4]; s.pull(b, 4);
	return (uint32_t)b[0] | ((uint32_t)b[1]<<8) | ((uint32_t)b[2]<<16) | ((uint32_t)b[3]<<24);
}
static double get_f64(mfChunkSource &s) {
	uint8_t b[8]; s.pull(b, 8); double d; ::memcpy(&d, b, 8); return d;
}

void
mfCross::encode(mfChunkSink &sink)
{
	Polygons ps = c_.ToPolygons();
	put_u32(sink, (uint32_t)ps.size());
	for ( size_t r = 0 ; r < ps.size() ; ++r ) {
		const SimplePolygon &ring = ps[r];
		put_u32(sink, (uint32_t)ring.size());
		for ( size_t i = 0 ; i < ring.size() ; ++i ) {
			put_f64(sink, ring[i].x);
			put_f64(sink, ring[i].y);
		}
	}
}

void
mfCross::decode(mfChunkSource &src)
{
	if ( crossExactInput_ ) { decode_cross_exact(src); return; }   /* ★ CGAL PLY2 → double(cast downgrade) */
	uint32_t nr = get_u32(src);
	Polygons ps;
	ps.reserve(nr);
	for ( uint32_t r = 0 ; r < nr ; ++r ) {
		uint32_t npt = get_u32(src);
		SimplePolygon ring;
		ring.reserve(npt);
		for ( uint32_t i = 0 ; i < npt ; ++i ) {
			double x = get_f64(src);
			double y = get_f64(src);
			ring.push_back(vec2(x, y));
		}
		ps.push_back(ring);
	}
	/* ToPolygons() は外周 CCW・穴 CW で出るので NonZero で忠実に再構成。 */
	c_ = CrossSection(ps, CrossSection::FillRule::NonZero);
}

/* ---- 2D ブーリアン(CrossSection +/^/-)---- */
sPtr<mfCross>
mfCross::op_union(sPtr<mfCross> b)
{
	if ( ! b.is_notNull() ) return sPtr<mfCross>();
	return thNEW(mfCross,(c_ + b->c_));
}
sPtr<mfCross>
mfCross::op_intersection(sPtr<mfCross> b)
{
	if ( ! b.is_notNull() ) return sPtr<mfCross>();
	return thNEW(mfCross,(c_ ^ b->c_));
}
sPtr<mfCross>
mfCross::op_difference(sPtr<mfCross> b)
{
	if ( ! b.is_notNull() ) return sPtr<mfCross>();
	return thNEW(mfCross,(c_ - b->c_));
}

/* ---- 計測 ---- */
double mfCross::op_area() { return c_.Area(); }

int
mfCross::op_bbox(double mn[3], double mx[3])
{
	manifold::Rect b = c_.Bounds();
	mn[0] = b.min.x; mn[1] = b.min.y; mn[2] = 0.0;
	mx[0] = b.max.x; mx[1] = b.max.y; mx[2] = 0.0;
	return 2;
}

/* アフィン変換(2D): 行優先 double[12] の XY 2x2 線形部 + XY 平行移動を CrossSection::Transform へ。
 *   mat2x3 は la 列優先(3 列×vec2): col0=(m00,m10)・col1=(m01,m11)・col2=(tx,ty)。 */
sPtr<mfGeom>
mfCross::apply_affine(const double e[12])
{
	manifold::mat2x3 m(
	    vec2(e[0], e[4]),    /* col 0 */
	    vec2(e[1], e[5]),    /* col 1 */
	    vec2(e[3], e[7]));   /* col 2 = 平行移動(tx,ty) */
	return thNEW(mfCross,(c_.Transform(m)));
}

bool
mfCross::write_to(const char * /*path*/, const char * /*unit*/)
{
	return false;   /* 2D 出力(SVG/DXF)は当面未対応 */
}

/* ---- primitive ---- */
sPtr<mfCross>
mfCross::polygon(const double *xy, int npts)
{
	/* 点列 → SimplePolygon。連続重複・閉じ重複を間引く(cgaPolygon と一貫。零長エッジ除去)。
	 * 符号付き面積が負(CW)なら反転して CCW(=Positive)に正規化。 */
	SimplePolygon ring;
	ring.reserve(npts);
	for ( int i = 0 ; i < npts ; ++i ) {
		vec2 p(xy[2*i], xy[2*i+1]);
		if ( ! ring.empty() && ring.back().x == p.x && ring.back().y == p.y )
			continue;
		ring.push_back(p);
	}
	while ( ring.size() >= 2 && ring.back().x == ring.front().x && ring.back().y == ring.front().y )
		ring.pop_back();
	/* 符号付き面積(shoelace)。負=CW → 反転して CCW。 */
	double a2 = 0.0;
	for ( size_t i = 0 ; i < ring.size() ; ++i ) {
		const vec2 &p = ring[i], &q = ring[(i+1) % ring.size()];
		a2 += p.x * q.y - q.x * p.y;
	}
	if ( a2 < 0.0 )
		std::reverse(ring.begin(), ring.end());
	return thNEW(mfCross,(CrossSection(ring, CrossSection::FillRule::Positive)));
}

sPtr<mfCross>
mfCross::rect(double w, double h)
{
	/* cgaRect に合わせ原点隅(0,0)→(w,h)。center=false。 */
	return thNEW(mfCross,(CrossSection::Square(vec2(w, h), false)));
}

sPtr<mfCross>
mfCross::circle(double r, int segs)
{
	if ( segs < 3 ) segs = 32;
	return thNEW(mfCross,(CrossSection::Circle(r, segs)));
}

sPtr<mfCross>
mfCross::ngon(int n, double r)
{
	if ( n < 3 ) n = 3;
	SimplePolygon ring;
	ring.reserve(n);
	for ( int k = 0 ; k < n ; ++k ) {
		double a = 2.0 * 3.14159265358979323846 * (double)k / (double)n;
		ring.push_back(vec2(r * ::cos(a), r * ::sin(a)));
	}
	return thNEW(mfCross,(CrossSection(ring, CrossSection::FillRule::Positive)));
}
