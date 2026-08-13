/*
 * d3Mesh — 第3(mesh 出力)カーネルの幾何本体実装 (rev4 Phase D-3)。CGAL/Manifold 非依存。
 */
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/ptsApplication.h"   /* ptsObject 派生 TU の作法 (ptsApp 完全型) */
#include	"pig/c++/pigData.h"
#include	"ts2/c++/stdString.h"
#include	"d3/c++/d3Mesh.h"

#include	<string.h>   /* memcpy / memcmp */
#include	<stdio.h>    /* snprintf */

/* ---- codec framing helpers (little-endian・mfMesh のミラー) ---- */
static void put_u32(d3ChunkSink &s, uint32_t v) {
	uint8_t b[4] = { (uint8_t)v, (uint8_t)(v>>8), (uint8_t)(v>>16), (uint8_t)(v>>24) };
	s.chunk(b, 4);
}
static void put_f64(d3ChunkSink &s, double d) {
	uint8_t b[8]; ::memcpy(b, &d, 8); s.chunk(b, 8);
}
static uint32_t get_u32(d3ChunkSource &s) {
	uint8_t b[4]; s.pull(b, 4);
	return (uint32_t)b[0] | ((uint32_t)b[1]<<8) | ((uint32_t)b[2]<<16) | ((uint32_t)b[3]<<24);
}
static double get_f64(d3ChunkSource &s) {
	uint8_t b[8]; s.pull(b, 8); double d; ::memcpy(&d, b, 8); return d;
}

sPtr<stdString>
d3Mesh::get_str()
{
	char b[64];
	::snprintf(b, sizeof b, "d3mesh(%u,%u)", (unsigned)nv(), (unsigned)nf());
	return thNEW(stdString,(b));
}

void
d3Mesh::encode(d3ChunkSink &sink)
{
	put_u32(sink, nv());
	put_u32(sink, nf());
	for ( size_t k = 0 ; k < verts_.size() ; ++k ) put_f64(sink, verts_[k]);
	for ( size_t k = 0 ; k < faces_.size() ; ++k ) put_u32(sink, faces_[k]);
}

void
d3Mesh::decode(d3ChunkSource &src)
{
	uint32_t n_v = get_u32(src);
	uint32_t n_f = get_u32(src);
	verts_.resize((size_t)n_v * 3);
	for ( size_t k = 0 ; k < verts_.size() ; ++k ) verts_[k] = get_f64(src);
	faces_.resize((size_t)n_f * 3);
	for ( size_t k = 0 ; k < faces_.size() ; ++k ) faces_[k] = get_u32(src);
}

/* 原点隅の s×s×s 立方体 (cgaBox/mfMesh::box の意味論に合わせ 8 頂点 12 三角形)。 */
sPtr<d3Mesh>
d3Mesh::cube(double s)
{
	sPtr<d3Mesh> m = thNEW(d3Mesh,());
	static const double V[8][3] = {
		{0,0,0},{1,0,0},{1,1,0},{0,1,0},
		{0,0,1},{1,0,1},{1,1,1},{0,1,1},
	};
	static const uint32_t F[12][3] = {
		{0,2,1},{0,3,2},   /* bottom (z=0) */
		{4,5,6},{4,6,7},   /* top    (z=1) */
		{0,1,5},{0,5,4},   /* front  (y=0) */
		{2,3,7},{2,7,6},   /* back   (y=1) */
		{1,2,6},{1,6,5},   /* right  (x=1) */
		{0,4,7},{0,7,3},   /* left   (x=0) */
	};
	m->verts_.resize(8 * 3);
	for ( int v = 0 ; v < 8 ; ++v ) {
		m->verts_[v*3+0] = V[v][0] * s;
		m->verts_[v*3+1] = V[v][1] * s;
		m->verts_[v*3+2] = V[v][2] * s;
	}
	m->faces_.resize(12 * 3);
	for ( int f = 0 ; f < 12 ; ++f ) {
		m->faces_[f*3+0] = F[f][0];
		m->faces_[f*3+1] = F[f][1];
		m->faces_[f*3+2] = F[f][2];
	}
	return m;
}

/* 2 メッシュの頂点/面を連結 (ブールなし)。b の面 index は a の頂点数だけオフセットする。
 * → nv = nv_a + nv_b・nf = nf_a + nf_b (立方体 2 個で 16v/24f になる = 往復検証点)。 */
sPtr<d3Mesh>
d3Mesh::merge(sPtr<d3Mesh> a, sPtr<d3Mesh> b)
{
	if ( ! a.is_notNull() || ! b.is_notNull() ) return sPtr<d3Mesh>();
	sPtr<d3Mesh> m = thNEW(d3Mesh,());
	uint32_t offv = a->nv();
	m->verts_ = a->verts_;
	m->verts_.insert(m->verts_.end(), b->verts_.begin(), b->verts_.end());
	m->faces_ = a->faces_;
	for ( size_t k = 0 ; k < b->faces_.size() ; ++k )
		m->faces_.push_back(b->faces_[k] + offv);
	return m;
}

sPtr<d3Mesh>
d3Mesh::create_for_meta(const uint8_t *meta, int len)
{
	if ( len == 4 && ::memcmp(meta, "D3M3", 4) == 0 )
		return thNEW(d3Mesh,());
	return sPtr<d3Mesh>();
}
