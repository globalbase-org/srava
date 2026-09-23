/*
 * nfNefMesh — CGAL Nef_polyhedron_3 の値ハンドル実装 (#3433 P1)。
 * ブールは Nef のまま(型維持)。cache は SNC で書くので、境界表現へ落とすのは
 * volume / export (= to_mesh) の時だけ。
 */
#include	"nf/c++/nfMeshCgal.h"   /* ★ #3545: CGAL はここからだけ入る (nfMesh.h も読む) */
#include	"common/blockframe.h"   /* ★ #3507: ブロック分割フレーミング */
#include	"cg/c++/cgaMeshCodec.h"        /* 厳密境界の共通 codec (cg の "MESH" と同一形式) */
#include	"ts2/c++/stdString.h"

#include	<CGAL/Aff_transformation_3.h>                       /* アフィン変換 (#3486) */
#include	<CGAL/IO/Nef_polyhedron_iostream_3.h>   /* ★SNC シリアライズの**定義**はここ (宣言は Nef_polyhedron_3.h) */
#include	<CGAL/minkowski_sum_3.h>                /* Minkowski 和 (内部で凸分解・#3440) */
#include	<CGAL/boost/graph/convert_nef_polyhedron_to_polygon_mesh.h>
#include	<CGAL/Polygon_mesh_processing/triangulate_faces.h>
#include	<CGAL/boost/graph/IO/polygon_mesh_io.h>
#include	<CGAL/boost/graph/generators.h>          /* make_icosahedron / make_hexahedron (offset の球と箱) */
#include	<CGAL/subdivision_method_3.h>            /* Loop 細分 (近似球) */
#include	<CGAL/Polygon_mesh_processing/connected_components.h>   /* シェル分解 (空洞の復元・#3440) */
#include	<CGAL/Polygon_mesh_processing/measure.h>
#include	<CGAL/Polygon_mesh_processing/self_intersections.h>   /* valid の ③ (#3487) */                /* 符号付き体積 (向きの判定) */
#include	<CGAL/Polygon_mesh_processing/orientation.h>            /* reverse_face_orientations */
#include	<CGAL/convex_decomposition_3.h>                         /* 凸分解 (#3441) */
#include	<CGAL/convex_hull_3.h>                              /* ★ #3511: hull */
#include	<CGAL/Polyhedron_3.h>                                   /* convert_inner_shell_to_polyhedron の受け皿 */
#include	<CGAL/boost/graph/copy_face_graph.h>
#include	<CGAL/Side_of_triangle_mesh.h>   /* シェルの入れ子判定 (#3441/#3442) */
#include	<CGAL/Nef_nary_union_3.h>             /* 面ごとの Nef を n 項 union (#3445) */
#include	<CGAL/normal_vector_newell_3.h>       /* 面の法線 (Nef の平面向き) */
#include	<CGAL/Nef_3/Mark_bounded_volumes.h>   /* 有界セルを「中身」にする (#3445) */
#include	<algorithm>
#include	"common/geodesic.h"   /* ★ #3545: 測地球の生成器 (カーネル非依存) */
#include	<vector>
#include	<cmath>

#include	<string.h>
#include	<sstream>

/* ---- reader 用ファクトリ: 4CC タグ → 具体型 ----
 * ★★ #3559: 判定の中身は **両変種で同じ** (受ける 4CC も昇格読みの扱いも)。違うのは
 *   *どちらの型として実体化するか* だけなので、器だけ 2 つ置いて中身は 1 本にする。
 *   ⚠ @pig_wire_factory<T>@ は @T::create_for_meta@ を呼ぶので、**派生に継承させては
 *     いけない** — nfMeshSnc が nfMesh のものを継いだら snc の wire が hybrid を作る。
 *     ⇒ 2 型とも自分の static を持つ (継承していたら型は割れていない、という検査でもある)。 */
static sPtr<nfGeom>
nf_accept_meta(sPtr<nfNefMesh> fresh, const uint8_t *meta, int len)
{
	if ( meta == 0 || len != 4 ) return thNULL;
	/* ★自分の 4CC と、**もう一方の nef モジュールの 4CC** の両方を読む (ひさ方針: reader は
	 *   どちらにも対応)。形式は payload 先頭バイトが自己記述するので decode は共通。 */
	if ( ::memcmp(meta, "NEF3", 4) == 0 ) return sPtr<nfGeom>::d_cast(fresh);
	if ( ::memcmp(meta, "NEFB", 4) == 0 ) return sPtr<nfGeom>::d_cast(fresh);
	if ( ::memcmp(meta, "MESH", 4) == 0 ) {          /* cg の厳密境界 → 昇格読み */
		fresh->set_boundary_input();
		return sPtr<nfGeom>::d_cast(fresh);
	}
	if ( ::memcmp(meta, "MFM3", 4) == 0 ) {          /* Manifold の raw double mesh → 昇格読み */
		fresh->set_mfm3_input();
		return sPtr<nfGeom>::d_cast(fresh);
	}
	return thNULL;
}

sPtr<nfGeom>
nfMesh::create_for_meta(const uint8_t *meta, int len)
{
	return nf_accept_meta(thNEW(nfMesh,()), meta, len);
}

sPtr<nfGeom>
nfMeshSnc::create_for_meta(const uint8_t *meta, int len)
{
	return nf_accept_meta(thNEW(nfMeshSnc,()), meta, len);
}

/* ---- 同じ変種の空の値 (このクラスの中の「新しい Nef を返す」経路はすべてここを通る) ---- */
sPtr<nfNefMesh> nfMesh::make_empty(sPtr<pigInfo> i)    const { return thNEW(nfMesh,(i)); }
sPtr<nfNefMesh> nfMeshSnc::make_empty(sPtr<pigInfo> i) const { return thNEW(nfMeshSnc,(i)); }

/* ★ #3535②: 体積 (nfaVolume.cpp から移した)。⚠ CGAL::to_double まで含めてここでやる。 */
int
nfNefMesh::volume(double *out)
{
	nfNefBox::Mesh m;
	if ( ! nf_to_mesh(this, m) )
		return 0;
	/* ⚠⚠ #3525: @CGAL::to_double@ は EPECK の Lazy_exact_nt の *区間近似* を返すので
	 *   **正しく丸められていない**。⇒ @CGAL::exact()@ で有理数を確定させてから落とす。
	 *   ★ 直した動機: cgal 側を直したら @srava_agree_nef_hybrid_pyramid@ が 2 ulp で割れた。
	 *     つまりそれまで **両方が同じだけずれていた** から緑だっただけで、合っていたのは
	 *     *互いに* であって *正しい値に* ではなかった。⇒ 片方だけ直すと割れるのが正しい挙動。
	 *   ⚠ cgMesh3D::op_volume と対 — 片方だけ直さないこと。 */
	if ( out != 0 )
		*out = CGAL::to_double(CGAL::exact(CGAL::Polygon_mesh_processing::volume(m)));
	return 1;
}

/* ★ #3545: 境界の頂点数 / 面数。**op 側で nfNefBox::Mesh を持たなくて済む**ようにするためだけの薄い層。
 *   ⚠ これが無いと nfaNverts / nfaNfaces が @nfNefBox::Mesh@ を宣言することになり、
 *     その TU が CGAL を include する = 元の木阿弥になる。 */
int
nfNefMesh::boundary_counts(int *nverts, int *nfaces)
{
	nfNefBox::Mesh m;
	if ( ! nf_to_mesh(this, m) )
		return 0;
	if ( nverts != 0 ) *nverts = (int)m.number_of_vertices();
	if ( nfaces != 0 ) *nfaces = (int)m.number_of_faces();
	return 1;
}

/* ★★ #3545 段 4: 厳密境界を cg の "MESH" フレーミングでバイト列へ (橋 nef_mf の口)。
 *   ⚠ 形式は nfNefMesh::encode の NF_FORM_BOUNDARY と **同じ** cgaMeshCodec なので、
 *     ここで新しい依存は増えない。⇒ 橋の op TU は CGAL を 1 枚も引かずに済む。 */
int
nfNefMesh::to_exact_boundary_bytes(std::vector<uint8_t> &out)
{
	nfNefBox::Mesh bnd;
	if ( ! nf_to_mesh(this, bnd) )
		return 0;                       /* 境界表現が無い (非有界など) */
	struct MemSink {
		std::vector<uint8_t> *b;
		void chunk(const uint8_t *d, int n) { b->insert(b->end(), d, d + n); }
	} a;
	a.b = &out;
	cgaMeshCodec::encode(bnd, a);
	return 1;
}

/* ★★ #3545: 素の配列から境界メッシュを作って Nef へ。**CGAL の受け皿はここだけ**。
 *   ⚠ 生成 op (nfTriSink を使う 9 本 + sphere) はここへ配列を渡すだけになり、
 *     CGAL のヘッダを 1 枚も引かなくなる。 */
void
nfNefMesh::build_from_triangles(const double *xyz, int nv, const int *idx, int nt)
{
	nfNefBox::Mesh m;
	std::vector<nfNefBox::Mesh::Vertex_index> vs;
	vs.reserve((size_t)nv);
	for ( int i = 0 ; i < nv ; ++i )
		vs.push_back(m.add_vertex(nfNefBox::Point_3(xyz[i*3], xyz[i*3+1], xyz[i*3+2])));
	for ( int t = 0 ; t < nt ; ++t )
		m.add_face(vs[idx[t*3]], vs[idx[t*3+1]], vs[idx[t*3+2]]);
	nf_set_from_mesh(this, m);
}

/* 測地球 (sphere / icosphere)。生成器 (common/geodesic.h) はカーネル非依存。 */
void
nfNefMesh::build_geodesic(int seed, int n, double r)
{
	nfNefBox::Mesh m;
	struct GeoSink {
		nfNefBox::Mesh&                           m;
		std::vector<nfNefBox::Mesh::Vertex_index> vs;
		GeoSink(nfNefBox::Mesh& mm) : m(mm) {}
		int  add_vertex(double x, double y, double z) {
			vs.push_back(m.add_vertex(nfNefBox::Point_3(x, y, z)));
			return (int)vs.size() - 1;
		}
		void add_triangle(int a, int b, int c) { m.add_face(vs[a], vs[b], vs[c]); }
	} sink(m);
	srava_geo::make_geodesic((srava_geo::SeedKind)seed, n, r, sink);
	nf_set_from_mesh(this, m);
}

/* ★ #3535②: 直方体 (nfaBox.cpp から移した。理由は nfMesh.h の宣言のところ)。 */
void
nfNefMesh::build_box(double w, double h, double d)
{
	nfNefBox::Mesh m;
	typedef nfNefBox::Point_3 P;
	CGAL::make_hexahedron(
	    P(0,0,0), P(w,0,0), P(w,h,0), P(0,h,0),
	    P(0,0,d), P(w,0,d), P(w,h,d), P(0,h,d), m);
	CGAL::Polygon_mesh_processing::triangulate_faces(m);
	nf_set_from_mesh(this, m);   /* 境界 → nfNefBox::Nef (SNC 構築) */
}

/* ★ #3535②: ctor / dtor の実体はここ (ヘッダに置かない理由は nfMesh.h の宣言のところ)。 */
nfNefMesh::nfNefMesh(sPtr<pigInfo> i)
    : nfGeom(i), boundaryInput_(0), buildErr_(0), mfm3Input_(0), lastErr_(0),
      box_(new nfNefBox()) {}

nfNefMesh::~nfNefMesh() { delete box_; }

/* ★ #3545: nfNefBox::Nef から nfNefMesh を作る (公開ヘッダに CGAL 型の ctor を置けないので自由関数)。
 * ★★ #3559: @proto@ と **同じ変種**で作る。nef_snc の op が nfb-mesh3d を返したら型が変わる。 */
sPtr<nfNefMesh>
nf_make_nef(const nfNefMesh &proto, const nfNefBox::Nef &n, sPtr<pigInfo> i)
{
	sPtr<nfNefMesh> m = proto.make_empty(i);
	m->box().n = n;
	return m;
}

sPtr<stdString>
nfNefMesh::get_str()
{
	std::ostringstream os;
	os << "nef3(volumes=" << box_->n.number_of_volumes()
	   << ",facets="      << box_->n.number_of_facets()
	   << (box_->n.is_simple() ? "" : ",non-simple") << ")";
	std::string s = os.str();
	return thNEW(stdString,(s.c_str()));
}

/* ---- cache 書き出し: SNC そのもの ([u32 blocklen][block]…[u32 0]・#3507) ----
 * ★境界表現で書くと非有界な Nef (箱の補集合など) が「箱」に化ける。SNC なら非有界・低次元も
 *   厳密に往復する (nfMesh.h 冒頭の実測メモ参照)。 */
void
nfNefMesh::encode(nfChunkSink &sink)
{
	/* ★ 形式は payload 先頭 1 バイトで自己記述する (4CC を増やさない = 型↔タグ 1:1 の
	 *   不変条件を壊さない・routing 無傷)。
	 *
	 * ★★ **境界表現を持てるかは @to_mesh@ を実際に試して決める** (2026-09-06)。
	 *   ⚠ #3499 以降、この判定が要るのは **hybrid だけ** (snc は常に SNC を書く)。
	 *   従来の条件は @is_bounded() && box_->n.is_simple()@ だったが、@to_mesh@ は 2-多様体で
	 *   なくても「marked volume ごとの全シェル」で境界を作れる — @volume@ / @export@ が
	 *   非 2-多様体の値でも通っているのはこの経路。is_simple() で切ると
	 *   **境界を持てる値まで「境界の無い SNC」として書かれ、cgal / manifold から読めなく
	 *   なっていた** (実測: 3 球の XOR は 4 つの塊が稜で接するので is_simple()=false になり、
	 *   volume は出るのに cast("mf-mesh3d", ...) が拒否されていた)。 */
	/* ★★ #3559: ここが **変種の唯一の分岐**。以前は @#if defined(NF_WIRE_HYBRID)@ で、
	 *   そのためにライブラリを 2 本建てていた (= 上流 CGAL の実体も 2 つになっていた)。
	 *   実行時の @wire_hybrid()@ にしたので、実装は 1 本で両方の道を持てる。
	 *   ⚠ snc の側で @nf_to_mesh@ を **呼ばない**ことが #3499 の眼目 (下記)。
	 *     「どちらも計算してから選ぶ」形に書き換えてはいけない。 */
	nfNefBox::Mesh bnd;
	uint8_t        form;
	if ( wire_hybrid() ) {
		bool haveBnd = nf_to_mesh(this, bnd);
		/* ★ハイブリッド (#3433): 普通の立体 (有界かつ 2-多様体) は **厳密境界だけ**で書く。
		 *   狙い: (a) 普通の立体で cache が cg 並みに小さくなる (SNC は ~18x 太い)
		 *         (b) 境界形式は **cgal も manifold もそのまま読める** (mf は CGAL 非依存のまま nf を消費可)
		 *   ★ 空洞 (中空立体) の扱い (#3440): 境界形式は「面の集まり」なので、素朴に読み戻すと
		 *     入れ子シェルが「もう 1 つの立体」になり空洞が中実に化ける。→ 読み側
		 *     (@set_from_mesh@) を **シェルごとの Nef を対称差 (even-odd) で畳む**よう直したので、
		 *     空洞つき立体も境界形式で安全に運べる。
		 *
		 *   ⚠ **2-多様体でない値に境界形式は使えない**。境界だけを書き戻すと内部の仕切り面が
		 *     消え、@convex_decomposition@ の結果が 1 塊に化ける (@nparts@ / @part@ が壊れる) —
		 *     点集合は同じでも **値が変わる**。そこで #3478 の SNC_BND を使い、
		 *     **SNC を本体・境界を付録**として両方書く。nef 自身は前半の SNC を読むので
		 *     値は完全に保たれ、他カーネルは後半の境界を読める。cache は太るが、
		 *     太るのは元々 SNC で書いていた値だけ (付録ぶんの増加は境界 1 枚)。 */
		form = haveBnd ? ( box_->n.is_simple() ? NF_FORM_BOUNDARY : NF_FORM_SNC_BND )
		               : NF_FORM_SNC;
	} else {
		/* ★★ #3499 (2026-09-07): nef_snc は **常に SNC だけ** を書く (nfNefBox::Nef 本来の表現)。
		 *   #3478 はここで境界を付録として併記していた (NF_FORM_SNC_BND) が、**撤回した**:
		 *     ・付録のために **encode ごとに nf_to_mesh(this, ) が走る**。データ量の増加は数 % だが、
		 *       実時間の代償はそれよりずっと大きい (#3499 で実測)。
		 *     ・「常に SNC」は nef_snc という変種の定義そのもので、付録はその境界を曖昧にしていた。
		 *   ⇒ 他カーネルへ渡す口は **橋モジュール** (nef_cg.so / nef_mf.so) が持つ。
		 *     cast が呼ばれたときだけ nf_to_mesh(this, ) を払う (occt_mf と同じ分担)。
		 *   ⚠ したがって cgal.so / manifold.so は **NEF3 を読まない**。読もうとすると
		 *     「SNC は CGAL 無しでは解釈できない」で正しく落ちる。 */
		form = NF_FORM_SNC;
	}
	sink.chunk(&form, 1);

	struct Adapt { nfChunkSink *s; void chunk(const uint8_t *d, int n) { s->chunk(d, n); } } a;
	a.s = &sink;

	if ( form == NF_FORM_BOUNDARY ) {
		cgaMeshCodec::encode(bnd, a);
		return;
	}
	/* ★★ #3507 (2026-09-10): SNC を **ブロック分割**で直接流す。
	 *   旧実装は @ostringstream@ に全文を作り @str()@ でもう 1 本コピーしていた
	 *   (SNC 2.80 GiB の模型では **この 1 op だけで一時領域 5.6 GiB**)。全長を先頭に置く
	 *   形式がそれを強制していたので、形式ごと [u32 blocklen][block]…[u32 0] へ変えた。
	 *   ⇒ 一時領域は固定 1 MiB ・ @int@ の 2 GiB 問題は構造的に消え (#3504 / #3506)、
	 *     長さ欄 u32 による **4 GiB の上限も無くなる**。 */
	{
		blockframe::obuf<nfChunkSink> ob(sink);
		std::ostream                  os(&ob);
		os << box_->n;
		os.flush();
		ob.finish();          /* 残りを出して終端 [u32 0] を書く */
	}
	/* ★ #3478: 後半の厳密境界 (cg の "MESH" と同一フレーミング)。読み手は前半の
	 *   ブロック列を終端 [u32 0] まで読み捨てればここに着く (#3507。旧形式では
	 *   前置された全長で読み飛ばしていた)。★ #3499 以降 **これを書くのは hybrid だけ**
	 *   (snc の form は上で NF_FORM_SNC に固定されるので、この条件は成立しない)。 */
	if ( form == NF_FORM_SNC_BND )
		cgaMeshCodec::encode(bnd, a);
}

void
nfNefMesh::decode(nfChunkSource &src)
{
	if ( boundaryInput_ ) { decode_boundary(src); return; }   /* "MESH" → 境界から SNC 構築 */
	if ( mfm3Input_ )     { decode_mfm3(src);     return; }   /* "MFM3" → raw double から SNC 構築 */
	uint8_t form = NF_FORM_SNC;
	src.pull(&form, 1);                                       /* ★形式バイト (encode 参照) */
	if ( form == NF_FORM_BOUNDARY ) { decode_boundary(src); return; }
	/* ★ #3478: NF_FORM_SNC_BND も **前半の SNC を読むだけ** (後半の境界は他カーネル向けの
	 *   付録で、nef にとっては冗長)。reader が残りを閉じるので読み飛ばす必要もない。 */
	/* ★ #3507: ブロック列を逐次に読む (全文バッファを作らない)。
	 *   ★ 途中で読み終えても **drain() で終端まで読み捨てる** — ストリームは先読みで
	 *     ブロックを丸ごと抱えているので、呼ばないと Source の位置がずれる
	 *     (hybrid の [SNC][境界] のように後ろに別の値が続く形式で効く)。 */
	{
		blockframe::ibuf<nfChunkSource> ib(src);
		std::istream                    is(&ib);
		is >> box_->n;
		if ( ! ib.drain() ) {
			buildErr_ = 1;
			lastErr_  = "the stored SNC frame is corrupt (implausible block length)";
			return;
		}
	}
}

/* ---- "MESH" (cg の厳密境界・cgaMeshCodec フレーミング) を読んで SNC を組む = 昇格読み ---- */
void
nfNefMesh::decode_boundary(nfChunkSource &src)
{
	struct Adapt { nfChunkSource *s;
	               void pull(uint8_t *d, int n) { s->pull(d, n); }
	               int  more() { return s->more(); } } a;
	a.s = &src;
	nfNefBox::Mesh m;
	cgaMeshCodec::decode(a, m);
	nf_set_from_mesh(this, m);
}

/* ---- "MFM3" (Manifold の raw double mesh) を読んで SNC を組む = 昇格読み ----
 * framing (mfMesh::encode と一致・全 little-endian): [u32 nv][u32 nt] + nv×(3×f64 頂点) +
 *   nt×(3×u32 三角形 index)。★double → EPECK は**無損失** (double は 2 進有理数)。
 *   mfMesh.h には依存しない (framing は安定契約としてここに inline 再現 = cgMesh3D::decode_mfm3 と同じ作法)。
 *   Manifold は一貫した外向き巻きで watertight を保証するので add_face は成功する。 */
void
nfNefMesh::decode_mfm3(nfChunkSource &src)
{
	uint8_t b4[4];
	src.pull(b4, 4);
	uint32_t nv = (uint32_t)b4[0] | ((uint32_t)b4[1]<<8) | ((uint32_t)b4[2]<<16) | ((uint32_t)b4[3]<<24);
	src.pull(b4, 4);
	uint32_t nt = (uint32_t)b4[0] | ((uint32_t)b4[1]<<8) | ((uint32_t)b4[2]<<16) | ((uint32_t)b4[3]<<24);
	nfNefBox::Mesh m;
	std::vector<nfNefBox::Mesh::Vertex_index> vmap;
	vmap.reserve(nv);
	for ( uint32_t i = 0 ; i < nv ; ++i ) {
		double xyz[3];
		for ( int k = 0 ; k < 3 ; ++k ) {
			uint8_t b8[8];
			src.pull(b8, 8);
			::memcpy(&xyz[k], b8, 8);
		}
		vmap.push_back(m.add_vertex(nfNefBox::Point_3(nfNefBox::K::FT(xyz[0]), nfNefBox::K::FT(xyz[1]), nfNefBox::K::FT(xyz[2]))));
	}
	for ( uint32_t t = 0 ; t < nt ; ++t ) {
		uint32_t idx[3];
		for ( int k = 0 ; k < 3 ; ++k ) {
			src.pull(b4, 4);
			idx[k] = (uint32_t)b4[0] | ((uint32_t)b4[1]<<8) | ((uint32_t)b4[2]<<16) | ((uint32_t)b4[3]<<24);
		}
		m.add_face(vmap[idx[0]], vmap[idx[1]], vmap[idx[2]]);
	}
	nf_set_from_mesh(this, m);
}

/* ---- 境界メッシュから nfNefBox::Nef を作る (例外を明示エラーへ) ----
 * ★**自己交差したメッシュ** (自分を貫く tube など) を渡すと CGAL の Nef 構築は前提を満たさず、
 *   assertion で落ちる = agent プロセスごと死ぬ ("agent closed unexpectedly" になり原因が分からない)。
 *   CGAL の assertion は既定で例外を投げるので、ここで受けて **null を返し呼び側が明示エラー**にする。
 *   ★事前に @does_self_intersect@ で弾く手もあるが、正常なメッシュにも O(n log n) の検査が乗るので
 *     採らない (seg=300 のベンチが重くなる)。壊れた入力のときだけ払う形にする。 */
static int
nf_try_build(nfNefBox::Mesh &m, nfNefBox::Nef &out)
{
	try {
		out = nfNefBox::Nef(m);
		return 1;
	} catch ( const std::exception & ) {
		return 0;   /* 自己交差など nfNefBox::Nef の前提を満たさない */
	} catch ( ... ) {
		return 0;
	}
}

/* ---- 面ごとの nfNefBox::Nef を n 項 union し、有界セルを mark する (#3445) ----
 * これが「壊れた境界から立体を作る」中核。面は 2 次元 (体積ゼロ) の Nef になるので、union だけでは
 * **表面のまま**だが、その表面が空間をセルに切る。最後に @Mark_bounded_volumes@ で **有界セルを
 * 塗る**と立体になる。交差線は union の過程で実エッジとして入るので、自己交差がここで解ける。
 * ★コストは面数に比例して Nef の union を繰り返す = **重い** (通常の Nef 構築より桁で重い)。
 *   だから既定の変換経路には決して置かない。明示 op からのみ。 */
sPtr<nfNefMesh>
nf_build_from_facets(sPtr<nfNefMesh> proto, nfNefBox::Mesh &m)
{
	CGAL::Nef_nary_union_3<nfNefBox::Nef> nary;
	int added = 0;
	for ( nfNefBox::Mesh::Face_index f : m.faces() ) {
		std::vector<nfNefBox::Point_3> pts;
		for ( nfNefBox::Mesh::Vertex_index v : CGAL::vertices_around_face(m.halfedge(f), m) )
			pts.push_back(m.point(v));
		if ( pts.size() < 3 ) continue;
		nfNefBox::K::Vector_3 normal;
		CGAL::normal_vector_newell_3(pts.begin(), pts.end(), normal);
		if ( normal == CGAL::NULL_VECTOR ) continue;   /* 退化面は捨てる */
		nfNefBox::Nef one(pts.begin(), pts.end(), normal);
		if ( one.is_empty() ) continue;
		nary.add_polyhedron(one);
		++added;
	}
	if ( added == 0 ) return thNULL;
	nfNefBox::Nef u = nary.get_union();
	CGAL::Mark_bounded_volumes<nfNefBox::Nef> mbv(true);
	u.delegate(mbv);                                   /* ★有界セルを「中身」にする */
	return nf_make_nef(proto, u);
}

/* ---- solidify の本体 (#3445) ----
 * 連結成分ごとに「面ごとの Nef を union → 有界セルを mark」し、成分どうしは **入れ子の深さ** で
 * 合成する。深さ合成が無いと @Mark_bounded_volumes@ が空洞まで塗って中空箱が 26 → 27 になる。
 * ★入力は nf のみ。自己交差した閉メッシュも cg→nf の厳密変換は **通る** (Nef 構築は面どうしの
 *   交差を検査せず局所の接続だけから SNC を組む) ので、壊れた形のまま入っていてよい。 */
sPtr<nfNefMesh>
nfNefMesh::solidify_mesh(sPtr<pigData> in)
{
	namespace PMP = CGAL::Polygon_mesh_processing;
	sPtr<nfNefMesh> nf = sPtr<nfNefMesh>::d_cast(in);
	if ( ! nf.is_notNull() )        return thNULL;
	nfNefBox::Mesh m;
	if ( ! nf_to_mesh(nf, m) )         return thNULL;   /* 非有界などは呼び側がエラーに */
	if ( m.number_of_faces() == 0 ) return thNULL;

	nfNefBox::Mesh::Property_map<nfNefBox::Mesh::Face_index, std::size_t> ccmap =
	    m.add_property_map<nfNefBox::Mesh::Face_index, std::size_t>("f:nfSol", 0).first;
	std::size_t nComp = PMP::connected_components(m, ccmap);
	m.remove_property_map(ccmap);
	if ( nComp <= 1 ) return nf_build_from_facets(nf, m);

	/* 成分ごとに立体化 → 深さで合成 (外殻は和・空洞は差)。 */
	std::vector<nfNefBox::Mesh> parts;
	PMP::split_connected_components(m, parts);
	std::vector<nfNefBox::Mesh> solids;      /* 深さ判定に使う「立体化した成分の境界」 */
	std::vector<nfNefBox::Nef>  nefs;
	for ( std::size_t i = 0 ; i < parts.size() ; ++i ) {
		sPtr<nfNefMesh> one = nf_build_from_facets(nf, parts[i]);
		if ( ! one.is_notNull() ) continue;
		nfNefBox::Mesh b;
		if ( ! nf_to_mesh(one, b) || b.number_of_faces() == 0 ) continue;
		solids.push_back(b);
		nefs.push_back(nf_nef(one));
	}
	if ( nefs.empty() ) return thNULL;

	std::vector<int> depth(solids.size(), 0);
	for ( std::size_t a = 0 ; a < solids.size() ; ++a ) {
		if ( solids[a].number_of_vertices() == 0 ) continue;
		nfNefBox::K::Point_3 probe = solids[a].point(*solids[a].vertices().begin());
		for ( std::size_t b = 0 ; b < solids.size() ; ++b ) {
			if ( a == b ) continue;
			CGAL::Side_of_triangle_mesh<nfNefBox::Mesh, nfNefBox::K> inside(solids[b]);
			if ( inside(probe) == CGAL::ON_BOUNDED_SIDE ) ++depth[a];
		}
	}
	std::vector<std::size_t> order(solids.size());
	for ( std::size_t i = 0 ; i < order.size() ; ++i ) order[i] = i;
	std::sort(order.begin(), order.end(),
	          [&depth](std::size_t x, std::size_t y) { return depth[x] < depth[y]; });
	nfNefBox::Nef acc;
	for ( std::size_t k = 0 ; k < order.size() ; ++k ) {
		std::size_t a = order[k];
		if ( depth[a] % 2 == 0 ) acc = acc + nefs[a];
		else                     acc = acc - nefs[a];
	}
	return nf_make_nef(nf, acc);
}

/* ---- 閉じたシェルの集まりを 1 つの nfNefBox::Nef へ畳む (#3440/#3441/#3442) ----
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
nf_combine_shells(std::vector<nfNefBox::Mesh> &shells, nfNefBox::Nef &out)
{
	namespace PMP = CGAL::Polygon_mesh_processing;
	typedef nfNefBox::Mesh Mesh;
	typedef nfNefBox::Nef  Nef;
	typedef nfNefBox::K    K;

	std::vector<int> keep;
	for ( std::size_t i = 0 ; i < shells.size() ; ++i ) {
		if ( shells[i].number_of_faces() == 0 || ! CGAL::is_closed(shells[i]) ) continue;
		if ( PMP::volume(shells[i]) < nfNefBox::K::FT(0) )
			PMP::reverse_face_orientations(shells[i]);   /* 向きを外向きへ揃える */
		keep.push_back((int)i);
	}

	/* 入れ子の深さ (自分を厳密に含むシェルの数)。 */
	std::vector<int> depth(keep.size(), 0);
	for ( std::size_t a = 0 ; a < keep.size() ; ++a ) {
		nfNefBox::Mesh &mi = shells[keep[a]];
		if ( mi.number_of_vertices() == 0 ) continue;
		nfNefBox::K::Point_3 probe = mi.point(*mi.vertices().begin());
		for ( std::size_t b = 0 ; b < keep.size() ; ++b ) {
			if ( a == b ) continue;
			CGAL::Side_of_triangle_mesh<nfNefBox::Mesh, nfNefBox::K> inside(shells[keep[b]]);
			if ( inside(probe) == CGAL::ON_BOUNDED_SIDE ) ++depth[a];
		}
	}

	/* 深さの昇順に「和 / 差」を交互に適用する。 */
	std::vector<std::size_t> order(keep.size());
	for ( std::size_t i = 0 ; i < order.size() ; ++i ) order[i] = i;
	std::sort(order.begin(), order.end(),
	          [&depth](std::size_t x, std::size_t y) { return depth[x] < depth[y]; });

	out = nfNefBox::Nef();
	for ( std::size_t k = 0 ; k < order.size() ; ++k ) {
		std::size_t a = order[k];
		nfNefBox::Nef one;
		if ( ! nf_try_build(shells[keep[a]], one) ) continue;   /* 壊れたシェルは飛ばす */
		if ( depth[a] % 2 == 0 ) out = out + one;   /* 立体 = 和 */
		else                     out = out - one;   /* 空洞 = 差 */
	}
}

/* ---- Surface_mesh → Nef ----
 * ★連結成分が 1 個なら従来どおり (数え上げは union-find で、Nef 構築に比べれば無視できる)。
 *   複数シェルのときだけ上の nf_combine_shells で入れ子を解く。 */
void
nf_set_from_mesh(sPtr<nfNefMesh> self, nfNefBox::Mesh &m)
{
	namespace PMP = CGAL::Polygon_mesh_processing;
	if ( m.number_of_faces() == 0 ) { self->box().n = nfNefBox::Nef(); return; }   /* 空集合 */

	nfNefBox::Mesh::Property_map<nfNefBox::Mesh::Face_index, std::size_t> ccmap =
	    m.add_property_map<nfNefBox::Mesh::Face_index, std::size_t>("f:nfCC", 0).first;
	std::size_t nComp = PMP::connected_components(m, ccmap);
	m.remove_property_map(ccmap);
	if ( nComp <= 1 || ! CGAL::is_closed(m) ) {
		if ( ! nf_try_build(m, self->box().n) ) { self->box().n = nfNefBox::Nef(); self->set_build_error(1); }
		return;
	}

	std::vector<nfNefBox::Mesh> shells;
	PMP::split_connected_components(m, shells);
	nf_combine_shells(shells, self->box().n);
}

/* ---- 有界性: SNC の最初の volume(無限体積)が mark されていれば非有界 ---- */
/* ---- 素性を訊く op (#3487) ---------------------------------------------------
 * 境界表現へ落としてから CGAL の厳密述語で測る (volume / export と同じ経路)。
 * 返り値 0 = to_mesh に失敗 (非有界 / 非 2-多様体) で、呼び側が明示エラーにする。 */
int
nfNefMesh::op_bbox(double mn[3], double mx[3])
{
	nfNefBox::Mesh m;
	if ( ! nf_to_mesh(this, m) ) return 0;
	bool first = true;
	for ( nfNefBox::Mesh::Vertex_index v : m.vertices() ) {
		double x = CGAL::to_double(m.point(v).x());
		double y = CGAL::to_double(m.point(v).y());
		double z = CGAL::to_double(m.point(v).z());
		if ( first ) { mn[0]=mx[0]=x; mn[1]=mx[1]=y; mn[2]=mx[2]=z; first = false; }
		else {
			if ( x < mn[0] ) mn[0] = x;  if ( x > mx[0] ) mx[0] = x;
			if ( y < mn[1] ) mn[1] = y;  if ( y > mx[1] ) mx[1] = y;
			if ( z < mn[2] ) mn[2] = z;  if ( z > mx[2] ) mx[2] = z;
		}
	}
	if ( first ) { mn[0]=mn[1]=mn[2]=mx[0]=mx[1]=mx[2] = 0.0; }
	return 3;
}

int
nfNefMesh::op_centroid(double c[3])
{
	nfNefBox::Mesh m;
	if ( ! nf_to_mesh(this, m) ) return 0;
	/* cgMesh3D::op_centroid と同じ式 (原点四面体への分解・符号付き体積で加重平均)。 */
	double cx = 0, cy = 0, cz = 0, vol = 0;
	for ( nfNefBox::Mesh::Face_index f : m.faces() ) {
		nfNefBox::Mesh::Halfedge_index h = m.halfedge(f);
		const nfNefBox::Point_3& A = m.point(m.source(h));
		const nfNefBox::Point_3& B = m.point(m.target(h));
		const nfNefBox::Point_3& C = m.point(m.target(m.next(h)));
		double ax = CGAL::to_double(A.x()), ay = CGAL::to_double(A.y()), az = CGAL::to_double(A.z());
		double bx = CGAL::to_double(B.x()), by = CGAL::to_double(B.y()), bz = CGAL::to_double(B.z());
		double dx = CGAL::to_double(C.x()), dy = CGAL::to_double(C.y()), dz = CGAL::to_double(C.z());
		double v = ( ax*(by*dz - bz*dy) - ay*(bx*dz - bz*dx) + az*(bx*dy - by*dx) ) / 6.0;
		vol += v;
		cx += v * (ax + bx + dx) / 4.0;
		cy += v * (ay + by + dy) / 4.0;
		cz += v * (az + bz + dz) / 4.0;
	}
	if ( vol != 0.0 ) { c[0] = cx/vol; c[1] = cy/vol; c[2] = cz/vol; }
	else              { c[0] = c[1] = c[2] = 0.0; }
	return 3;
}

int
nfNefMesh::op_area(double *out)
{
	nfNefBox::Mesh m;
	if ( ! nf_to_mesh(this, m) ) return 0;
	*out = CGAL::to_double(CGAL::Polygon_mesh_processing::area(m));
	return 1;
}

int
nfNefMesh::op_valid()
{
	nfNefBox::Mesh m;
	/* to_mesh の失敗 = 非有界 or 非 2-多様体 = 「閉じた 2-多様体である」を満たさない。 */
	if ( ! nf_to_mesh(this, m) ) return 0;
	if ( m.number_of_faces() == 0 ) return 0;            /* ① 空でない */
	if ( ! CGAL::is_closed(m) ) return 0;                /* ② 閉じている */
	return CGAL::Polygon_mesh_processing::does_self_intersect(m) ? 0 : 1;   /* ③ */
}


bool
nfNefMesh::is_bounded() const
{
	nfNefBox::Nef::Volume_const_iterator ci = box_->n.volumes_begin();
	if ( ci == box_->n.volumes_end() ) return true;   /* 空集合は有界扱い */
	return ! ci->mark();
}

/* ---- Nef → Surface_mesh (境界表現)。非有界/非 2-多様体は false ----
 * ★is_simple() だけでは足りない: 箱の補集合は is_simple()==true だが非有界で、境界だけ
 *   書き出すと体積 8 の箱に化ける (黙って嘘の値を返すことになる)。is_bounded() を先に見る。 */
bool
nf_to_mesh(sPtr<nfNefMesh> self, nfNefBox::Mesh &out)
{
	self->set_last_error(0);
	if ( ! self->is_bounded() ) { self->set_last_error("the nfNefBox::Nef is unbounded"); return false; }
	if ( self->box().n.is_simple() ) {
		std::size_t nf0 = out.number_of_faces();
		CGAL::convert_nef_polyhedron_to_polygon_mesh(self->box().n, out, true /* 三角化 */);
		/* ★★ #3504: この変換は **面を 1 枚も出せなくても何も言わない** (例外も戻り値も無い)。
		 *   SNC に中身 (marked volume) があるのに面が増えなかったら、それは「空集合」ではなく
		 *   **書き出しの失敗**なので false を返す。ここで通すと export が 0 三角形の STL を
		 *   書いて rc=0 を返し、測定側は体積 0.0 を正しい値として拾ってしまう
		 *   (xor N>=24 で実際に起きた・2026-09-09)。
		 *   ⚠ 本当の空集合 (marked volume が 0) は面 0 が正しい答えなのでそのまま通す。 */
		if ( out.number_of_faces() == nf0 && self->op_nparts() > 0 ) {
			self->set_last_error("the SNC has solid volumes but no triangle came out");
			return false;
		}
		return true;
	}
	/* ★ 仕切り面を持つ nfNefBox::Nef (凸分解の結果・稜だけで接する 2 立体の和など) は境界が 2-多様体で
	 *   ないので上の変換は使えない。**marked な volume ごとに全シェルを取り出し**、別々の
	 *   連結成分として 1 つの Mesh に束ねる。volume は片の合計 (= 分解前と同じ)・export は
	 *   片が別成分として出る。
	 *   ★最初の volume は無限体積なので飛ばす (self->is_bounded() で mark 無しは確認済み)。
	 *   ⚠⚠ **全シェルを回すこと** (2026-09-06 修正)。ここが shells_begin() の 1 枚だけだった
	 *   ため、**非 2-多様体な値の空洞が黙って埋まっていた**。実測: 中空の箱 (8-1=7) と、
	 *   稜だけで接する箱 (8) の和は 15 のはずが **16** を返していた (空洞ぶんの 1 が消える)。
	 *   op_part は最初から全シェルを回しており、そちらだけが正しかった。 */
	typedef CGAL::Polyhedron_3<nfNefBox::K> Poly;
	int nPart = 0;
	nfNefBox::Nef::Volume_const_iterator ci = self->box().n.volumes_begin();
	for ( ++ci ; ci != self->box().n.volumes_end() ; ++ci ) {
		if ( ! ci->mark() ) continue;
		int nShell = 0;
		for ( nfNefBox::Nef::Shell_entry_const_iterator si = ci->shells_begin() ;
		      si != ci->shells_end() ; ++si ) {
			Poly p;
			self->box().n.convert_inner_shell_to_polyhedron(si, p);
			if ( p.size_of_facets() == 0 ) continue;
			nfNefBox::Mesh part;
			CGAL::copy_face_graph(p, part);
			CGAL::Polygon_mesh_processing::triangulate_faces(part);
			CGAL::copy_face_graph(part, out);   /* 別連結成分として追記 */
			++nShell;
		}
		if ( nShell > 0 ) ++nPart;
	}
	if ( nPart == 0 ) self->set_last_error("no shell of the SNC could be turned into facets");
	return nPart > 0;
}

bool
nfNefMesh::write_to(const char *path, const char *)
{
	nfNefBox::Mesh m;
	if ( ! nf_to_mesh(this, m) ) return false;         /* 非有界/非多様体は書けない = 黙らずに失敗 */
	return CGAL::IO::write_polygon_mesh(path, m, CGAL::parameters::stream_precision(17));
}

/* ---- ブール: nfNefBox::Nef のまま (overlay + 選択関数)。型維持 ---- */
sPtr<nfNefMesh>
nfNefMesh::op_union(sPtr<nfNefMesh> o)
{
	if ( ! o.is_notNull() ) return thNULL;
	return nf_make_nef(*this, box_->n + nf_nef(o));
}

sPtr<nfNefMesh>
nfNefMesh::op_intersection(sPtr<nfNefMesh> o)
{
	if ( ! o.is_notNull() ) return thNULL;
	return nf_make_nef(*this, box_->n * nf_nef(o));
}

sPtr<nfNefMesh>
nfNefMesh::op_difference(sPtr<nfNefMesh> o)
{
	if ( ! o.is_notNull() ) return thNULL;
	return nf_make_nef(*this, box_->n - nf_nef(o));
}

sPtr<nfNefMesh>
nfNefMesh::op_complement()
{
	return nf_make_nef(*this, box_->n.complement());
}

/* ---- アフィン変換 (行優先 double[12] = 3x4) — #3486 ----------------------------
 * ★ **Nef のまま**変換する (型維持・境界表現へ戻さない)。非有界な Nef も通る。
 * ⚠ 反射 (det<0) の向き補正は **CGAL の Nef_polyhedron_3::transform が中でやる**ので、
 *   cgMesh3D::apply_affine のように reverse_face_orientations を後から当ててはいけない
 *   (当てると二重に裏返る)。Nef は面の向きを頂点ごとの **球面地図の巡回順**で持っており、
 *   transform() は SM_decorator 経由でそこも直す。実測 2026-09-05:
 *     [0,3]x[0,1]^2 を x 鏡像 → 体積 3 のまま -x 側に移動・後段の union/difference も正しい
 *   (素の頂点書き換えだと補集合に化ける)。
 * ⚠ 行列の要素は double のまま nfNefBox::K::FT へ入れる。EPECK なのは「その行列を厳密に適用する」
 *   ところまでで、回転の cos/sin が厳密になるわけではない。 */
sPtr<nfNefMesh>
nfNefMesh::apply_affine(const double e[12])
{
	/* NB: nfNefBox::K::FT(e[i]) を ctor に直接並べると most vexing parse (関数宣言化) になるので、
	 * いったん FT の配列へ移してから添字式で渡す (cgMesh3D::apply_affine と同じ理由)。 */
	nfNefBox::K::FT f[12];
	for ( int i = 0 ; i < 12 ; ++i )
		f[i] = nfNefBox::K::FT(e[i]);
	CGAL::Aff_transformation_3<nfNefBox::K> aff(
	    f[0], f[1], f[2],  f[3],
	    f[4], f[5], f[6],  f[7],
	    f[8], f[9], f[10], f[11] );

	nfNefBox::Nef n = box_->n;   /* Handle_for なので rep 共有。transform() が is_shared() を見て COW する */
	n.transform(aff);
	return nf_make_nef(*this, n);
}

/* ---- Minkowski 和 A ⊕ B (#3440) ----
 * ★CGAL::minkowski_sum_3 は引数を **非 const 参照**で取り、非凸なら**入力自身を凸分解で書き換える**
 *   (ヘッダの \post に明記)。ここで渡すのは cache 由来の共有された値なので、**ハンドルのコピー**を
 *   作って渡す。Nef_polyhedron_3 は Handle_for なので、コピーは rep 共有だが
 *   convex_decomposition_3 が通る delegate() が `if (is_shared()) clone_rep()` で
 *   **COW する** (Nef_polyhedron_3.h:1135)。よって原本 box_->n / nf_nef(o) は壊れない。
 * ★有界性の検査は呼び側 (nfaMinkowski) の責務。CGAL は非有界を渡されると stderr に
 *   "first parameter is an infinite point set" と出して**片方をそのまま返す**ので、
 *   ここまで来たら黙って嘘の答えになる。 */
/* ---- ★ 凸包 (#3511) ---------------------------------------------------------
 * SNC は凸包を直接は作れないので、**頂点を抜いて CGAL::convex_hull_3 へ渡し、出来た境界を
 * set_from_mesh で Nef へ戻す**。n 項は点集合を足し合わせるだけなので 1 回で済む
 * (minkowski と違い凸分解もブールも通らない = nef としては安い op)。
 *
 * ⚠ **非有界は弾く**。補集合のような非有界 Nef は「頂点の凸包」では表せない
 *   (op_minkowski と同じ理由)。黙って有界の答えを返すと嘘になる。
 * ⚠ 空の Nef は頂点を 1 つも出さない = 点集合に何も足さない (hull の中立元として正しい)。
 *   全部が空なら点が 0 個になるので、そこはエラーにする (空集合の凸包は空だが、それを
 *   「立体でない」と言い分ける手段が下流に無いため)。
 * ⚠ 退化 (1 点 / 1 直線上 / 1 平面上) は閉じた境界にならない。set_from_mesh は面 0 なら
 *   空 Nef を返すので、ここで面数を見て明示エラーにする。 */
sPtr<nfNefMesh>
nf_hull_from_args(sArray<sPtr<pigData> > *args, const char **errmsg)
{
	int na = ( args != 0 ) ? args->length() : 0;
	if ( na < 1 ) { *errmsg = "needs at least one Nef mesh"; return thNULL; }
	std::vector<nfNefBox::Point_3> pts;
	/* ★ #3559: 結果は **1 つ目の被演算子と同じ変種**で作る (n 項なので受け手が居ない)。
	 *   ⚠ 混在は起きない — sig が 1 つの型に縛っており、他カーネルの値は wire が
	 *     このモジュールの型として実体化するため。 */
	sPtr<nfNefMesh> proto;
	for ( int i = 0 ; i < na ; ++i ) {
		sPtr<nfNefMesh> mi = sPtr<nfNefMesh>::d_cast((*args)[i]);
		if ( ! mi.is_notNull() ) { *errmsg = "needs Nef meshes"; return thNULL; }
		if ( ! proto.is_notNull() ) proto = mi;
		if ( ! mi->is_bounded() ) {
			*errmsg = "an unbounded Nef has no convex hull (e.g. the result of complement)";
			return thNULL;
		}
		const nfNefBox::Nef &n = nf_nef(mi);
		for ( nfNefBox::Nef::Vertex_const_iterator v = n.vertices_begin() ;
		      v != n.vertices_end() ; ++v )
			pts.push_back(v->point());
	}
	if ( pts.empty() ) { *errmsg = "the operands have no vertices"; return thNULL; }

	nfNefBox::Mesh hull;
	try {
		CGAL::convex_hull_3(pts.begin(), pts.end(), hull);
	} catch ( const std::exception& ) {
		*errmsg = "CGAL could not build the convex hull";
		return thNULL;
	}
	if ( hull.number_of_faces() < 4 || ! CGAL::is_closed(hull) ) {
		*errmsg = "the points are degenerate (a single point, all on one line, or all on one plane), "
		          "so the convex hull is not a solid";
		return thNULL;
	}
	sPtr<nfNefMesh> out = proto->make_empty();
	nf_set_from_mesh(out, hull);
	return out;
}

sPtr<nfNefMesh>
nfNefMesh::op_minkowski(sPtr<nfNefMesh> o)
{
	if ( ! o.is_notNull() ) return thNULL;
	/* 空集合との和は空 (CGAL に渡す前に畳む。凸分解を走らせる意味が無い)。 */
	if ( box_->n.is_empty() || nf_nef(o).is_empty() ) return nf_make_nef(*this, nfNefBox::Nef());
	nfNefBox::Nef a(box_->n), b(nf_nef(o));
	return nf_make_nef(*this, CGAL::minkowski_sum_3(a, b));
}

/* ---- 凸分解 (#3441) ----
 * @CGAL::convex_decomposition_3@ は Nef を **その場で** 凸片へ割る (仕切り面が入るので
 * @is_simple()@ は偽になる)。共有された値を壊さないようコピーに対して行う。
 * ★用途: 物理エンジンの凸コリジョン形状・3D プリントのサポート生成。Minkowski 和が内部で
 *   使っているものと同じ分解で、offset が重いのはここのペア数が m×n で効くため。 */
sPtr<nfNefMesh>
nfNefMesh::op_convex_decomposition()
{
	if ( ! is_bounded() ) return thNULL;   /* 呼び側が明示エラーにする */
	nfNefBox::Nef a(box_->n);
	CGAL::convex_decomposition_3(a);
	return nf_make_nef(*this, a);
}

/* ---- 塊 (part) の取り出し (#3441 追補) ----
 * 塊 = SNC の **marked volume**。最初の volume は無限体積なので飛ばす。
 * 空洞は marked でないので塊に数えない (空洞つき立体は 1 つの塊)。 */
int
nfNefMesh::op_nparts()
{
	int n = 0;
	nfNefBox::Nef::Volume_const_iterator ci = box_->n.volumes_begin();
	for ( ++ci ; ci != box_->n.volumes_end() ; ++ci )
		if ( ci->mark() ) ++n;
	return n;
}

/* i 番目の塊 (0 始まり)。範囲外は null (呼び側が明示エラー)。
 * ★その volume の **全シェル** を集める: shells_begin() の 1 本目が外殻で、以降は空洞の境界。
 *   集めた殻を set_from_mesh に渡すと入れ子の深さで組み直されるので、空洞を持つ塊も正しく出る。 */
sPtr<nfNefMesh>
nfNefMesh::op_part(int i)
{
	typedef CGAL::Polyhedron_3<nfNefBox::K> Poly;
	if ( i < 0 ) return thNULL;
	int n = 0;
	nfNefBox::Nef::Volume_const_iterator ci = box_->n.volumes_begin();
	for ( ++ci ; ci != box_->n.volumes_end() ; ++ci ) {
		if ( ! ci->mark() ) continue;
		if ( n++ != i ) continue;
		nfNefBox::Mesh acc;
		for ( nfNefBox::Nef::Shell_entry_const_iterator si = ci->shells_begin() ;
		      si != ci->shells_end() ; ++si ) {
			Poly p;
			box_->n.convert_inner_shell_to_polyhedron(si, p);
			if ( p.size_of_facets() == 0 ) continue;
			nfNefBox::Mesh part;
			CGAL::copy_face_graph(p, part);
			CGAL::Polygon_mesh_processing::triangulate_faces(part);
			CGAL::copy_face_graph(part, acc);
		}
		if ( acc.number_of_faces() == 0 ) return thNULL;
		sPtr<nfNefMesh> out = make_empty();
		nf_set_from_mesh(out, acc);
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
sPtr<nfNefMesh>
nfNefMesh::op_unify_shells()
{
	nfNefBox::Mesh m;
	if ( ! nf_to_mesh(this, m) ) return thNULL;   /* 非有界などは呼び側が明示エラー */
	sPtr<nfNefMesh> out = make_empty();
	nf_set_from_mesh(out, m);
	return out;
}

/* ---- offset 用の近似球 (#3440 の 2: cgMesh3D.cpp の cga_make_icosphere をそのまま移設) ----
 * 半径 r・細分化 subdiv の icosphere (icosahedron を Loop 細分して球面へ投影)。
 * subdiv=0=icosahedron(20 面・粗い)・1=80 面・2=320 面… 大きいほど滑らかだが Minkowski が重い。
 * 投影は double 正規化 (sqrt) → nfNefBox::K::FT 格納 (近似形状・有理座標。回転と同じ方針)。
 * ★sphere/icosphere op が使う測地球 (common/geodesic.h) とは**別の生成器**である。offset 専用で、
 *   cgal.so 時代の出力 (頂点数・面数) をそのまま引き継ぐために同じ作り方を維持する。 */
static void
nf_make_offset_ball(nfNefBox::Mesh &ball, double r, int subdiv)
{
	typedef nfNefBox::K K;
	CGAL::make_icosahedron<nfNefBox::Mesh, nfNefBox::K::Point_3>(ball, nfNefBox::K::Point_3(0,0,0), nfNefBox::K::FT(r));
	CGAL::Polygon_mesh_processing::triangulate_faces(ball);
	for ( int i = 0 ; i < subdiv ; ++i )
		CGAL::Subdivision_method_3::Loop_subdivision(ball, CGAL::parameters::number_of_iterations(1));
	if ( subdiv > 0 ) {
		for ( nfNefBox::Mesh::Vertex_index v : ball.vertices() ) {
			double x = CGAL::to_double(ball.point(v).x());
			double y = CGAL::to_double(ball.point(v).y());
			double z = CGAL::to_double(ball.point(v).z());
			double L = std::sqrt(x*x + y*y + z*z);
			if ( L > 0 ) { double s = r / L;
				ball.point(v) = nfNefBox::K::Point_3(nfNefBox::K::FT(x*s), nfNefBox::K::FT(y*s), nfNefBox::K::FT(z*s)); }
		}
		CGAL::Polygon_mesh_processing::triangulate_faces(ball);
	}
}

/* ---- 3D オフセット (#3440 の 2) ----
 * d>0: A ⊕ ball(d)。d<0: 補集合トリック erode(A,r) = A − dilate(box−A, ball(r))。
 * ★bbox は **SNC の頂点**から取る (境界表現へ落とさない = 型維持・非 2-多様体でも取れる)。
 *   有界化のため margin(>r) 拡大した箱を使う (箱の壁付近の誤侵食を避ける)。 */
sPtr<nfNefMesh>
nfNefMesh::op_offset(double d, int subdiv)
{
	if ( d == 0.0 ) return nf_make_nef(*this, box_->n);
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
	if ( subdiv > 6 ) return sPtr<nfNefMesh>();
	double r = ( d > 0.0 ) ? d : -d;

	nfNefBox::Mesh ballMesh;
	nf_make_offset_ball(ballMesh, r, subdiv);
	nfNefBox::Nef ball(ballMesh);

	if ( d > 0.0 ) {
		nfNefBox::Nef a(box_->n);
		return nf_make_nef(*this, CGAL::minkowski_sum_3(a, ball));
	}

	/* 収縮: A の bbox を 2r 拡大した箱 B を作り、B − A を膨張させて A から引く。 */
	double minx=1e300, miny=1e300, minz=1e300, maxx=-1e300, maxy=-1e300, maxz=-1e300;
	for ( nfNefBox::Nef::Vertex_const_iterator v = box_->n.vertices_begin() ; v != box_->n.vertices_end() ; ++v ) {
		double x = CGAL::to_double(v->point().x());
		double y = CGAL::to_double(v->point().y());
		double z = CGAL::to_double(v->point().z());
		if ( x<minx ) minx=x; if ( x>maxx ) maxx=x;
		if ( y<miny ) miny=y; if ( y>maxy ) maxy=y;
		if ( z<minz ) minz=z; if ( z>maxz ) maxz=z;
	}
	if ( minx > maxx ) return nf_make_nef(*this, nfNefBox::Nef());   /* 空 */
	double mg = 2.0 * r;   /* margin > r */
	minx-=mg; miny-=mg; minz-=mg; maxx+=mg; maxy+=mg; maxz+=mg;
	nfNefBox::Mesh boxMesh;
	CGAL::make_hexahedron(
	    nfNefBox::Point_3(nfNefBox::K::FT(minx),nfNefBox::K::FT(miny),nfNefBox::K::FT(minz)), nfNefBox::Point_3(nfNefBox::K::FT(maxx),nfNefBox::K::FT(miny),nfNefBox::K::FT(minz)),
	    nfNefBox::Point_3(nfNefBox::K::FT(maxx),nfNefBox::K::FT(maxy),nfNefBox::K::FT(minz)), nfNefBox::Point_3(nfNefBox::K::FT(minx),nfNefBox::K::FT(maxy),nfNefBox::K::FT(minz)),
	    nfNefBox::Point_3(nfNefBox::K::FT(minx),nfNefBox::K::FT(miny),nfNefBox::K::FT(maxz)), nfNefBox::Point_3(nfNefBox::K::FT(maxx),nfNefBox::K::FT(miny),nfNefBox::K::FT(maxz)),
	    nfNefBox::Point_3(nfNefBox::K::FT(maxx),nfNefBox::K::FT(maxy),nfNefBox::K::FT(maxz)), nfNefBox::Point_3(nfNefBox::K::FT(minx),nfNefBox::K::FT(maxy),nfNefBox::K::FT(maxz)), boxMesh);
	CGAL::Polygon_mesh_processing::triangulate_faces(boxMesh);

	nfNefBox::Nef box(boxMesh);
	nfNefBox::Nef outside = box - box_->n;                                   /* 箱内の外側 (A の補集合を有界化) */
	nfNefBox::Nef dilated = CGAL::minkowski_sum_3(outside, ball);       /* 外側を r 膨張 (A 内へ r シェル侵入) */
	return nf_make_nef(*this, box_->n - dilated);                      /* A から r シェルを除く = 収縮 */
}


/* ---- n 項ブール (#3436 P4) --------------------------------------------------
 * CGAL の Nef 演算子は二項なので、ここで逐次に畳む。★ 効くのは「中間 SNC を wire へ
 * 直列化して読み直す往復が消える」ところで、geogram/occt の「1 回の交差計算」とは効き方が違う
 * (P4 の対比ではここを区別する)。 */
sPtr<nfNefMesh>
nfNefMesh::bool_from_args(sArray<sPtr<pigData> > *args, const char *kind, const char **errmsg)
{
	int na = ( args != 0 ) ? args->length() : 0;
	if ( na < 2 ) { *errmsg = "needs at least two Nef meshes"; return sPtr<nfNefMesh>(); }
	sArray<sPtr<nfNefMesh> > ops;
	ops.length(na);
	for ( int i = 0 ; i < na ; ++i ) {
		ops[i] = sPtr<nfNefMesh>::d_cast((*args)[i]);
		if ( ! ops[i].is_notNull() ) { *errmsg = "needs Nef meshes"; return sPtr<nfNefMesh>(); }
	}
	sPtr<nfNefMesh> acc = ops[0];
	for ( int i = 1 ; i < na ; ++i ) {
		if      ( ::strcmp(kind, "union") == 0 )        acc = acc->op_union(ops[i]);
		else if ( ::strcmp(kind, "intersection") == 0 ) acc = acc->op_intersection(ops[i]);
		else                                            acc = acc->op_difference(ops[i]);
		if ( ! acc.is_notNull() ) { *errmsg = "boolean failed"; return sPtr<nfNefMesh>(); }
	}
	return acc;
}
