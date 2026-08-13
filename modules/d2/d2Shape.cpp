/*
 * d2Shape — 第2(2D)カーネルの幾何本体実装 (rev4 次元分担デモ)。CGAL/Manifold 非依存。d3Mesh のミラー。
 */
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"ts2/c++/stdString.h"
#include	"d2/c++/d2Shape.h"

#include	<string.h>
#include	<stdio.h>

static void put_u32(d2ChunkSink &s, uint32_t v) {
	uint8_t b[4] = { (uint8_t)v, (uint8_t)(v>>8), (uint8_t)(v>>16), (uint8_t)(v>>24) };
	s.chunk(b, 4);
}
static void put_f64(d2ChunkSink &s, double d) {
	uint8_t b[8]; ::memcpy(b, &d, 8); s.chunk(b, 8);
}
static uint32_t get_u32(d2ChunkSource &s) {
	uint8_t b[4]; s.pull(b, 4);
	return (uint32_t)b[0] | ((uint32_t)b[1]<<8) | ((uint32_t)b[2]<<16) | ((uint32_t)b[3]<<24);
}
static double get_f64(d2ChunkSource &s) {
	uint8_t b[8]; s.pull(b, 8); double d; ::memcpy(&d, b, 8); return d;
}

sPtr<stdString>
d2Shape::get_str()
{
	char b[64];
	::snprintf(b, sizeof b, "d2shape(%u)", (unsigned)np());
	return thNEW(stdString,(b));
}

void
d2Shape::encode(d2ChunkSink &sink)
{
	put_u32(sink, np());
	for ( size_t k = 0 ; k < pts_.size() ; ++k ) put_f64(sink, pts_[k]);
}

void
d2Shape::decode(d2ChunkSource &src)
{
	uint32_t n_p = get_u32(src);
	pts_.resize((size_t)n_p * 2);
	for ( size_t k = 0 ; k < pts_.size() ; ++k ) pts_[k] = get_f64(src);
}

/* 原点隅の s×s 正方形 (4 点)。 */
sPtr<d2Shape>
d2Shape::square(double s)
{
	sPtr<d2Shape> m = thNEW(d2Shape,());
	static const double P[4][2] = { {0,0},{1,0},{1,1},{0,1} };
	m->pts_.resize(4 * 2);
	for ( int i = 0 ; i < 4 ; ++i ) {
		m->pts_[i*2+0] = P[i][0] * s;
		m->pts_[i*2+1] = P[i][1] * s;
	}
	return m;
}

sPtr<d2Shape>
d2Shape::create_for_meta(const uint8_t *meta, int len)
{
	if ( len == 4 && ::memcmp(meta, "D2S2", 4) == 0 )
		return thNEW(d2Shape,());
	return sPtr<d2Shape>();
}
