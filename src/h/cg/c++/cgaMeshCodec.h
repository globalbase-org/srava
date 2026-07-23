#ifndef CGA_MESH_CODEC_H
#define CGA_MESH_CODEC_H
/*
 * cgaMeshCodec — Surface_mesh(EPECK) ⇄ pigwire D_CHUNK ストリームの厳密シリアライズ(確認①)。
 *
 * 中間 blob を持たない(ひさレビュー 2026-06-06):
 *   - encode は **Sink**(writer)へ put_u32/put_coord が直接 chunk() = d_chunk を呼ぶ。
 *   - decode は **Source**(reader)から get_u32/get_coord が pull() でチャンク境界をまたいで取る。
 *   どちらも mesh 本体 + 高々 1 チャンク(+ 座標 1 個分の小さな文字列)しかメモリに乗らない。
 *
 * Sink 要件   : void chunk(const uint8_t* data, int n)
 * Source 要件 : void pull(uint8_t* dst, int n)   (足りなければ次の D_CHUNK を待ち pull する)
 *
 * フレーミング(little-endian u32):
 *   [u32 nv][u32 nf]
 *   頂点 × nv: 各座標 x,y,z を [u32 len][len byte の厳密有理数文字列]
 *   面   × nf: [u32 nidx][u32 idx]...   (頂点登録順インデックス)
 *
 * 座標は CGAL::exact(c) を operator<< で "p/q"(or 整数)に書き、operator>> で厳密に復元する
 * (OFF テキストと同じ厳密往復機構をバイナリ枠で運ぶ → double 化せずロバスト性維持)。
 * mesh の走査/構築は Surface_mesh の API を直接使う(polygon soup への全体コピーも作らない)。
 *
 * CGAL を #include する .cpp(cgaBox / cgaUnion / ...WriterMesh / ...ReaderMesh = srava_agent)専用。
 */
#include	<CGAL/Kernel_traits.h>
#include	<CGAL/number_utils.h>
#include	<CGAL/Lazy_exact_nt.h>    /* CGAL::exact(EPECK::FT) */
#include	<CGAL/boost/graph/iterator.h>   /* CGAL::vertices_around_face */
#include	<CGAL/IO/Color.h>               /* 面色(f:color)の codec */
#include	<vector>
#include	<string>
#include	<sstream>
#include	<cstddef>
#include	<optional>
#include	<stdint.h>

namespace cgaMeshCodec {

/* ---- encode: Sink へ直接ストリーム(d_chunk) ---- */

template<class Sink>
inline void put_u32(Sink& sink, uint32_t v) {
	uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
	sink.chunk(b, 4);
}
template<class Sink, class FT>
inline void put_coord(Sink& sink, const FT& c) {
	std::ostringstream os;
	os << CGAL::exact(c);                  /* 厳密値を "p/q" or 整数で */
	std::string s = os.str();
	put_u32(sink, (uint32_t)s.size());
	if ( !s.empty() ) sink.chunk((const uint8_t*)s.data(), (int)s.size());
}

template<class Mesh, class Sink>
inline void encode(Mesh& m, Sink& sink) {
	typedef typename Mesh::Vertex_index VI;
	m.collect_garbage();                   /* 削除要素を詰めて idx を 0..nv-1 連続に */
	put_u32(sink, (uint32_t)m.number_of_vertices());
	put_u32(sink, (uint32_t)m.number_of_faces());
	for ( VI v : m.vertices() ) {
		const typename Mesh::Point& p = m.point(v);
		put_coord(sink, p.x());
		put_coord(sink, p.y());
		put_coord(sink, p.z());
	}
	for ( typename Mesh::Face_index f : m.faces() ) {
		std::vector<uint32_t> vis;         /* この面の頂点だけ(小さい) */
		for ( VI v : CGAL::vertices_around_face(m.halfedge(f), m) )
			vis.push_back((uint32_t)(std::size_t)v);
		put_u32(sink, (uint32_t)vis.size());
		for ( std::size_t j = 0 ; j < vis.size() ; ++j )
			put_u32(sink, vis[j]);
	}
	/* 面色(f:color)があれば 1 + 各面 RGB(u32 packed 0xRRGGBB)。無ければ 0。
	 * 旧 cache はこの section 自体が無く、decode は src.more() で読むか判定(後方互換)。 */
	std::optional<typename Mesh::template Property_map<typename Mesh::Face_index, CGAL::IO::Color> > fc
	    = m.template property_map<typename Mesh::Face_index, CGAL::IO::Color>("f:color");
	if ( fc.has_value() ) {
		put_u32(sink, 1u);
		for ( typename Mesh::Face_index f : m.faces() ) {
			const CGAL::IO::Color& c = (*fc)[f];
			put_u32(sink, ((uint32_t)c.red() << 16) | ((uint32_t)c.green() << 8) | (uint32_t)c.blue());
		}
	} else {
		put_u32(sink, 0u);
	}
}

/* ---- decode: Source から pull(チャンク境界をまたぐ) ---- */

template<class Source>
inline uint32_t get_u32(Source& src) {
	uint8_t b[4];
	src.pull(b, 4);
	return (uint32_t)b[0] | ((uint32_t)b[1] << 8)
	     | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}
template<class Source, class FT>
inline FT get_coord(Source& src) {
	uint32_t len = get_u32(src);
	std::string s;
	s.resize(len);
	if ( len > 0 ) src.pull((uint8_t*)&s[0], (int)len);
	std::istringstream is(s);
	FT v;
	is >> v;                               /* 厳密に復元(OFF と同じ機構) */
	return v;
}

template<class Mesh, class Source>
inline void decode(Source& src, Mesh& m) {
	typedef typename Mesh::Point Point;
	typedef typename Mesh::Vertex_index VI;
	typedef typename CGAL::Kernel_traits<Point>::Kernel K;
	typedef typename K::FT FT;
	m.clear();
	uint32_t nv = get_u32(src);
	uint32_t nf = get_u32(src);
	std::vector<VI> vmap;                  /* 登録順 idx → Vertex_index(int 程度。mesh の付随情報) */
	vmap.reserve(nv);
	for ( uint32_t i = 0 ; i < nv ; ++i ) {
		FT x = get_coord<Source,FT>(src);
		FT y = get_coord<Source,FT>(src);
		FT z = get_coord<Source,FT>(src);
		vmap.push_back(m.add_vertex(Point(x, y, z)));
	}
	for ( uint32_t i = 0 ; i < nf ; ++i ) {
		uint32_t cnt = get_u32(src);
		std::vector<VI> face;              /* この面の頂点だけ */
		face.reserve(cnt);
		for ( uint32_t j = 0 ; j < cnt ; ++j )
			face.push_back(vmap[get_u32(src)]);
		m.add_face(face);
	}
	/* 面色 section(新 cache のみ。旧 cache は src.more()==0 でスキップ=後方互換)。 */
	if ( src.more() ) {
		uint32_t hasColor = get_u32(src);
		if ( hasColor ) {
			typename Mesh::template Property_map<typename Mesh::Face_index, CGAL::IO::Color> fc
			    = m.template add_property_map<typename Mesh::Face_index, CGAL::IO::Color>(
			          "f:color", CGAL::IO::Color((unsigned char)180,(unsigned char)180,(unsigned char)180)).first;
			for ( uint32_t i = 0 ; i < nf ; ++i ) {
				uint32_t packed = get_u32(src);
				fc[ typename Mesh::Face_index(i) ] = CGAL::IO::Color(
				    (unsigned char)((packed >> 16) & 0xff),
				    (unsigned char)((packed >> 8) & 0xff),
				    (unsigned char)(packed & 0xff));
			}
		}
	}
}

} /* namespace cgaMeshCodec */

#endif /* CGA_MESH_CODEC_H */
