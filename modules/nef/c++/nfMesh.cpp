/*
 * nfMesh — CGAL Nef_polyhedron_3 の値ハンドル実装 (#3433 P1)。
 * ブールは Nef のまま(型維持)。cache は SNC で書くので、境界表現へ落とすのは
 * volume / export (= to_mesh) の時だけ。
 */
#include	"nf/c++/nfMesh.h"
#include	"cg/c++/cgaMeshCodec.h"        /* 厳密境界の共通 codec (cg の "MESH" と同一形式) */
#include	"ts2/c++/stdString.h"

#include	<CGAL/IO/Nef_polyhedron_iostream_3.h>   /* ★SNC シリアライズの**定義**はここ (宣言は Nef_polyhedron_3.h) */
#include	<CGAL/minkowski_sum_3.h>                /* Minkowski 和 (内部で凸分解・#3440) */
#include	<CGAL/boost/graph/convert_nef_polyhedron_to_polygon_mesh.h>
#include	<CGAL/Polygon_mesh_processing/triangulate_faces.h>
#include	<CGAL/boost/graph/IO/polygon_mesh_io.h>
#include	<CGAL/boost/graph/generators.h>          /* make_icosahedron / make_hexahedron (offset の球と箱) */
#include	<CGAL/subdivision_method_3.h>            /* Loop 細分 (近似球) */
#include	<CGAL/Polygon_mesh_processing/connected_components.h>   /* シェル分解 (空洞の復元・#3440) */
#include	<CGAL/Polygon_mesh_processing/measure.h>                /* 符号付き体積 (向きの判定) */
#include	<CGAL/Polygon_mesh_processing/orientation.h>            /* reverse_face_orientations */
#include	<CGAL/convex_decomposition_3.h>                         /* 凸分解 (#3441) */
#include	<CGAL/Polyhedron_3.h>                                   /* convert_inner_shell_to_polyhedron の受け皿 */
#include	<CGAL/boost/graph/copy_face_graph.h>
#include	<CGAL/Side_of_triangle_mesh.h>   /* シェルの入れ子判定 (#3441/#3442) */
#include	<CGAL/Nef_nary_union_3.h>             /* 面ごとの Nef を n 項 union (#3445) */
#include	<CGAL/normal_vector_newell_3.h>       /* 面の法線 (Nef の平面向き) */
#include	<CGAL/Nef_3/Mark_bounded_volumes.h>   /* 有界セルを「中身」にする (#3445) */
#include	<algorithm>
#include	<stdexcept>
#include	<vector>
#include	<cmath>

#include	<string.h>
#include	<sstream>

/* ---- reader 用ファクトリ: 4CC タグ → 具体型 ----
 * "NEF3" = 自型 (SNC) / "MESH" = cg の厳密境界 (昇格読み → decode_boundary)。 */
sPtr<nfGeom>
nfGeom::create_for_meta(const uint8_t *meta, int len)
{
	if ( meta == 0 || len != 4 ) return thNULL;
	/* ★自分の 4CC と、**もう一方の nef モジュールの 4CC** の両方を読む (ひさ方針: reader は
	 *   どちらにも対応)。形式は payload 先頭バイトが自己記述するので decode は共通。 */
	if ( ::memcmp(meta, "NEF3", 4) == 0 ) return sPtr<nfGeom>::d_cast(thNEW(nfMesh,()));
	if ( ::memcmp(meta, "NEFB", 4) == 0 ) return sPtr<nfGeom>::d_cast(thNEW(nfMesh,()));
	if ( ::memcmp(meta, "MESH", 4) == 0 ) {          /* cg の厳密境界 → 昇格読み */
		sPtr<nfMesh> m = thNEW(nfMesh,());
		m->set_boundary_input();
		return sPtr<nfGeom>::d_cast(m);
	}
	if ( ::memcmp(meta, "MFM3", 4) == 0 ) {          /* Manifold の raw double mesh → 昇格読み */
		sPtr<nfMesh> m = thNEW(nfMesh,());
		m->set_mfm3_input();
		return sPtr<nfGeom>::d_cast(m);
	}
	return thNULL;
}

sPtr<stdString>
nfMesh::get_str()
{
	std::ostringstream os;
	os << "nef3(volumes=" << n_.number_of_volumes()
	   << ",facets="      << n_.number_of_facets()
	   << (n_.is_simple() ? "" : ",non-simple") << ")";
	std::string s = os.str();
	return thNEW(stdString,(s.c_str()));
}

/* ---- cache 書き出し: SNC そのもの ([u32 len][SNC テキスト]) ----
 * ★境界表現で書くと非有界な Nef (箱の補集合など) が「箱」に化ける。SNC なら非有界・低次元も
 *   厳密に往復する (nfMesh.h 冒頭の実測メモ参照)。 */
void
nfMesh::encode(nfChunkSink &sink)
{
	/* ★ハイブリッド (#3433): 普通の立体 (有界かつ 2-多様体) は **厳密境界**で書き、
	 *   Nef 固有の値 (非有界/非多様体) だけ SNC で書く。形式は payload 先頭 1 バイトで自己記述する
	 *   (4CC を増やさない = 型↔タグ 1:1 の不変条件を壊さない・routing 無傷)。
	 *   狙い: (a) 普通の立体で cache が cg 並みに小さくなる (SNC は ~18x 太い)
	 *         (b) 境界形式は **cgal も manifold もそのまま読める** (mf は CGAL 非依存のまま nf を消費可) */
#if defined(NF_WIRE_HYBRID)
	/* ★ 空洞 (中空立体) の扱い (#3440): 境界形式は「面の集まり」なので、素朴に読み戻すと
	 *   入れ子シェルが「もう 1 つの立体」になり **空洞が中実に化ける** (中空箱が 26 → 27)。
	 *   → 読み側 (@set_from_mesh@) を **シェルごとの Nef を対称差 (even-odd) で畳む**よう直したので、
	 *     空洞つき立体も境界形式で安全に運べる。cg / mf への降格も通る (両者とも中空立体を表現できる)。
	 *   ★ここを「空洞があれば SNC」に逃がすと cache が太り、cg/mf へ渡せなくなるので**しない**。 */
	uint8_t form = ( is_bounded() && n_.is_simple() ) ? NF_FORM_BOUNDARY : NF_FORM_SNC;
#else
	uint8_t form = NF_FORM_SNC;   /* nef_snc: 常に SNC (Nef 本来の表現をそのまま持ち回る) */
#endif
	sink.chunk(&form, 1);
	if ( form == NF_FORM_BOUNDARY ) {
		struct Adapt { nfChunkSink *s; void chunk(const uint8_t *d, int n) { s->chunk(d, n); } } a;
		a.s = &sink;
		Mesh m;
		to_mesh(m);
		cgaMeshCodec::encode(m, a);
		return;
	}
	std::ostringstream os;
	os << n_;
	std::string s = os.str();
	uint32_t n = (uint32_t)s.size();
	uint8_t b[4] = { (uint8_t)n, (uint8_t)(n >> 8), (uint8_t)(n >> 16), (uint8_t)(n >> 24) };
	sink.chunk(b, 4);
	if ( n > 0 ) sink.chunk((const uint8_t*)s.data(), (int)n);
}

void
nfMesh::decode(nfChunkSource &src)
{
	if ( boundaryInput_ ) { decode_boundary(src); return; }   /* "MESH" → 境界から SNC 構築 */
	if ( mfm3Input_ )     { decode_mfm3(src);     return; }   /* "MFM3" → raw double から SNC 構築 */
	uint8_t form = NF_FORM_SNC;
	src.pull(&form, 1);                                       /* ★形式バイト (encode 参照) */
	if ( form == NF_FORM_BOUNDARY ) { decode_boundary(src); return; }
	uint8_t b[4];
	src.pull(b, 4);
	uint32_t n = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
	std::string s;
	s.resize(n);
	if ( n > 0 ) src.pull((uint8_t*)&s[0], (int)n);
	std::istringstream is(s);
	is >> n_;
}

/* ---- "MESH" (cg の厳密境界・cgaMeshCodec フレーミング) を読んで SNC を組む = 昇格読み ---- */
void
nfMesh::decode_boundary(nfChunkSource &src)
{
	struct Adapt { nfChunkSource *s;
	               void pull(uint8_t *d, int n) { s->pull(d, n); }
	               int  more() { return s->more(); } } a;
	a.s = &src;
	Mesh m;
	cgaMeshCodec::decode(a, m);
	set_from_mesh(m);
}

/* ---- "MFM3" (Manifold の raw double mesh) を読んで SNC を組む = 昇格読み ----
 * framing (mfMesh::encode と一致・全 little-endian): [u32 nv][u32 nt] + nv×(3×f64 頂点) +
 *   nt×(3×u32 三角形 index)。★double → EPECK は**無損失** (double は 2 進有理数)。
 *   mfMesh.h には依存しない (framing は安定契約としてここに inline 再現 = cgMesh3D::decode_mfm3 と同じ作法)。
 *   Manifold は一貫した外向き巻きで watertight を保証するので add_face は成功する。 */
void
nfMesh::decode_mfm3(nfChunkSource &src)
{
	uint8_t b4[4];
	src.pull(b4, 4);
	uint32_t nv = (uint32_t)b4[0] | ((uint32_t)b4[1]<<8) | ((uint32_t)b4[2]<<16) | ((uint32_t)b4[3]<<24);
	src.pull(b4, 4);
	uint32_t nt = (uint32_t)b4[0] | ((uint32_t)b4[1]<<8) | ((uint32_t)b4[2]<<16) | ((uint32_t)b4[3]<<24);
	Mesh m;
	std::vector<Mesh::Vertex_index> vmap;
	vmap.reserve(nv);
	for ( uint32_t i = 0 ; i < nv ; ++i ) {
		double xyz[3];
		for ( int k = 0 ; k < 3 ; ++k ) {
			uint8_t b8[8];
			src.pull(b8, 8);
			::memcpy(&xyz[k], b8, 8);
		}
		vmap.push_back(m.add_vertex(Point_3(K::FT(xyz[0]), K::FT(xyz[1]), K::FT(xyz[2]))));
	}
	for ( uint32_t t = 0 ; t < nt ; ++t ) {
		uint32_t idx[3];
		for ( int k = 0 ; k < 3 ; ++k ) {
			src.pull(b4, 4);
			idx[k] = (uint32_t)b4[0] | ((uint32_t)b4[1]<<8) | ((uint32_t)b4[2]<<16) | ((uint32_t)b4[3]<<24);
		}
		m.add_face(vmap[idx[0]], vmap[idx[1]], vmap[idx[2]]);
	}
	set_from_mesh(m);
}

/* ---- 境界メッシュから Nef を作る (例外を明示エラーへ) ----
 * ★**自己交差したメッシュ** (自分を貫く tube など) を渡すと CGAL の Nef 構築は前提を満たさず、
 *   assertion で落ちる = agent プロセスごと死ぬ ("agent closed unexpectedly" になり原因が分からない)。
 *   CGAL の assertion は既定で例外を投げるので、ここで受けて **null を返し呼び側が明示エラー**にする。
 *   ★事前に @does_self_intersect@ で弾く手もあるが、正常なメッシュにも O(n log n) の検査が乗るので
 *     採らない (seg=300 のベンチが重くなる)。壊れた入力のときだけ払う形にする。 */
static int
nf_try_build(nfMesh::Mesh &m, nfMesh::Nef &out)
{
	try {
		out = nfMesh::Nef(m);
		return 1;
	} catch ( const std::exception & ) {
		return 0;   /* 自己交差など Nef の前提を満たさない */
	} catch ( ... ) {
		return 0;
	}
}

/* ---- 面ごとの Nef を n 項 union し、有界セルを mark する (#3445) ----
 * これが「壊れた境界から立体を作る」中核。面は 2 次元 (体積ゼロ) の Nef になるので、union だけでは
 * **表面のまま**だが、その表面が空間をセルに切る。最後に @Mark_bounded_volumes@ で **有界セルを
 * 塗る**と立体になる。交差線は union の過程で実エッジとして入るので、自己交差がここで解ける。
 * ★コストは面数に比例して Nef の union を繰り返す = **重い** (通常の Nef 構築より桁で重い)。
 *   だから既定の変換経路には決して置かない。明示 op からのみ。 */
sPtr<nfMesh>
nfMesh::build_from_facets(Mesh &m)
{
	CGAL::Nef_nary_union_3<Nef> nary;
	int added = 0;
	for ( Mesh::Face_index f : m.faces() ) {
		std::vector<Point_3> pts;
		for ( Mesh::Vertex_index v : CGAL::vertices_around_face(m.halfedge(f), m) )
			pts.push_back(m.point(v));
		if ( pts.size() < 3 ) continue;
		K::Vector_3 normal;
		CGAL::normal_vector_newell_3(pts.begin(), pts.end(), normal);
		if ( normal == CGAL::NULL_VECTOR ) continue;   /* 退化面は捨てる */
		Nef one(pts.begin(), pts.end(), normal);
		if ( one.is_empty() ) continue;
		nary.add_polyhedron(one);
		++added;
	}
	if ( added == 0 ) return thNULL;
	Nef u = nary.get_union();
	CGAL::Mark_bounded_volumes<Nef> mbv(true);
	u.delegate(mbv);                                   /* ★有界セルを「中身」にする */
	return thNEW(nfMesh,(u));
}

/* ---- solidify の本体 (#3445) ----
 * 連結成分ごとに「面ごとの Nef を union → 有界セルを mark」し、成分どうしは **入れ子の深さ** で
 * 合成する。深さ合成が無いと @Mark_bounded_volumes@ が空洞まで塗って中空箱が 26 → 27 になる。
 * ★入力は nf のみ。自己交差した閉メッシュも cg→nf の厳密変換は **通る** (Nef 構築は面どうしの
 *   交差を検査せず局所の接続だけから SNC を組む) ので、壊れた形のまま入っていてよい。 */
sPtr<nfMesh>
nfMesh::solidify_mesh(sPtr<pigData> in)
{
	namespace PMP = CGAL::Polygon_mesh_processing;
	sPtr<nfMesh> nf = sPtr<nfMesh>::d_cast(in);
	if ( ! nf.is_notNull() )        return thNULL;
	Mesh m;
	if ( ! nf->to_mesh(m) )         return thNULL;   /* 非有界などは呼び側がエラーに */
	if ( m.number_of_faces() == 0 ) return thNULL;

	Mesh::Property_map<Mesh::Face_index, std::size_t> ccmap =
	    m.add_property_map<Mesh::Face_index, std::size_t>("f:nfSol", 0).first;
	std::size_t nComp = PMP::connected_components(m, ccmap);
	m.remove_property_map(ccmap);
	if ( nComp <= 1 ) return build_from_facets(m);

	/* 成分ごとに立体化 → 深さで合成 (外殻は和・空洞は差)。 */
	std::vector<Mesh> parts;
	PMP::split_connected_components(m, parts);
	std::vector<Mesh> solids;      /* 深さ判定に使う「立体化した成分の境界」 */
	std::vector<Nef>  nefs;
	for ( std::size_t i = 0 ; i < parts.size() ; ++i ) {
		sPtr<nfMesh> one = build_from_facets(parts[i]);
		if ( ! one.is_notNull() ) continue;
		Mesh b;
		if ( ! one->to_mesh(b) || b.number_of_faces() == 0 ) continue;
		solids.push_back(b);
		nefs.push_back(one->nef());
	}
	if ( nefs.empty() ) return thNULL;

	std::vector<int> depth(solids.size(), 0);
	for ( std::size_t a = 0 ; a < solids.size() ; ++a ) {
		if ( solids[a].number_of_vertices() == 0 ) continue;
		K::Point_3 probe = solids[a].point(*solids[a].vertices().begin());
		for ( std::size_t b = 0 ; b < solids.size() ; ++b ) {
			if ( a == b ) continue;
			CGAL::Side_of_triangle_mesh<Mesh, K> inside(solids[b]);
			if ( inside(probe) == CGAL::ON_BOUNDED_SIDE ) ++depth[a];
		}
	}
	std::vector<std::size_t> order(solids.size());
	for ( std::size_t i = 0 ; i < order.size() ; ++i ) order[i] = i;
	std::sort(order.begin(), order.end(),
	          [&depth](std::size_t x, std::size_t y) { return depth[x] < depth[y]; });
	Nef acc;
	for ( std::size_t k = 0 ; k < order.size() ; ++k ) {
		std::size_t a = order[k];
		if ( depth[a] % 2 == 0 ) acc = acc + nefs[a];
		else                     acc = acc - nefs[a];
	}
	return thNEW(nfMesh,(acc));
}

/* ---- 閉じたシェルの集まりを 1 つの Nef へ畳む (#3440/#3441/#3442) ----
 * 境界表現は「面の集まり」でしかないので、そのまま @Nef_polyhedron_3(Mesh)@ に渡すと
 * **入れ子シェルまで和で取り込まれ、空洞が中実に化ける** (中空箱が 26 → 27 になった)。
 * → シェルごとに Nef を作り、**入れ子の深さ**で足し引きする:
 *     深さ 0 (どのシェルにも入っていない) = 立体   → 和
 *     深さ 1 (どれか 1 つの中)             = 空洞   → 差
 *     深さ 2                               = 空洞の中の立体 → 和 … と交互
 *   深さの昇順に処理すると、外側から順に「足す・くり抜く」形になって正しく組める。
 * ★XOR (even-odd) ではいけない: 面を接して並ぶ 2 つの立体で、共有する壁が**両方に属する**ため
 *   対称差から落ち、Nef に 2 次元のスリットが残る (体積は合うが @is_simple()@ が偽になり、
 *   cg/mf へ降格できなくなる)。和なら壁が溶けて 1 つの立体になる。
 * ★深さは「シェル i の頂点がシェル j の**厳密な内側**か」で数える。接しているだけの頂点は
 *   境界上と判定されるので数に入らない (= 接する立体は両方とも深さ 0 = 和になる)。 */
static void
nf_combine_shells(std::vector<nfMesh::Mesh> &shells, nfMesh::Nef &out)
{
	namespace PMP = CGAL::Polygon_mesh_processing;
	typedef nfMesh::Mesh Mesh;
	typedef nfMesh::Nef  Nef;
	typedef nfMesh::K    K;

	std::vector<int> keep;
	for ( std::size_t i = 0 ; i < shells.size() ; ++i ) {
		if ( shells[i].number_of_faces() == 0 || ! CGAL::is_closed(shells[i]) ) continue;
		if ( PMP::volume(shells[i]) < K::FT(0) )
			PMP::reverse_face_orientations(shells[i]);   /* 向きを外向きへ揃える */
		keep.push_back((int)i);
	}

	/* 入れ子の深さ (自分を厳密に含むシェルの数)。 */
	std::vector<int> depth(keep.size(), 0);
	for ( std::size_t a = 0 ; a < keep.size() ; ++a ) {
		Mesh &mi = shells[keep[a]];
		if ( mi.number_of_vertices() == 0 ) continue;
		K::Point_3 probe = mi.point(*mi.vertices().begin());
		for ( std::size_t b = 0 ; b < keep.size() ; ++b ) {
			if ( a == b ) continue;
			CGAL::Side_of_triangle_mesh<Mesh, K> inside(shells[keep[b]]);
			if ( inside(probe) == CGAL::ON_BOUNDED_SIDE ) ++depth[a];
		}
	}

	/* 深さの昇順に「和 / 差」を交互に適用する。 */
	std::vector<std::size_t> order(keep.size());
	for ( std::size_t i = 0 ; i < order.size() ; ++i ) order[i] = i;
	std::sort(order.begin(), order.end(),
	          [&depth](std::size_t x, std::size_t y) { return depth[x] < depth[y]; });

	out = Nef();
	for ( std::size_t k = 0 ; k < order.size() ; ++k ) {
		std::size_t a = order[k];
		Nef one;
		if ( ! nf_try_build(shells[keep[a]], one) ) continue;   /* 壊れたシェルは飛ばす */
		if ( depth[a] % 2 == 0 ) out = out + one;   /* 立体 = 和 */
		else                     out = out - one;   /* 空洞 = 差 */
	}
}

/* ---- Surface_mesh → Nef ----
 * ★連結成分が 1 個なら従来どおり (数え上げは union-find で、Nef 構築に比べれば無視できる)。
 *   複数シェルのときだけ上の nf_combine_shells で入れ子を解く。 */
void
nfMesh::set_from_mesh(Mesh &m)
{
	namespace PMP = CGAL::Polygon_mesh_processing;
	if ( m.number_of_faces() == 0 ) { n_ = Nef(); return; }   /* 空集合 */

	Mesh::Property_map<Mesh::Face_index, std::size_t> ccmap =
	    m.add_property_map<Mesh::Face_index, std::size_t>("f:nfCC", 0).first;
	std::size_t nComp = PMP::connected_components(m, ccmap);
	m.remove_property_map(ccmap);
	if ( nComp <= 1 || ! CGAL::is_closed(m) ) {
		if ( ! nf_try_build(m, n_) ) { n_ = Nef(); buildErr_ = 1; }
		return;
	}

	std::vector<Mesh> shells;
	PMP::split_connected_components(m, shells);
	nf_combine_shells(shells, n_);
}

/* ---- 有界性: SNC の最初の volume(無限体積)が mark されていれば非有界 ---- */
bool
nfMesh::is_bounded() const
{
	Nef::Volume_const_iterator ci = n_.volumes_begin();
	if ( ci == n_.volumes_end() ) return true;   /* 空集合は有界扱い */
	return ! ci->mark();
}

/* ---- Nef → Surface_mesh (境界表現)。非有界/非 2-多様体は false ----
 * ★is_simple() だけでは足りない: 箱の補集合は is_simple()==true だが非有界で、境界だけ
 *   書き出すと体積 8 の箱に化ける (黙って嘘の値を返すことになる)。is_bounded() を先に見る。 */
bool
nfMesh::to_mesh(Mesh &out)
{
	if ( ! is_bounded() ) return false;
	if ( n_.is_simple() ) {
		CGAL::convert_nef_polyhedron_to_polygon_mesh(n_, out, true /* 三角化 */);
		return true;
	}
	/* ★ 仕切り面を持つ Nef (凸分解の結果など・#3441) は境界が 2-多様体でないので上の変換は使えない。
	 *   **marked な volume ごとに外側シェルを取り出し**、別々の連結成分として 1 つの Mesh に束ねる。
	 *   こうすると volume は片の合計 (= 分解前と同じ)・export は片が別成分として出る。
	 *   ★最初の volume は無限体積なので飛ばす (is_bounded() で mark 無しは確認済み)。 */
	typedef CGAL::Polyhedron_3<K> Poly;
	int nPart = 0;
	Nef::Volume_const_iterator ci = n_.volumes_begin();
	for ( ++ci ; ci != n_.volumes_end() ; ++ci ) {
		if ( ! ci->mark() ) continue;
		Poly p;
		n_.convert_inner_shell_to_polyhedron(ci->shells_begin(), p);
		if ( p.size_of_facets() == 0 ) continue;
		Mesh part;
		CGAL::copy_face_graph(p, part);
		CGAL::Polygon_mesh_processing::triangulate_faces(part);
		CGAL::copy_face_graph(part, out);   /* 別連結成分として追記 */
		++nPart;
	}
	return nPart > 0;
}

bool
nfMesh::write_to(const char *path, const char *)
{
	Mesh m;
	if ( ! to_mesh(m) ) return false;         /* 非有界/非多様体は書けない = 黙らずに失敗 */
	return CGAL::IO::write_polygon_mesh(path, m, CGAL::parameters::stream_precision(17));
}

/* ---- ブール: Nef のまま (overlay + 選択関数)。型維持 ---- */
sPtr<nfMesh>
nfMesh::op_union(sPtr<nfMesh> o)
{
	if ( ! o.is_notNull() ) return thNULL;
	return thNEW(nfMesh,(n_ + o->nef()));
}

sPtr<nfMesh>
nfMesh::op_intersection(sPtr<nfMesh> o)
{
	if ( ! o.is_notNull() ) return thNULL;
	return thNEW(nfMesh,(n_ * o->nef()));
}

sPtr<nfMesh>
nfMesh::op_difference(sPtr<nfMesh> o)
{
	if ( ! o.is_notNull() ) return thNULL;
	return thNEW(nfMesh,(n_ - o->nef()));
}

sPtr<nfMesh>
nfMesh::op_complement()
{
	return thNEW(nfMesh,(n_.complement()));
}

/* ---- Minkowski 和 A ⊕ B (#3440) ----
 * ★CGAL::minkowski_sum_3 は引数を **非 const 参照**で取り、非凸なら**入力自身を凸分解で書き換える**
 *   (ヘッダの \post に明記)。ここで渡すのは cache 由来の共有された値なので、**ハンドルのコピー**を
 *   作って渡す。Nef_polyhedron_3 は Handle_for なので、コピーは rep 共有だが
 *   convex_decomposition_3 が通る delegate() が `if (is_shared()) clone_rep()` で
 *   **COW する** (Nef_polyhedron_3.h:1135)。よって原本 n_ / o->nef() は壊れない。
 * ★有界性の検査は呼び側 (nfaMinkowski) の責務。CGAL は非有界を渡されると stderr に
 *   "first parameter is an infinite point set" と出して**片方をそのまま返す**ので、
 *   ここまで来たら黙って嘘の答えになる。 */
sPtr<nfMesh>
nfMesh::op_minkowski(sPtr<nfMesh> o)
{
	if ( ! o.is_notNull() ) return thNULL;
	/* 空集合との和は空 (CGAL に渡す前に畳む。凸分解を走らせる意味が無い)。 */
	if ( n_.is_empty() || o->nef().is_empty() ) return thNEW(nfMesh,(Nef()));
	Nef a(n_), b(o->nef());
	return thNEW(nfMesh,(CGAL::minkowski_sum_3(a, b)));
}

/* ---- 凸分解 (#3441) ----
 * @CGAL::convex_decomposition_3@ は Nef を **その場で** 凸片へ割る (仕切り面が入るので
 * @is_simple()@ は偽になる)。共有された値を壊さないようコピーに対して行う。
 * ★用途: 物理エンジンの凸コリジョン形状・3D プリントのサポート生成。Minkowski 和が内部で
 *   使っているものと同じ分解で、offset が重いのはここのペア数が m×n で効くため。 */
sPtr<nfMesh>
nfMesh::op_convex_decomposition()
{
	if ( ! is_bounded() ) return thNULL;   /* 呼び側が明示エラーにする */
	Nef a(n_);
	CGAL::convex_decomposition_3(a);
	return thNEW(nfMesh,(a));
}

/* ---- 塊 (part) の取り出し (#3441 追補) ----
 * 塊 = SNC の **marked volume**。最初の volume は無限体積なので飛ばす。
 * 空洞は marked でないので塊に数えない (空洞つき立体は 1 つの塊)。 */
int
nfMesh::op_nparts()
{
	int n = 0;
	Nef::Volume_const_iterator ci = n_.volumes_begin();
	for ( ++ci ; ci != n_.volumes_end() ; ++ci )
		if ( ci->mark() ) ++n;
	return n;
}

/* i 番目の塊 (0 始まり)。範囲外は null (呼び側が明示エラー)。
 * ★その volume の **全シェル** を集める: shells_begin() の 1 本目が外殻で、以降は空洞の境界。
 *   集めた殻を set_from_mesh に渡すと入れ子の深さで組み直されるので、空洞を持つ塊も正しく出る。 */
sPtr<nfMesh>
nfMesh::op_part(int i)
{
	typedef CGAL::Polyhedron_3<K> Poly;
	if ( i < 0 ) return thNULL;
	int n = 0;
	Nef::Volume_const_iterator ci = n_.volumes_begin();
	for ( ++ci ; ci != n_.volumes_end() ; ++ci ) {
		if ( ! ci->mark() ) continue;
		if ( n++ != i ) continue;
		Mesh acc;
		for ( Nef::Shell_entry_const_iterator si = ci->shells_begin() ;
		      si != ci->shells_end() ; ++si ) {
			Poly p;
			n_.convert_inner_shell_to_polyhedron(si, p);
			if ( p.size_of_facets() == 0 ) continue;
			Mesh part;
			CGAL::copy_face_graph(p, part);
			CGAL::Polygon_mesh_processing::triangulate_faces(part);
			CGAL::copy_face_graph(part, acc);
		}
		if ( acc.number_of_faces() == 0 ) return thNULL;
		sPtr<nfMesh> out = thNEW(nfMesh,());
		out->set_from_mesh(acc);
		return out;
	}
	return thNULL;   /* 範囲外 */
}

/* ---- 内壁除去 (#3442) ----
 * **境界を経由して作り直す**: @to_mesh@ が marked volume ごとに外側シェルだけを取り出すので、
 * 内部の仕切り面 (両側とも立体の facet) はこの時点で落ちる。取り出した殻から
 * @set_from_mesh@ が XOR (even-odd) で組み直すと、接する片は和になって 1 つの立体へ戻る。
 *   凸分解の結果 → 片が接しているので和 = 元の凹形状 (内壁だけ消える・体積は不変)
 *   空洞つき立体 → 空洞の境界は残る (片側が立体でない = 本物の境界) ので体積も不変
 *   単一立体     → 何も変わらない
 * ★@repair@ とは別物。repair は自己交差を幾何的に解消するだけで形を変えないが、こちらは
 *   内壁と低次元の破片が落ちるので **体積が変わりうる**。自動ではやらない (ひさ判断)。
 * ★CGAL の @regularization()@ (= closure(interior())) は使わない — **SNC の作られ方で結果が変わる**。
 *   同じ中空箱でも、difference が作った SNC では空洞が埋まって 27 になり、境界から組み直した
 *   SNC では 26 のままだった (2026-08-18 実測)。挙動が読める境界経由に統一する。
 * ★限界: **自己交差した 1 枚のメッシュ** (自分自身を貫く tube など) はそもそも Nef へ取り込む
 *   時点で有効な立体でないので、ここでは直せない (#3442 に残件として記録)。 */
sPtr<nfMesh>
nfMesh::op_unify_shells()
{
	Mesh m;
	if ( ! to_mesh(m) ) return thNULL;   /* 非有界などは呼び側が明示エラー */
	sPtr<nfMesh> out = thNEW(nfMesh,());
	out->set_from_mesh(m);
	return out;
}

/* ---- offset 用の近似球 (#3440 の 2: cgMesh3D.cpp の cga_make_icosphere をそのまま移設) ----
 * 半径 r・細分化 subdiv の icosphere (icosahedron を Loop 細分して球面へ投影)。
 * subdiv=0=icosahedron(20 面・粗い)・1=80 面・2=320 面… 大きいほど滑らかだが Minkowski が重い。
 * 投影は double 正規化 (sqrt) → K::FT 格納 (近似形状・有理座標。回転と同じ方針)。
 * ★sphere/icosphere op が使う測地球 (common/geodesic.h) とは**別の生成器**である。offset 専用で、
 *   cgal.so 時代の出力 (頂点数・面数) をそのまま引き継ぐために同じ作り方を維持する。 */
static void
nf_make_offset_ball(nfMesh::Mesh &ball, double r, int subdiv)
{
	typedef nfMesh::K K;
	CGAL::make_icosahedron<nfMesh::Mesh, K::Point_3>(ball, K::Point_3(0,0,0), K::FT(r));
	CGAL::Polygon_mesh_processing::triangulate_faces(ball);
	for ( int i = 0 ; i < subdiv ; ++i )
		CGAL::Subdivision_method_3::Loop_subdivision(ball, CGAL::parameters::number_of_iterations(1));
	if ( subdiv > 0 ) {
		for ( nfMesh::Mesh::Vertex_index v : ball.vertices() ) {
			double x = CGAL::to_double(ball.point(v).x());
			double y = CGAL::to_double(ball.point(v).y());
			double z = CGAL::to_double(ball.point(v).z());
			double L = std::sqrt(x*x + y*y + z*z);
			if ( L > 0 ) { double s = r / L;
				ball.point(v) = K::Point_3(K::FT(x*s), K::FT(y*s), K::FT(z*s)); }
		}
		CGAL::Polygon_mesh_processing::triangulate_faces(ball);
	}
}

/* ---- 3D オフセット (#3440 の 2) ----
 * d>0: A ⊕ ball(d)。d<0: 補集合トリック erode(A,r) = A − dilate(box−A, ball(r))。
 * ★bbox は **SNC の頂点**から取る (境界表現へ落とさない = 型維持・非 2-多様体でも取れる)。
 *   有界化のため margin(>r) 拡大した箱を使う (箱の壁付近の誤侵食を避ける)。 */
sPtr<nfMesh>
nfMesh::op_offset(double d, int subdiv)
{
	if ( d == 0.0 ) return thNEW(nfMesh,(n_));
	if ( subdiv < 0 ) subdiv = 0;
	/* ★ 上限 6。**黙って丸めず、範囲外は呼び元が明示エラーにする** (null を返す約束)。
	 *   旧実装は `subdiv > 3` を黙って 3 に丸めていたが、これは「黙ってフォールバックしない」
	 *   という本プロジェクトの原則に反していた (利用者は 4 を頼んだのに 3 の結果を受け取り、
	 *   しかもそれと気づけない)。
	 *
	 *   ★ 分割を 1 段上げるごとに誤差は下がるが、**コストの伸びの方が急**で、上限の 6 は
	 *     つまみとしては事実上の終端 (実用にならない時間になる)。
	 *   ⚠ 7 以上は**未検証**。伸びから外挿して「できない」と言い切らないこと。
	 *
	 *   ⚠⚠ 2026-08-20 の初報で「6 は CGAL が完走しない」と書いたのは**誤り**だった。
	 *     6 は完走する。**測定中に私が別のビルドをしていて agent が版不一致で拒否されていた**のを、
	 *     CGAL の失敗と読み違えていた。docs/Redmine とも訂正済み。 */
	if ( subdiv > 6 ) return sPtr<nfMesh>();
	double r = ( d > 0.0 ) ? d : -d;

	Mesh ballMesh;
	nf_make_offset_ball(ballMesh, r, subdiv);
	Nef ball(ballMesh);

	if ( d > 0.0 ) {
		Nef a(n_);
		return thNEW(nfMesh,(CGAL::minkowski_sum_3(a, ball)));
	}

	/* 収縮: A の bbox を 2r 拡大した箱 B を作り、B − A を膨張させて A から引く。 */
	double minx=1e300, miny=1e300, minz=1e300, maxx=-1e300, maxy=-1e300, maxz=-1e300;
	for ( Nef::Vertex_const_iterator v = n_.vertices_begin() ; v != n_.vertices_end() ; ++v ) {
		double x = CGAL::to_double(v->point().x());
		double y = CGAL::to_double(v->point().y());
		double z = CGAL::to_double(v->point().z());
		if ( x<minx ) minx=x; if ( x>maxx ) maxx=x;
		if ( y<miny ) miny=y; if ( y>maxy ) maxy=y;
		if ( z<minz ) minz=z; if ( z>maxz ) maxz=z;
	}
	if ( minx > maxx ) return thNEW(nfMesh,(Nef()));   /* 空 */
	double mg = 2.0 * r;   /* margin > r */
	minx-=mg; miny-=mg; minz-=mg; maxx+=mg; maxy+=mg; maxz+=mg;
	Mesh boxMesh;
	CGAL::make_hexahedron(
	    Point_3(K::FT(minx),K::FT(miny),K::FT(minz)), Point_3(K::FT(maxx),K::FT(miny),K::FT(minz)),
	    Point_3(K::FT(maxx),K::FT(maxy),K::FT(minz)), Point_3(K::FT(minx),K::FT(maxy),K::FT(minz)),
	    Point_3(K::FT(minx),K::FT(miny),K::FT(maxz)), Point_3(K::FT(maxx),K::FT(miny),K::FT(maxz)),
	    Point_3(K::FT(maxx),K::FT(maxy),K::FT(maxz)), Point_3(K::FT(minx),K::FT(maxy),K::FT(maxz)), boxMesh);
	CGAL::Polygon_mesh_processing::triangulate_faces(boxMesh);

	Nef box(boxMesh);
	Nef outside = box - n_;                                   /* 箱内の外側 (A の補集合を有界化) */
	Nef dilated = CGAL::minkowski_sum_3(outside, ball);       /* 外側を r 膨張 (A 内へ r シェル侵入) */
	return thNEW(nfMesh,(n_ - dilated));                      /* A から r シェルを除く = 収縮 */
}


/* ---- n 項ブール (#3436 P4) --------------------------------------------------
 * CGAL の Nef 演算子は二項なので、ここで逐次に畳む。★ 効くのは「中間 SNC を wire へ
 * 直列化して読み直す往復が消える」ところで、geogram/occt の「1 回の交差計算」とは効き方が違う
 * (P4 の対比ではここを区別する)。 */
sPtr<nfMesh>
nfMesh::bool_from_args(sArray<sPtr<pigData> > *args, const char *kind, const char **errmsg)
{
	int na = ( args != 0 ) ? args->length() : 0;
	if ( na < 2 ) { *errmsg = "needs at least two Nef meshes"; return sPtr<nfMesh>(); }
	sArray<sPtr<nfMesh> > ops;
	ops.length(na);
	for ( int i = 0 ; i < na ; ++i ) {
		ops[i] = sPtr<nfMesh>::d_cast((*args)[i]);
		if ( ! ops[i].is_notNull() ) { *errmsg = "needs Nef meshes"; return sPtr<nfMesh>(); }
	}
	sPtr<nfMesh> acc = ops[0];
	for ( int i = 1 ; i < na ; ++i ) {
		if      ( ::strcmp(kind, "union") == 0 )        acc = acc->op_union(ops[i]);
		else if ( ::strcmp(kind, "intersection") == 0 ) acc = acc->op_intersection(ops[i]);
		else                                            acc = acc->op_difference(ops[i]);
		if ( ! acc.is_notNull() ) { *errmsg = "boolean failed"; return sPtr<nfMesh>(); }
	}
	return acc;
}
