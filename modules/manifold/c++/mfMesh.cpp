/*
 * mfMesh — Manifold 幾何ラッパの実装(cgMesh3D のミラー・CGAL 非依存)。
 */
#include	"mf/c++/mfMesh.h"
#include	"ts2/c++/stdString.h"

#include	<cstdio>
#include	<cstring>
#include	<cstdint>
#include	<cstddef>
#include	<cstdlib>    /* getenv (decode 内訳プローブ) */
#include	<cmath>
#include	<map>
#include	<vector>
#include	<utility>
#include	"common/geodesic.h"   /* sphere/icosphere の測地球生成 (cgal と共通) */
#include	"common/mesh3mf.h"   /* AMF/3MF ライタ (cgal と共通) */
#include	<sys/time.h>  /* gettimeofday (decode 内訳プローブ) */
#include	<unistd.h>    /* getpid */

using manifold::Manifold;
using manifold::MeshGL64;

/* decode 内訳プローブ: PIG_TIMING が設定されていれば各サブフェーズの絶対 ms を追記。
 * mfatsAgent の [timing] と同一ファイル・同一 pid(reader は agent の子=同プロセス)なので
 * recv_op/parse_done と突き合わせて read/decode/Manifold 再構築を切り分けできる。 */
static void mf_decode_timing(const char* tag) {
	const char* path = ::getenv("PIG_TIMING");
	if ( path == 0 || path[0] == 0 ) return;
	struct timeval tv; ::gettimeofday(&tv, 0);
	double now = tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
	FILE* f = ::fopen(path, "a");
	if ( f ) { ::fprintf(f, "[decode pid=%d] %-16s abs=%.1f ms\n", (int)::getpid(), tag, now); ::fclose(f); }
}

sPtr<stdString>
mfMesh::get_str()
{
	char buf[64];
	::snprintf(buf, sizeof buf, "<mesh:manifold tris=%zu>", (size_t)m_.NumTri());
	return thNEW(stdString,(buf));
}

/* ---- codec: raw double 頂点 + uint 三角形(little-endian。全対象 LE 前提)---- */
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
/* 色プロパティ (double 0-255) ⇄ packed 0xRRGGBB。範囲外はクランプ (ブールの補間で外れうる)。 */
static uint32_t pack_rgb(double r, double g, double b) {
	int ri = (int)(r + 0.5), gi = (int)(g + 0.5), bi = (int)(b + 0.5);
	if ( ri < 0 ) ri = 0; if ( ri > 255 ) ri = 255;
	if ( gi < 0 ) gi = 0; if ( gi > 255 ) gi = 255;
	if ( bi < 0 ) bi = 0; if ( bi > 255 ) bi = 255;
	return ((uint32_t)ri << 16) | ((uint32_t)gi << 8) | (uint32_t)bi;
}

void
mfMesh::encode(mfChunkSink &sink)
{
	mf_decode_timing("enc_start");
	MeshGL64 m = m_.GetMeshGL64();   /* ★ lazy CSG(ブール等)はここで初めて評価される */
	mf_decode_timing("enc_meshgl");  /* GetMeshGL64 完了 = 実ブール評価 + メッシュ抽出済 */
	const int np = m.numProp;
	uint32_t nv = (uint32_t)(m.vertProperties.size() / (np ? np : 1));
	uint32_t nt = (uint32_t)(m.triVerts.size() / 3);
	put_u32(sink, nv);
	put_u32(sink, nt);
	for ( uint32_t v = 0 ; v < nv ; ++v ) {
		const double *p = &m.vertProperties[(size_t)v * np];
		put_f64(sink, p[0]); put_f64(sink, p[1]); put_f64(sink, p[2]);
	}
	for ( size_t i = 0 ; i < m.triVerts.size() ; ++i )
		put_u32(sink, (uint32_t)m.triVerts[i]);
	/* 色 section: 頂点プロパティ ch3..5 (RGB 0-255) があれば 1 + 頂点×nv の packed 0xRRGGBB。
	 * 無ければ 0。旧 cache はこの section 自体が無く、decode は more() で判定する (後方互換)。 */
	if ( np >= 6 ) {
		put_u32(sink, 1u);
		for ( uint32_t v = 0 ; v < nv ; ++v ) {
			const double *p = &m.vertProperties[(size_t)v * np];
			put_u32(sink, pack_rgb(p[3], p[4], p[5]));
		}
	} else {
		put_u32(sink, 0u);
	}
	/* ★ merge ベクタ: 「位置は同じだがプロパティが違う」ために分裂した頂点の対応表。
	 * 色が付くと、成分の境界 (2 つの箱が接する角など) で同一座標の頂点が色ごとに分裂するので、
	 * これが無いと decode 側が座標比較で復元できず **非多様体になって volume=0 / valid=0** になる
	 * (2026-08-12 に「色つき combine を export した後で volume を採ると 0」で発覚)。
	 * Manifold の設計どおり from/to の対を持ち回る (浮動小数比較に頼らない正しい復元)。 */
	uint32_t nmg = (uint32_t)m.mergeFromVert.size();
	if ( (uint32_t)m.mergeToVert.size() < nmg ) nmg = (uint32_t)m.mergeToVert.size();
	put_u32(sink, nmg);
	for ( uint32_t i = 0 ; i < nmg ; ++i ) {
		put_u32(sink, (uint32_t)m.mergeFromVert[i]);
		put_u32(sink, (uint32_t)m.mergeToVert[i]);
	}
	mf_decode_timing("enc_done");    /* シリアライズ(sink への put)完了 */
}

/* ---- exact→float(Phase D): CGAL の MESH(cgaMeshCodec 形式・厳密有理数文字列)を double 化して読む。
 *   framing: [u32 nv][u32 nf] / 頂点×nv(各 x,y,z = [u32 len][len byte の "p/q" or 整数 文字列])/
 *            面×nf([u32 nidx][u32 idx]...)/ 色 section。CGAL 非依存で "p/q" を double へ(桁あふれ回避の
 *   スケール除算)。面は n-gon 可 → ファン三角化。頂点 index は共有済み(統合不要)。 */
static std::string get_str_field(mfChunkSource &s) {
	uint32_t len = get_u32(s);
	std::string str;
	str.resize(len);
	if ( len > 0 ) s.pull((uint8_t*)&str[0], (int)len);
	return str;
}
/* 十進整数文字列 → mantissa(|m|<1e18) と 10 の指数 e10。value = m * 10^e10。桁あふれしない。 */
static double strdec_scaled(const std::string &s, int &e10) {
	int i = 0, sign = 1;
	if ( i < (int)s.size() && (s[i]=='-' || s[i]=='+') ) { if ( s[i]=='-' ) sign = -1; i++; }
	std::string d = s.substr(i);
	size_t nz = d.find_first_not_of('0');
	if ( nz == std::string::npos ) { e10 = 0; return 0.0; }
	d = d.substr(nz);
	int total = (int)d.size();
	int keep = ( total < 18 ) ? total : 18;
	double m = 0.0;
	for ( int k = 0 ; k < keep ; ++k ) m = m * 10.0 + (double)(d[k] - '0');
	e10 = total - keep;
	return sign * m;
}
/* "p/q"(or 整数)→ double。巨大 p,q でも (mp/mq)*10^(ep-eq) で桁あふれを避ける。 */
static double parse_rational_d(const std::string &s) {
	size_t slash = s.find('/');
	if ( slash == std::string::npos ) {
		int e; double m = strdec_scaled(s, e);
		return m * ::pow(10.0, (double)e);
	}
	int en, ed;
	double mn = strdec_scaled(s.substr(0, slash), en);
	double md = strdec_scaled(s.substr(slash + 1), ed);
	if ( md == 0.0 ) return 0.0;
	return (mn / md) * ::pow(10.0, (double)(en - ed));
}

void
mfMesh::decode_mesh_exact(mfChunkSource &src)
{
	uint32_t nv = get_u32(src);
	uint32_t nf = get_u32(src);
	MeshGL64 m;
	m.numProp = 3;
	m.vertProperties.reserve((size_t)nv * 3);
	for ( uint32_t v = 0 ; v < nv ; ++v ) {
		m.vertProperties.push_back(parse_rational_d(get_str_field(src)));   /* x */
		m.vertProperties.push_back(parse_rational_d(get_str_field(src)));   /* y */
		m.vertProperties.push_back(parse_rational_d(get_str_field(src)));   /* z */
	}
	for ( uint32_t f = 0 ; f < nf ; ++f ) {
		uint32_t nidx = get_u32(src);
		std::vector<uint32_t> idx((size_t)nidx);
		for ( uint32_t j = 0 ; j < nidx ; ++j ) idx[j] = get_u32(src);
		for ( uint32_t j = 1 ; j + 1 < nidx ; ++j ) {   /* ファン三角化(共有 index) */
			m.triVerts.push_back(idx[0]);
			m.triVerts.push_back(idx[j]);
			m.triVerts.push_back(idx[j+1]);
		}
	}
	/* 色 section は読まない(必要バイトのみ pull 済み・reader が残りを閉じる)。 */
	m_ = Manifold(m);
}

/* ---- exact→float 2D (cast の cg→mf downgrade): CGAL の PLY2(cgMesh2D 形式・厳密有理数リング)を
 *   double 化して読む。framing: [u32 nregions] / 各 region: 外周 ring([u32 npts]+点(x,y=有理数文字列)) /
 *   [u32 nholes] / 穴 ring 群。末尾のガイド層は読まない(必要バイトのみ pull・reader が残りを閉じる)。
 *   CGAL Pwh は外周 CCW・穴 CW なので、そのまま NonZero で CrossSection に忠実再構成(mfCross::decode と同方針)。 */
void
mfCross::decode_cross_exact(mfChunkSource &src)
{
	uint32_t nreg = get_u32(src);
	manifold::Polygons ps;
	for ( uint32_t r = 0 ; r < nreg ; ++r ) {
		uint32_t nouter = get_u32(src);
		manifold::SimplePolygon outer;
		outer.reserve(nouter);
		for ( uint32_t i = 0 ; i < nouter ; ++i ) {
			double x = parse_rational_d(get_str_field(src));
			double y = parse_rational_d(get_str_field(src));
			outer.push_back(manifold::vec2(x, y));
		}
		ps.push_back(outer);
		uint32_t nholes = get_u32(src);
		for ( uint32_t h = 0 ; h < nholes ; ++h ) {
			uint32_t nh = get_u32(src);
			manifold::SimplePolygon hole;
			hole.reserve(nh);
			for ( uint32_t i = 0 ; i < nh ; ++i ) {
				double x = parse_rational_d(get_str_field(src));
				double y = parse_rational_d(get_str_field(src));
				hole.push_back(manifold::vec2(x, y));
			}
			ps.push_back(hole);
		}
	}
	c_ = manifold::CrossSection(ps, manifold::CrossSection::FillRule::NonZero);
}

void
mfMesh::decode(mfChunkSource &src)
{
	if ( meshExactInput_ ) { decode_mesh_exact(src); return; }   /* ★ CGAL MESH → double(Phase D) */
	mf_decode_timing("dec_start");
	uint32_t nv = get_u32(src);
	uint32_t nt = get_u32(src);
	MeshGL64 m;
	m.numProp = 3;
	m.vertProperties.resize((size_t)nv * 3);
	for ( size_t k = 0 ; k < (size_t)nv * 3 ; ++k )
		m.vertProperties[k] = get_f64(src);
	mf_decode_timing("dec_verts");   /* 頂点 pull+deserialize 完了 */
	m.triVerts.resize((size_t)nt * 3);
	for ( size_t k = 0 ; k < (size_t)nt * 3 ; ++k )
		m.triVerts[k] = get_u32(src);
	mf_decode_timing("dec_tris");    /* 三角形 index pull+deserialize 完了 */
	/* 色 section (新 cache のみ。旧 cache は more()==0 でスキップ=後方互換)。
	 * 色があれば numProp=6 に組み替え、xyz の後ろに RGB(0-255) を差し込む。 */
	if ( src.more() ) {
		uint32_t hasColor = get_u32(src);
		if ( hasColor ) {
			std::vector<double> vp((size_t)nv * 6);
			for ( uint32_t v = 0 ; v < nv ; ++v ) {
				uint32_t c = get_u32(src);
				vp[(size_t)v*6+0] = m.vertProperties[(size_t)v*3+0];
				vp[(size_t)v*6+1] = m.vertProperties[(size_t)v*3+1];
				vp[(size_t)v*6+2] = m.vertProperties[(size_t)v*3+2];
				vp[(size_t)v*6+3] = (double)((c >> 16) & 0xff);
				vp[(size_t)v*6+4] = (double)((c >> 8)  & 0xff);
				vp[(size_t)v*6+5] = (double)( c        & 0xff);
			}
			m.numProp = 6;
			m.vertProperties.swap(vp);
		}
		/* merge ベクタ (プロパティ分裂した同一座標頂点の対応)。encode と対。 */
		uint32_t nmg = get_u32(src);
		m.mergeFromVert.resize((size_t)nmg);
		m.mergeToVert.resize((size_t)nmg);
		for ( uint32_t i = 0 ; i < nmg ; ++i ) {
			m.mergeFromVert[i] = get_u32(src);
			m.mergeToVert[i]   = get_u32(src);
		}
	}
	m_ = Manifold(m);   /* MeshGL64 から再構成(座標一致で頂点マージ)*/
	mf_decode_timing("dec_manifold");/* Manifold(m) 再構築完了 */
}

/* ---- ブーリアン: Manifold の + / ^ / - ---- */
/* ---- 着色: 全頂点プロパティ ch3..5 に RGB(0-255) を入れた新 mesh (numProp=6) ----
 * cgal の per-face f:color に相当する持ち方。Manifold の変換/ブールでプロパティは運ばれる。
 * combine (Compose) で成分ごとの色が残るのが狙い (cgMesh3D::op_color と同じ用途)。 */
sPtr<mfMesh>
mfMesh::op_color(int r, int g, int b)
{
	const double rd = (double)r, gd = (double)g, bd = (double)b;
	Manifold out = m_.SetProperties(3, [rd, gd, bd](double *newProp, manifold::vec3, const double *) {
		newProp[0] = rd; newProp[1] = gd; newProp[2] = bd;
	});
	return thNEW(mfMesh,(out));
}

sPtr<mfMesh>
mfMesh::op_union(sPtr<mfMesh> b)
{
	if ( ! b.is_notNull() ) return sPtr<mfMesh>();
	return thNEW(mfMesh,(m_ + b->m_));
}

sPtr<mfMesh>
mfMesh::op_intersection(sPtr<mfMesh> b)
{
	if ( ! b.is_notNull() ) return sPtr<mfMesh>();
	return thNEW(mfMesh,(m_ ^ b->m_));
}

sPtr<mfMesh>
mfMesh::op_difference(sPtr<mfMesh> b)
{
	if ( ! b.is_notNull() ) return sPtr<mfMesh>();
	return thNEW(mfMesh,(m_ - b->m_));
}

/* ---- アフィン変換: 行優先 double[12] → Manifold::Transform(mat3x4)。
 *   e = { m00 m01 m02 tx  m10 m11 m12 ty  m20 m21 m22 tz }(cgMesh3D::apply_affine と同規約)。
 *   mat3x4 は la 列優先(4 列×vec3): 列0..2=線形部の各列・列3=平行移動。反射(det<0)の面反転は
 *   Manifold が内部で扱う。 */
sPtr<mfGeom>
mfMesh::apply_affine(const double e[12])
{
	manifold::mat3x4 m(
	    manifold::vec3(e[0], e[4], e[8]),    /* col 0 = 線形部 1 列目 */
	    manifold::vec3(e[1], e[5], e[9]),    /* col 1 */
	    manifold::vec3(e[2], e[6], e[10]),   /* col 2 */
	    manifold::vec3(e[3], e[7], e[11]));  /* col 3 = 平行移動 */
	return thNEW(mfMesh,(m_.Transform(m)));
}

/* ---- 計測 / 妥当性 ---- */
double mfMesh::op_volume() { return m_.Volume(); }
double mfMesh::op_area()   { return m_.SurfaceArea(); }

int
mfMesh::op_valid()
{
	return ( m_.Status() == Manifold::Error::NoError && ! m_.IsEmpty() ) ? 1 : 0;
}

int
mfMesh::op_bbox(double mn[3], double mx[3])
{
	manifold::Box b = m_.BoundingBox();
	mn[0] = b.min.x; mn[1] = b.min.y; mn[2] = b.min.z;
	mx[0] = b.max.x; mx[1] = b.max.y; mx[2] = b.max.z;
	return 3;
}

int
mfMesh::op_centroid(double out[3])
{
	/* 体積重心(発散定理)。各三角形を原点四面体に分け、符号付き体積で重み付け。
	 * cgMesh3D::op_centroid(四面体分割)と同型。空/退化は原点。 */
	MeshGL64 m = m_.GetMeshGL64();
	const int np = m.numProp;
	double C[3] = {0,0,0}, V = 0.0;
	size_t nt = m.triVerts.size() / 3;
	for ( size_t t = 0 ; t < nt ; ++t ) {
		const double *a = &m.vertProperties[(size_t)m.triVerts[3*t]   * np];
		const double *b = &m.vertProperties[(size_t)m.triVerts[3*t+1] * np];
		const double *c = &m.vertProperties[(size_t)m.triVerts[3*t+2] * np];
		double cr[3] = { b[1]*c[2]-b[2]*c[1], b[2]*c[0]-b[0]*c[2], b[0]*c[1]-b[1]*c[0] };
		double v = ( a[0]*cr[0] + a[1]*cr[1] + a[2]*cr[2] ) / 6.0;   /* 符号付き四面体体積 */
		V += v;
		for ( int k = 0 ; k < 3 ; ++k ) C[k] += v * (a[k] + b[k] + c[k]) / 4.0;
	}
	if ( V != 0.0 ) { out[0] = C[0]/V; out[1] = C[1]/V; out[2] = C[2]/V; }
	else            { out[0] = out[1] = out[2] = 0.0; }
	return 3;
}

/* ---- 書き出し ---- */
static bool
write_stl_bin(const char *path, const MeshGL64 &m)
{
	FILE *f = ::fopen(path, "wb");
	if ( ! f ) return false;
	char header[80]; ::memset(header, 0, sizeof header);
	::fwrite(header, 1, 80, f);
	uint32_t ntri = (uint32_t)(m.triVerts.size() / 3);
	::fwrite(&ntri, 4, 1, f);
	const int np = m.numProp;
	for ( uint32_t t = 0 ; t < ntri ; ++t ) {
		float zero[3] = {0, 0, 0};
		::fwrite(zero, 4, 3, f);                      /* normal(0=ビューア再計算)*/
		for ( int k = 0 ; k < 3 ; ++k ) {
			size_t vi = (size_t)m.triVerts[3 * t + k];
			const double *p = &m.vertProperties[vi * np];
			float xyz[3] = {(float)p[0], (float)p[1], (float)p[2]};
			::fwrite(xyz, 4, 3, f);
		}
		uint16_t attr = 0; ::fwrite(&attr, 2, 1, f);
	}
	::fclose(f);
	return true;
}

static bool
write_off_ascii(const char *path, const MeshGL64 &m)
{
	FILE *f = ::fopen(path, "wb");
	if ( ! f ) return false;
	const int np = m.numProp;
	size_t nv = m.vertProperties.size() / (np ? np : 1);
	size_t nt = m.triVerts.size() / 3;
	::fprintf(f, "OFF\n%zu %zu 0\n", nv, nt);
	for ( size_t v = 0 ; v < nv ; ++v ) {
		const double *p = &m.vertProperties[v * np];
		::fprintf(f, "%.17g %.17g %.17g\n", p[0], p[1], p[2]);
	}
	for ( size_t t = 0 ; t < nt ; ++t )
		::fprintf(f, "3 %u %u %u\n", (unsigned)m.triVerts[3*t], (unsigned)m.triVerts[3*t+1],
		          (unsigned)m.triVerts[3*t+2]);
	::fclose(f);
	return true;
}

bool
mfMesh::write_to(const char *path, const char *unit)
{
	const char *dot = ::strrchr(path, '.');
	MeshGL64 m = m_.GetMeshGL64();
	if ( dot && ( ::strcmp(dot, ".off") == 0 || ::strcmp(dot, ".OFF") == 0 ) )
		return write_off_ascii(path, m);
	if ( dot && ( ::strcmp(dot, ".stl") == 0 || ::strcmp(dot, ".STL") == 0 ) )
		return write_stl_bin(path, m);
	/* ★ 3MF / AMF は共通ライタ (common/mesh3mf.h・cgal.so と同じ実装) へ。単位を刻める形式なので
	 * unit をそのまま渡す。面色は「三角形の第 1 隅の頂点プロパティ ch3..5」から取る
	 * (color() は全頂点を同色にするので成分ごとに一様 = cgal の per-face 色と同じ見え方)。 */
	if ( dot && ( ::strcasecmp(dot, ".3mf") == 0 || ::strcasecmp(dot, ".amf") == 0 ) ) {
		srava_io::TriMesh tm;
		const int np = m.numProp;
		uint32_t nv = (uint32_t)(m.vertProperties.size() / (np ? np : 1));
		uint32_t nt = (uint32_t)(m.triVerts.size() / 3);
		tm.verts.reserve((size_t)nv * 3);
		for ( uint32_t v = 0 ; v < nv ; ++v ) {
			const double *p = &m.vertProperties[(size_t)v * np];
			tm.verts.push_back(p[0]); tm.verts.push_back(p[1]); tm.verts.push_back(p[2]);
		}
		tm.tris.reserve((size_t)nt * 3);
		for ( size_t i = 0 ; i < m.triVerts.size() ; ++i )
			tm.tris.push_back((uint32_t)m.triVerts[i]);
		if ( np >= 6 ) {
			tm.faceColor.reserve((size_t)nt);
			for ( uint32_t t = 0 ; t < nt ; ++t ) {
				const double *p = &m.vertProperties[(size_t)m.triVerts[(size_t)t*3] * np];
				tm.faceColor.push_back(pack_rgb(p[3], p[4], p[5]));
			}
		}
		return ( ::strcasecmp(dot, ".3mf") == 0 ) ? srava_io::write_3mf(path, tm, unit)
		                                          : srava_io::write_amf(path, tm, unit);
	}
	/* ★ 未知拡張子は失敗にする (深層防御・2026-08-06)。以前は黙って STL を書いており、
	 * .3mf 指定で STL の中身のファイルができていた。通常は planner (decide_out_module) が
	 * stl/off 以外を CGAL に振るのでここには来ないが、直 wire クライアントや将来の退行が
	 * 無言破損でなくエラーになるように。 */
	return false;
}

/* ---- primitive ---- */
sPtr<mfMesh>
mfMesh::box(double x, double y, double z)
{
	/* cgaBox に合わせ原点隅(0,0,0)→(x,y,z)。center=false。*/
	return thNEW(mfMesh,(Manifold::Cube(manifold::vec3(x, y, z), false)));
}

sPtr<mfMesh>
mfMesh::sphere(double r, int seg)
{
	return thNEW(mfMesh,(Manifold::Sphere(r, seg)));
}

/* 測地球 (cgal と共通アルゴリズム = src/h/common/geodesic.h)。種 (八面体/二十面体) を n 分割して
 * 球面投影し、MeshGL64 を組んで Manifold へ。cgal 側 cga_make_geodesic と頂点・面が一致するので
 * sphere/icosphere の体積が数値誤差レベルで揃う (2026-08-11 ひさ設計)。 */
namespace {
struct MfGeoSink {
	MeshGL64 m;
	MfGeoSink() { m.numProp = 3; }
	int add_vertex(double x, double y, double z) {
		int id = (int)(m.vertProperties.size() / 3);
		m.vertProperties.push_back(x);
		m.vertProperties.push_back(y);
		m.vertProperties.push_back(z);
		return id;
	}
	void add_triangle(int a, int b, int c) {
		m.triVerts.push_back((uint64_t)a);
		m.triVerts.push_back((uint64_t)b);
		m.triVerts.push_back((uint64_t)c);
	}
};
}  /* namespace */

sPtr<mfMesh>
mfMesh::geodesic(int seed, int n, double r)
{
	MfGeoSink sink;
	srava_geo::make_geodesic(seed, n, r, sink);
	return thNEW(mfMesh,(Manifold(sink.m)));
}

sPtr<mfMesh>
mfMesh::prism(int n, double h, double r)
{
	/* prism(n,h,r) ≡ extrude(ngon(n,r), h)。正 n 角形(外接円半径 r)を XY に作り z=0..h へ押し出す。
	 * cgaPrism と同じ「底面 XY・高さ Z」。頂点は角度 2πk/n(+X 始点)・CCW。 */
	if ( n < 3 ) n = 3;
	manifold::SimplePolygon poly;
	poly.reserve(n);
	for ( int k = 0 ; k < n ; ++k ) {
		double a = 2.0 * 3.14159265358979323846 * (double)k / (double)n;
		poly.push_back(manifold::vec2(r * ::cos(a), r * ::sin(a)));
	}
	manifold::Polygons polys; polys.push_back(poly);
	return thNEW(mfMesh,(Manifold::Extrude(polys, h)));
}

/* ---- 外部メッシュ読み込み(自前パーサ・CGAL 非依存)---- */
/* 頂点を座標でユニーク化して MeshGL64 を組み、Manifold(MeshGL64) にする(隣接三角形の共有頂点統合)。 */
namespace {
struct MeshBuilder {
	std::vector<double> verts;                 /* x,y,z フラット */
	std::vector<uint64_t> tris;                /* i,j,k(MeshGL64::triVerts は uint64_t) */
	std::map<std::pair<std::pair<double,double>,double>, uint32_t> idx;
	uint32_t vid(double x, double y, double z) {
		std::pair<std::pair<double,double>,double> key(std::make_pair(x,y),z);
		std::map<std::pair<std::pair<double,double>,double>, uint32_t>::iterator it = idx.find(key);
		if ( it != idx.end() ) return it->second;
		uint32_t id = (uint32_t)(verts.size()/3);
		verts.push_back(x); verts.push_back(y); verts.push_back(z);
		idx[key] = id;
		return id;
	}
	sPtr<mfMesh> build() {
		MeshGL64 m;
		m.numProp = 3;
		m.vertProperties = verts;
		m.triVerts = tris;
		return thNEW(mfMesh,(Manifold(m)));
	}
};

static bool ends_with_ci(const char *s, const char *suf) {
	size_t ls = ::strlen(s), lf = ::strlen(suf);
	if ( ls < lf ) return false;
	return ::strcasecmp(s + ls - lf, suf) == 0;
}
static bool parse_stl_binary(FILE *f, MeshBuilder &b) {
	::fseek(f, 80, SEEK_SET);
	uint32_t nt = 0;
	if ( ::fread(&nt, 4, 1, f) != 1 ) return false;
	for ( uint32_t t = 0 ; t < nt ; ++t ) {
		float buf[12];
		if ( ::fread(buf, 4, 12, f) != 12 ) return false;
		uint16_t attr; if ( ::fread(&attr, 2, 1, f) != 1 ) return false;
		uint32_t a = b.vid(buf[3], buf[4], buf[5]);
		uint32_t c = b.vid(buf[6], buf[7], buf[8]);
		uint32_t d = b.vid(buf[9], buf[10], buf[11]);
		b.tris.push_back(a); b.tris.push_back(c); b.tris.push_back(d);
	}
	return b.tris.size() > 0;
}
static bool parse_off(FILE *f, MeshBuilder &b) {
	char line[256];
	if ( ! ::fgets(line, sizeof line, f) ) return false;   /* "OFF" */
	int nv = 0, nf = 0, ne = 0;
	/* nv nf ne 行(OFF の直後・空行/コメントは無い前提の単純版) */
	if ( ::fscanf(f, "%d %d %d", &nv, &nf, &ne) != 3 ) return false;
	std::vector<uint32_t> vmap((size_t)nv);
	for ( int i = 0 ; i < nv ; ++i ) {
		double x, y, z;
		if ( ::fscanf(f, "%lf %lf %lf", &x, &y, &z) != 3 ) return false;
		vmap[i] = b.vid(x, y, z);
	}
	for ( int i = 0 ; i < nf ; ++i ) {
		int cnt = 0;
		if ( ::fscanf(f, "%d", &cnt) != 1 ) return false;
		std::vector<int> fv((size_t)cnt);
		for ( int j = 0 ; j < cnt ; ++j )
			if ( ::fscanf(f, "%d", &fv[j]) != 1 ) return false;
		/* 三角形ファン分割 */
		for ( int j = 1 ; j + 1 < cnt ; ++j ) {
			b.tris.push_back(vmap[fv[0]]);
			b.tris.push_back(vmap[fv[j]]);
			b.tris.push_back(vmap[fv[j+1]]);
		}
	}
	return b.tris.size() > 0;
}
static bool parse_stl_ascii(FILE *f, MeshBuilder &b) {
	char tok[128];
	double vv[9]; int vn = 0;
	while ( ::fscanf(f, "%127s", tok) == 1 ) {
		if ( ::strcmp(tok, "vertex") == 0 ) {
			if ( ::fscanf(f, "%lf %lf %lf", &vv[vn], &vv[vn+1], &vv[vn+2]) != 3 ) return false;
			vn += 3;
			if ( vn == 9 ) {
				uint32_t a = b.vid(vv[0], vv[1], vv[2]);
				uint32_t c = b.vid(vv[3], vv[4], vv[5]);
				uint32_t d = b.vid(vv[6], vv[7], vv[8]);
				b.tris.push_back(a); b.tris.push_back(c); b.tris.push_back(d);
				vn = 0;
			}
		}
	}
	return b.tris.size() > 0;
}
} /* anon namespace */

sPtr<mfMesh>
mfMesh::import_file(const char *path)
{
	FILE *f = ::fopen(path, "rb");
	if ( ! f ) return sPtr<mfMesh>();
	MeshBuilder b;
	bool ok = false;
	if ( ends_with_ci(path, ".off") ) {
		ok = parse_off(f, b);
	} else {
		/* STL: 先頭 5 バイトが "solid" でも binary のことがある(多くのツールが binary に solid ヘッダ)。
		 * サイズ整合(84 + nt*50)で binary を優先判定し、崩れたら ascii。 */
		::fseek(f, 0, SEEK_END); long sz = ::ftell(f);
		::fseek(f, 80, SEEK_SET);
		uint32_t nt = 0;
		bool isBin = ( ::fread(&nt, 4, 1, f) == 1 ) && ( sz == (long)(84 + (long)nt * 50) );
		::fseek(f, 0, SEEK_SET);
		ok = isBin ? parse_stl_binary(f, b) : parse_stl_ascii(f, b);
	}
	::fclose(f);
	if ( ! ok ) return sPtr<mfMesh>();
	return b.build();
}

/* ---- reader 用ファクトリ: D_META タグ → 具体型(mfMesh/mfCross)---- */
sPtr<mfGeom>
mfGeom::create_for_meta(const uint8_t *meta, int len)
{
	if ( len == 4 && ::memcmp(meta, "MFM3", 4) == 0 )
		return thNEW(mfMesh,(Manifold()));
	if ( len == 4 && ::memcmp(meta, "MFC2", 4) == 0 )
		return thNEW(mfCross,(manifold::CrossSection()));
	/* ★ Phase D: CGAL の 3D exact mesh "MESH" も受理し、decode 時に有理数文字列→double で Manifold 化
	 *   (cast("manifold", exactMesh) の損失変換。cg agent 非依存=文字列パースのみ)。 */
	if ( len == 4 && ::memcmp(meta, "MESH", 4) == 0 ) {
		sPtr<mfMesh> m = thNEW(mfMesh,(Manifold()));
		m->set_mesh_exact_input();
		return m;
	}
	/* ★ cast downgrade 2D: CGAL の 2D exact "PLY2" も受理し、decode 時に有理数リング→double で
	 *   CrossSection 化(cast("mf-cross2d", cgCross) の損失変換)。 */
	if ( len == 4 && ::memcmp(meta, "PLY2", 4) == 0 ) {
		sPtr<mfCross> c = thNEW(mfCross,(manifold::CrossSection()));
		c->set_cross_exact_input();
		return c;
	}
	return sPtr<mfGeom>();
}
