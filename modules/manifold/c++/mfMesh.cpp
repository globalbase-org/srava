/*
 * mfMesh — Manifold 幾何ラッパの実装(cgMesh3D のミラー・CGAL 非依存)。
 */
#include	"mf/c++/mfMesh.h"
#include	"common/meshprops.h"   /* valid の共通定義 (#3487) */
#include	"ts2/c++/stdString.h"
#include	"common/blockframe.h"   /* ★ #3507: SNC のブロック列を終端まで読み捨てる */

#include	<cstdio>
#include	<cstring>
#include	<cstdint>
#include	<cstddef>
#include	<cstdlib>    /* getenv (decode 内訳プローブ) */
#include	<cmath>
#include	<map>
#include	<vector>
#include	<utility>
#include	<algorithm>   /* ★ #3511: loft の標本パラメータの整列 */
#include	"manifold/polygon.h"   /* ★ #3511: 蓋の三角形分割 (TriangulateIdx) */
#include	"common/geodesic.h"   /* sphere/icosphere の測地球生成 (cgal と共通) */
#include	"common/mesh3mf.h"   /* AMF/3MF ライタ (cgal と共通) */
#include	"common/exact_wire.h"   /* CGAL 厳密 wire の有理数文字列パーサ (geogram と共通) */
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
/* パーサ本体は src/h/common/exact_wire.h (geogram.so と共通)。ここでは短い別名だけ置く。 */
static inline std::string get_str_field(mfChunkSource &s) { return srava_exact::get_str_field(s); }
static inline double parse_rational_d(const std::string &s) { return srava_exact::parse_rational_d(s); }

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

/* ★ #3433: nef_hybrid の出力 (4CC "NEFB") を読む。payload 先頭 1 バイト = 形式
 *   (nfMesh.h の NF_FORM_*・安定契約としてここに inline 再現):
 *     1 = 厳密境界 → cg の "MESH" と同一フレーミングなので decode_mesh_exact をそのまま使える
 *     0 = SNC      → パースに CGAL が要る。manifold.so は **CGAL 非依存 (GPL 非汚染)** を設計として
 *                    守っているので読まない。黙って空 mesh にせず理由つきで落とす。
 *   ⚠ 受理する 4CC は "NEFB" **だけ**で、nef_snc の "NEF3" は create_for_meta が弾く (#3478 で対処予定)。
 *   ⚠ 旧コメントは迂回路として cast("cg-mesh3d", x) を勧めていたが **成立しない** —
 *     cgal.so も SNC を読まないので同じ理由で落ちる (2026-09-05 実測)。形式 0 で書かれる値は
 *     非有界か非多様体で、cg にも mf にも表現が無い。
 *     ⚠ #3559: cgal.so 側の理由は「CGAL Nef に依存しない方針 (#3440)」から「reader を
 *       持たない」へ変わった (libsrava_cg は Nef を含むようになった)。**manifold.so 自身は
 *       CGAL 非依存のまま**で、ここの結論は動かない。 */
void
mfMesh::decode_nef3(mfChunkSource &src)
{
	uint8_t form = 0;
	src.pull(&form, 1);
	if ( form == 1 ) { decode_mesh_exact(src); return; }   /* NF_FORM_BOUNDARY */
	/* ★ #3478: NF_FORM_SNC_BND (=2) は [SNC のブロック列][厳密境界]。SNC は読めないが、
	 *   終端まで読み捨てれば後半は form 1 と同一フレーミングなので同じ経路で読める。 */
	if ( form == 2 ) {
		/* ★ #3507: SNC は [u32 blocklen][block]…[u32 0] のブロック列になった。
		 *   前置された全長で読み飛ばすのではなく、**終端まで読み捨てる**。 */
		blockframe::ibuf<mfChunkSource> ib(src);
		if ( ! ib.drain() ) {
			set_decode_err("the stored SNC frame is corrupt (implausible block length)");
			return;
		}
		decode_mesh_exact(src);
		return;
	}
	/* ★ 2026-09-06: 「非 2-多様体だから」は **もう理由ではない** — nef は marked volume ごとの
	 *   全シェルから境界を作れるので、有界なら非 2-多様体でも境界を併記する。
	 *   ここまで来るのは **境界表現がそもそも取れない値** (非有界) だけ。 */
	set_decode_err("the Nef value is stored as a bare SNC with no boundary section "
	               "(it has no boundary representation at all — e.g. it is unbounded, "
	               "like the result of complement), which manifold cannot represent "
	               "and cannot parse without CGAL");
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
	/* ★★★ #3527 段 6 (2026-09-17): **PLY2 の残りの節を読む**。regions のあとに
	 *   ガイド層 (開ポリライン群) の節が続き、その後ろに枠 ("MARF") が置かれている
	 *   (cgMesh2D.cpp:217 / 240 の順序)。
	 * ⚠⚠ ここで止まっていたため、**cast("mf-face3d", <cg-face3d>) が枠を黙って捨て**、
	 *   空間に置いた 2D が z=0 へ戻っていた。#3526 で mf→cg の向きは直したが、
	 *   **cg→mf の向きが直っていなかった** (段 6 の cast 監査で発見。gu も同じ形だった)。
	 *   ⇒ #3533 規約②「降格は cast のみ」を、cast 自身が *黙って* 破っていた。
	 * ⚠ ガイドは **開いた折れ線**で mfCross には表現が無い ⇒ 在ったら **断る**。
	 *   黙って捨てると nverts が cgal と食い違う (cgal の op_verts はガイドの点も歩く)。 */
	if ( src.more() ) {
		const uint32_t nguide = get_u32(src);
		if ( nguide != 0 ) {
			set_decode_err("this 2D value carries guide polylines (from line(...)), which this "
			               "representation cannot hold; drop them before converting");
			return;
		}
		if ( src.more() ) {
			const uint32_t mark = get_u32(src);
			if ( mark == 0x4652414dU ) {
				for ( int i = 0 ; i < 3 ; ++i ) fo_[i] = get_f64(src);
				for ( int i = 0 ; i < 3 ; ++i ) fu_[i] = get_f64(src);
				for ( int i = 0 ; i < 3 ; ++i ) fv_[i] = get_f64(src);
				placed_ = 1;   /* 節が在る = face3d (encode と対) */
			} else {
				set_decode_err("the exact 2D cache has a trailing section that is not a frame marker");
				return;
			}
		}
	}
	/* ★ #3529: cast 経路 (厳密→double) は **今までどおり正規化を通す**。
	 *   切り下げの丸めで「無かった自己交差」が生まれうるので、ここは Union が要る。
	 *   ⇒ 派生 c_ を作ってから、真実 p_ をその正準形から取る (built_ を立てる)。 */
	c_ = manifold::CrossSection(ps, manifold::CrossSection::FillRule::NonZero);
	p_ = c_.ToPolygons();
	built_ = true;
}

void
mfMesh::decode(mfChunkSource &src)
{
	if ( meshExactInput_ ) { decode_mesh_exact(src); return; }   /* ★ CGAL MESH → double(Phase D) */
	if ( nef3Input_ )      { decode_nef3(src);       return; }   /* ★ NEF3 → 境界形式なら読む(#3433) */
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

/* ---- ★ Minkowski 和 (#3511)。但し書きは mfMesh.h を参照 ---- */
sPtr<mfMesh>
mfMesh::op_minkowski(sPtr<mfMesh> b)
{
	if ( ! b.is_notNull() ) return sPtr<mfMesh>();
	/* 空との和は空 (三角形ごとの凸包を 1 つも作らずに済ませる)。 */
	if ( m_.IsEmpty() || b->m_.IsEmpty() ) return thNEW(mfMesh,(manifold::Manifold()));
	return thNEW(mfMesh,(m_.MinkowskiSum(b->m_)));
}

/* ---- n 項ブール (#3436 P4) --------------------------------------------------
 * manifold::Manifold::BatchBoolean は n 個をまとめて 1 つの CsgOpNode にする (上限なし)。
 * 二項の +/^/- はその 2 オペランド版。★ Subtract は CsgOpNode が「先頭は正・残りは負」
 * = a - (b ∪ c ∪ …) として畳むので、左 fold の a-b-c と一致する。 */
sPtr<mfGeom>
mf_bool_from_args(sArray<sPtr<pigData> > *args, const char *kind, const char **errmsg)
{
	int na = ( args != 0 ) ? args->length() : 0;
	if ( na < 2 ) { *errmsg = "needs at least two meshes"; return sPtr<mfGeom>(); }
	int isU = ( ::strcmp(kind, "union") == 0 ), isI = ( ::strcmp(kind, "intersection") == 0 );
	manifold::OpType op = isU ? manifold::OpType::Add
	                    : isI ? manifold::OpType::Intersect : manifold::OpType::Subtract;

	if ( sPtr<mfMesh>::d_cast((*args)[0]).is_notNull() ) {           /* 3D */
		std::vector<manifold::Manifold> v;
		v.reserve((size_t)na);
		for ( int i = 0 ; i < na ; ++i ) {
			sPtr<mfMesh> mi = sPtr<mfMesh>::d_cast((*args)[i]);
			if ( ! mi.is_notNull() ) { *errmsg = "needs meshes of one kind (3D)"; return sPtr<mfGeom>(); }
			v.push_back(mi->manifold());
		}
		if ( na == 2 )   /* 既存キャッシュを byte 不変に保つため 2 項は従来どおり */
			return thNEW(mfMesh,( isU ? (v[0] + v[1]) : isI ? (v[0] ^ v[1]) : (v[0] - v[1]) ));
		return thNEW(mfMesh,(manifold::Manifold::BatchBoolean(v, op)));
	}
	if ( sPtr<mfCross>::d_cast((*args)[0]).is_notNull() ) {          /* 2D */
		std::vector<manifold::CrossSection> v;
		v.reserve((size_t)na);
		sPtr<mfCross> c0 = sPtr<mfCross>::d_cast((*args)[0]);
		int anyPlaced = 0;   /* ★ #3533 規約③: どれかが face3d なら結果も face3d */
		for ( int i = 0 ; i < na ; ++i ) {
			sPtr<mfCross> ci = sPtr<mfCross>::d_cast((*args)[i]);
			if ( ! ci.is_notNull() ) { *errmsg = "needs meshes of one kind (2D)"; return sPtr<mfGeom>(); }
			if ( ci->is_placed() ) anyPlaced = 1;   /* ⚠ 表し直す前の値で見る */
			/* ★★ #3526: 枠が違うときの扱いは **2 段**。
			 *   ① **同じ平面で軸の取り方だけが違う** なら、先頭の枠で **表し直してから** 計算する
			 *      (幾何は動かない・局所座標の読み方だけ揃える)。⇒ 使う側は枠を意識しなくてよい。
			 *      ★ 揃える先は **先頭の被演算子の枠** (決定的にするため)。
			 *   ② **本当に別の平面** なら明示エラー — 別の平面にある 2 つの平面領域の交わりは
			 *      *線分以下* に落ちるので 2D 領域として表せない (ひさ判断 ②)。
			 *   ⚠ 2940662 で潰した「黙って射影する」と同じ穴を作り直さないこと。
			 *   ⚠ 枠が完全に一致しているときは表し直さない (既存の値を動かさないため)。 */
			sPtr<mfCross> use = ci;
			if ( ! c0->same_frame(ci->frame_o(), ci->frame_u(), ci->frame_v()) ) {
				use = ci->reexpress(c0->frame_o(), c0->frame_u(), c0->frame_v());
				if ( ! use.is_notNull() ) {
					*errmsg = "the 2D regions are on different planes, so the result is not a 2D "
					          "region (it would drop to a segment); move them onto one plane first";
					return sPtr<mfGeom>();
				}
			}
			v.push_back(use->cross());
		}
		sPtr<mfCross> r = ( na == 2 )
		    ? thNEW(mfCross,( isU ? (v[0] + v[1]) : isI ? (v[0] ^ v[1]) : (v[0] - v[1]) ))
		    : thNEW(mfCross,(manifold::CrossSection::BatchBoolean(v, op)));
		r->set_frame(c0->frame_o(), c0->frame_u(), c0->frame_v());   /* 枠は引き継ぐ */
		r->set_placed(anyPlaced);   /* ★ #3533 規約③: どれかが face3d なら結果も face3d */
		return sPtr<mfGeom>::d_cast(r);
	}
	*errmsg = "needs two meshes";
	return sPtr<mfGeom>();
}

/* ★ #3512: refine(m, len) — 形を厳密に保ったまま面密度だけ上げる。
 *
 * ★ Manifold::RefineToLength は「長い辺を len くらいの断片に割り、**内部頂点も足して**
 *   三角形分割を揃える」(上流のコメント)。新しい頂点は既存頂点の線形補間なので、
 *   **体積・面積は bit 単位で不変**になる (実測: 単位箱 1 / 6・1x1x0.02 の板 0.02 / 2.08)。
 * ⚠ **remesh ではない**。頂点を動かさず辺も潰さないので、針状三角形は割られても針状のまま
 *   (板の最小角 1.15° が不変)。単位箱では 45° → 23.2° と **悪化**さえする。
 * ★ eager op なので **ExecutionContext で中断できる** (deferred 木の Status() 経由ではなく、
 *   WithContext(ctx).RefineToLength(len) が上流の作法)。
 */
/* Manifold の最長辺 (refine の約束「全辺が len 以下」を確かめるのに使う)。 */
static double
mf_max_edge(const manifold::Manifold &m)
{
	manifold::MeshGL64 g = m.GetMeshGL64();
	const std::size_t np = (std::size_t)g.numProp;
	double mx = 0.0;
	for ( std::size_t t = 0 ; t + 2 < g.triVerts.size() ; t += 3 )
		for ( int k = 0 ; k < 3 ; ++k ) {
			std::size_t a = (std::size_t)g.triVerts[t + (std::size_t)k];
			std::size_t b = (std::size_t)g.triVerts[t + (std::size_t)((k + 1) % 3)];
			double d0 = g.vertProperties[a*np+0] - g.vertProperties[b*np+0];
			double d1 = g.vertProperties[a*np+1] - g.vertProperties[b*np+1];
			double d2 = g.vertProperties[a*np+2] - g.vertProperties[b*np+2];
			double L = std::sqrt(d0*d0 + d1*d1 + d2*d2);
			if ( L > mx ) mx = L;
		}
	return mx;
}

sPtr<mfGeom>
mf_refine_to_length(sPtr<mfMesh> in, double len, const pigBreak &brk, const char **errmsg)
{
	if ( ! ( len > 0 ) ) { *errmsg = "the target edge length must be > 0"; return sPtr<mfGeom>(); }

	manifold::ExecutionContext ctx;
	/* 押し出し口 (force_eval と同じ作法)。dtor が返るまでの間だけ cancel が届く。 */
	pigBreakHook hook(&brk, [&ctx]{ ctx.Cancel(); });

	if ( mf_max_edge(in->manifold()) <= len )   /* 既に十分細かい = そのまま返す */
		return thNEW(mfMesh,(in->manifold()));

	/* ⚠⚠ **上流に「len 以下」の保証は無い**。理由は 2 つ重なっている:
	 *   ① 分割数を **切り捨て**で決める (`(int)(length / len)`)。長さ 1.414 の辺に len=0.25 を
	 *      頼むと n=5 = 断片 0.283 になる。
	 *   ② 辺を割った後に **内部頂点を足して三角形分割を揃える**ので、*辺の分割数からは
	 *      決まらない* 内部の辺ができる。実測で最長辺は要求の **1.4〜1.65 倍**に散った
	 *      (len 0.25 → 0.357 / 0.2 → 0.325 / 0.15 → 0.229)。
	 * ⇒ cgal 版 (相似分割なので必ず len 以下) と **同じ約束にならない**ので、
	 *   *作った結果を測って、超えていたら詰めて作り直す*。⚠ 面数は cgal 版より多くなるが、
	 *   「全辺 len 以下」という **op の約束の方を揃える** (面数はもともと実装間で一致しない)。 */
	double lenq = len;
	manifold::Manifold r;
	for ( int attempt = 0 ; attempt < 6 ; ++attempt ) {
		r = in->manifold().WithContext(ctx).RefineToLength(lenq);
		const manifold::Manifold::Error st = r.Status();
		if ( st == manifold::Manifold::Error::Cancelled ) { *errmsg = "aborted (interrupted)"; return sPtr<mfGeom>(); }
		if ( st != manifold::Manifold::Error::NoError )   { *errmsg = "manifold could not refine this mesh"; return sPtr<mfGeom>(); }
		double mx = mf_max_edge(r);
		if ( mx <= len ) break;
		/* 観測した超過ぶんだけ詰める (0.9 は取りこぼしの余裕)。単位箱 len=0.25 では
		 * 0.25 → 0.359 → 0.269 → 0.253 → 0.229 と **3 回**で収まった。⚠ 比は一定ではない
		 * (内部の三角形分割の当たり方で変わる) ので、閉じた式では決められない。 */
		double shrink = len / mx * 0.9;
		if ( shrink > 0.9 ) shrink = 0.9;
		lenq *= shrink;
	}
	if ( r.NumTri() == 0 ) { *errmsg = "the mesh has no faces"; return sPtr<mfGeom>(); }
	return thNEW(mfMesh,(r));
}

/* ★ #3512 続き: simplify_cleanup(m, tol) — 許容差以下の特徴を潰して掃除する。
 *
 * ★ 上流 Manifold::Simplify の約束が明快: *「結果は元の頂点の部分集合で、どの面も tol 未満
 *   しか動かない」*。⇒ **新しい頂点を作らない有界誤差の間引き**で、ブール後に残る極小辺や
 *   針状三角形を落とすのに向く。
 * ⚠ `simplify(m, n)` (cgal・面数を指定して形を保つ) とは **約束が違う**ので別名にしてある。
 *   実測 (球 r=1・2048 面): tol=0.01 → 2012 面 / 0.03 → 752 面 (体積 −2%) /
 *   0.1 → 180 面 (体積 −10.8%)。*形は動く* ので面数を振る測定軸には使えない。
 * ⚠ tol=0 は上流で「メッシュ自身の許容差を使う」意味になる (厳密に組んだ形では実質不変)。
 * ⚠ Simplify は **deferred op** なので ExecutionContext を見ない (manifold.h の一覧)。
 *   ⇒ 中断は呼び手が mf_eval_err で木の Status() から取る。ここで ctx を張っても無駄。
 */
sPtr<mfGeom>
mf_simplify_cleanup(sPtr<mfMesh> in, double tol, const char **errmsg)
{
	if ( tol < 0.0 ) { *errmsg = "the tolerance must be >= 0"; return sPtr<mfGeom>(); }
	return thNEW(mfMesh,(in->manifold().Simplify(tol)));
}

/* ---- ★ 凸包 (#3511) ---------------------------------------------------------
 * Manifold は @c Hull() (自分 1 個) と @c Hull(vector<Manifold>) (まとめて) を持つ。後者は
 * 全オペランドの頂点を 1 つの点集合にしてから QuickHull を回すので、**二項に分けて畳むより
 * 素直**かつ中間メッシュを作らない。2D は @c CrossSection::Hull が同じ構えで居る。
 *
 * ⚠ 空のオペランドは頂点を 1 つも出さないので、点集合には何も足さない = 中立元として振る舞う。
 *   ブールの空集合と違って「hull の中立元」は数学的にも正しいのでそのまま通す。
 * ⚠ 全部が空なら結果も空になる。エラーにはしない (空集合の凸包は空集合)。 */
sPtr<mfGeom>
mf_hull_from_args(sArray<sPtr<pigData> > *args, const char **errmsg)
{
	int na = ( args != 0 ) ? args->length() : 0;
	if ( na < 1 ) { *errmsg = "needs at least one mesh"; return sPtr<mfGeom>(); }

	/* ★★ #3533: 振り分けは **sig の行と 1 対 1** にする (cgal の @cg_hull_from_args@ と同じ規則)。
	 *   sig が決めた出力型とここが作る値の型がずれると、スタンプは mf-mesh3d なのに中身は
	 *   MFC2、という *黙って通るずれ* になる。規則は
	 *   「**3D が混ざるか、2 つ以上あって face3d が混ざれば 3D**」(cgal と同じ・ひさ裁定 2026-09-15)。
	 *   ★ 単項の face3d が 2D 側なのは、平面の中の凸包は平面から出ないから。
	 *   ⚠ 型が face3d なら幾何が同じ平面に居ても 3D へ行く。2D の凸包が欲しければ
	 *     @cast("mf-cross2d", …)@ で先に降ろす。 */
	int has3d = 0, n2d = 0, nplaced2d = 0;
	for ( int i = 0 ; i < na ; ++i ) {
		if ( sPtr<mfMesh>::d_cast((*args)[i]).is_notNull() ) { has3d = 1; continue; }
		sPtr<mfCross> ci = sPtr<mfCross>::d_cast((*args)[i]);
		if ( ci.is_notNull() ) { ++n2d; if ( ci->is_placed() ) ++nplaced2d; }
	}
	if ( ( has3d || ( na > 1 && nplaced2d > 0 ) ) && n2d > 0 ) {
		/* 2D が混ざる立体の凸包。★ 平面領域は面を持たないので @Manifold::Hull(points)@ へ
		 *   **world 座標の点**を渡す (立体側も頂点にばらす)。
		 *   ⚠ 局所座標のまま渡すと別の平面の図形が全部 z=0 に重なる = 黙って別の立体。 */
		/* ★★ #3533: **入力が全部 2D で 1 つの平面に載っているなら立体にならない** — 点を積む
		 *   前に構造で断る (cgal の @cg_hull_3d@ と同じ検査。⚠ 片方だけ置かないこと)。
		 *   ⚠ Manifold は内部 ε でたまたま捕まえていたが、それは *出力* を見る代理で、
		 *     厳密カーネル (cgal) では取り逃がす形だった (2026-09-15 実測)。⇒ 入力で見る。 */
		if ( ! has3d ) {
			sPtr<mfCross> ref;
			int coplanar = 1;
			for ( int i = 0 ; i < na ; ++i ) {
				sPtr<mfCross> ci = sPtr<mfCross>::d_cast((*args)[i]);
				if ( ! ci.is_notNull() ) continue;
				if ( ! ref.is_notNull() ) ref = ci;
				else if ( ! ref->same_plane(ci->frame_o(), ci->frame_u(), ci->frame_v()) ) coplanar = 0;
			}
			if ( coplanar ) {
				*errmsg = "all the operands are on one plane, so their convex hull is not a solid; "
				          "cast(\"mf-cross2d\", ...) the placed ones first so the whole call is a "
				          "2D hull";
				return sPtr<mfGeom>();
			}
		}
		std::vector<manifold::vec3> pts;
		for ( int i = 0 ; i < na ; ++i ) {
			sPtr<mfCross> ci = sPtr<mfCross>::d_cast((*args)[i]);
			if ( ci.is_notNull() ) {
				const manifold::Polygons &ps = ci->polys();
				for ( size_t r = 0 ; r < ps.size() ; ++r )
					for ( size_t k = 0 ; k < ps[r].size() ; ++k ) {
						double w[3];
						ci->to_world(ps[r][k].x, ps[r][k].y, w);
						pts.push_back(manifold::vec3(w[0], w[1], w[2]));
					}
				continue;
			}
			sPtr<mfMesh> mi = sPtr<mfMesh>::d_cast((*args)[i]);
			if ( ! mi.is_notNull() ) { *errmsg = "needs meshes or 2D regions"; return sPtr<mfGeom>(); }
			manifold::MeshGL64 g = mi->manifold().GetMeshGL64();
			const size_t np = ( g.numProp > 0 ) ? (size_t)g.numProp : 3;
			for ( size_t k = 0 ; k + 2 < g.vertProperties.size() ; k += np )
				pts.push_back(manifold::vec3(g.vertProperties[k],
				                             g.vertProperties[k+1],
				                             g.vertProperties[k+2]));
		}
		if ( pts.empty() ) { *errmsg = "the operands have no points"; return sPtr<mfGeom>(); }
		sPtr<mfMesh> r = thNEW(mfMesh,(manifold::Manifold::Hull(pts)));
		/* ⚠ 全部が 1 つの平面に載っていると立体にならない。Manifold は空を返すので
		 *   *黙って空* にせず明示エラーにする (cgal の同じ枝と文言を揃える)。 */
		if ( r->manifold().IsEmpty() ) {
			*errmsg = "the points are degenerate (all on one plane), so the convex hull is not a "
			          "solid; if the operands really are on one plane, cast(\"mf-cross2d\", ...) the "
			          "placed ones first so the whole call is a 2D hull";
			return sPtr<mfGeom>();
		}
		return sPtr<mfGeom>::d_cast(r);
	}

	if ( sPtr<mfMesh>::d_cast((*args)[0]).is_notNull() ) {           /* 3D */
		std::vector<manifold::Manifold> v;
		v.reserve((size_t)na);
		for ( int i = 0 ; i < na ; ++i ) {
			sPtr<mfMesh> mi = sPtr<mfMesh>::d_cast((*args)[i]);
			if ( ! mi.is_notNull() ) { *errmsg = "needs meshes of one kind (3D)"; return sPtr<mfGeom>(); }
			v.push_back(mi->manifold());
		}
		if ( na == 1 )   /* 1 個なら自分の凸包 (vector 版と同じ結果・余計な copy を避ける) */
			return thNEW(mfMesh,(v[0].Hull()));
		return thNEW(mfMesh,(manifold::Manifold::Hull(v)));
	}
	if ( sPtr<mfCross>::d_cast((*args)[0]).is_notNull() ) {          /* 2D */
		std::vector<manifold::CrossSection> v;
		v.reserve((size_t)na);
		sPtr<mfCross> c0 = sPtr<mfCross>::d_cast((*args)[0]);
		int anyPlaced = 0;   /* ★ #3533 規約③: どれかが face3d なら結果も face3d */
		for ( int i = 0 ; i < na ; ++i ) {
			sPtr<mfCross> ci = sPtr<mfCross>::d_cast((*args)[i]);
			if ( ! ci.is_notNull() ) { *errmsg = "needs meshes of one kind (2D)"; return sPtr<mfGeom>(); }
			if ( ci->is_placed() ) anyPlaced = 1;   /* ⚠ 表し直す前の値で見る */
			/* ★★ #3526: hull は **点しか見ない** op なので、別の平面の 2D を混ぜると
			 *   「どの平面の上の凸包か」が決まらない — 局所座標を素で混ぜたら **黙って誤値**に
			 *   なる。⇒ ブールと同じ 2 段 (同じ平面なら表し直す / 別平面なら明示エラー)。
			 *   ⚠ ここは 2026-09-13 まで **検査が無かった** (cgal 側にだけ入れていた歯抜け)。 */
			sPtr<mfCross> use = ci;
			if ( ! c0->same_frame(ci->frame_o(), ci->frame_u(), ci->frame_v()) ) {
				use = ci->reexpress(c0->frame_o(), c0->frame_u(), c0->frame_v());
				if ( ! use.is_notNull() ) {
					*errmsg = "the 2D regions are on different planes, so their convex hull is "
					          "not a 2D region; move them onto one plane first";
					return sPtr<mfGeom>();
				}
			}
			v.push_back(use->cross());
		}
		sPtr<mfCross> r = ( na == 1 ) ? thNEW(mfCross,(v[0].Hull()))
		                              : thNEW(mfCross,(manifold::CrossSection::Hull(v)));
		r->set_frame(c0->frame_o(), c0->frame_u(), c0->frame_v());   /* ★ 枠は引き継ぐ */
		/* ★ #3533: ここへ来るのは単項の face3d か、全部 cross2d の n 項だけ。 */
		r->set_placed(anyPlaced);
		return sPtr<mfGeom>::d_cast(r);
	}
	*errmsg = "needs a mesh";
	return sPtr<mfGeom>();
}


/* ---- ★★ 線織 loft (#3511) — 断面の列を **直線で結ぶ** ------------------------
 *
 * ★★ 断面の **置き場所は op が決めない**。利用者が @transform@ で空間に置いた 2D を
 *   そのまま受ける (occt の loft と同じ規約)。これが書けるのは #3526 で mf-cross2d が
 *   **枠 (平面)** を持つようになったから。⇒ [[mesh2d-frame-3526]]
 *
 * ★ @loft_ruled@ だけを置く。なめらかな方 (@loft@) は解析曲面が要るのでメッシュ系には無い。
 *   これが #3511 で 2 つを別 op にした理由そのもの (#3510 の表に出る差)。
 *
 * ─── 対応づけの規約 (この op の唯一の設計判断・2026-09-13 に実測で決めた) ───
 *
 *   ① 各断面の外周を **弧長で正規化**する (0..1)。★ 枠が正規直交なので *局所座標の弧長 =
 *      世界座標の弧長* で、局所座標のまま測れる (#3526 で枠を正規直交に縛った効能の 2 つ目)。
 *   ② 標本パラメータは **全断面の頂点パラメータの和集合**。⇒ どの断面の頂点も 1 つも失われず、
 *      挿入される点は必ず元の稜の上に乗るので **形は厳密に保たれる**。
 *      ⚠ 断面ごとに別の標本を使うと稜が噛み合わず (T 字頂点) メッシュに裂け目ができる。
 *      ⚠ 頂点数の合計 x 断面数だけ頂点を作るので、上限を設けて明示エラーにする (下記)。
 *   ③ 始点 (パラメータの原点) は **前の断面の始点に世界座標で最も近い頂点**。
 *      ★ occt は @c CheckCompatibility@ が始点と向きを揃えてくれるが、メッシュ系には
 *        対応物が無いので自分で決める必要があった (#3511 起票時の「実装上の難所」)。
 *   ④ 巡回の向きは **軸 a = 末端の重心 − 先頭の重心** に対して揃える (法線が a と逆を向く
 *      断面は巡回順を反転)。⇒ 断面を裏返して置いても側面がねじれない。
 *
 *   ★ この規約が occt と **厳密に一致する**ことを実測で確認した (どちらも厳密な多面体になる組で、
 *     *断面の頂点数が違っても・断面を傾けても* 一致する)。突き合わせは test/srava_loft_ruled.sh。
 *
 * ⚠⚠ **ねじれ (面内回転) は一致しない** — ひさ判断で *そのまま受ける* (2026-09-13)。
 *   対応する稜が同一平面に無いと、その四角形は **双線形パッチ (双曲放物面)** になり
 *   三角形 2 枚では表せない。⇒ 断面 2 枚だけだと目に見える差が出る。中間断面を刻むと
 *   **2 次で収束する**。
 *   ⇒ **立場は @circle(r,segs)@ の segs と同じ**: 細かくしたい人は断面を足す。
 *   ⚠ ねじれが大きいと **Manifold が非平面の四角形の対角線を勝手に張り替える** (実測: 自前で
 *     数えた符号つき体積と Manifold の返す体積が食い違った)。⇒ 粗いままだと分割の選び方
 *     すらこちらの手に無い = cgal は同じ式で別の値を出す。承知の上で受ける。
 */
namespace {

/* 局所座標の符号つき面積 (向きの判定に使う)。 */
static double
mf_loft_sarea(const std::vector<manifold::vec2> &p)
{
	double a = 0;
	int n = (int)p.size();
	for ( int i = 0 ; i < n ; ++i ) {
		const manifold::vec2 &u = p[i], &v = p[(i+1) % n];
		a += u.x * v.y - v.x * u.y;
	}
	return a * 0.5;
}

/* 局所座標の面積重心 (軸を決めるのに使う)。面積 0 なら頂点の平均で代用。 */
static void
mf_loft_centroid(const std::vector<manifold::vec2> &p, double c[2])
{
	double a = 0, cx = 0, cy = 0;
	int n = (int)p.size();
	for ( int i = 0 ; i < n ; ++i ) {
		const manifold::vec2 &u = p[i], &v = p[(i+1) % n];
		double cr = u.x * v.y - v.x * u.y;
		a += cr; cx += (u.x + v.x) * cr; cy += (u.y + v.y) * cr;
	}
	if ( a > 1e-300 || a < -1e-300 ) { c[0] = cx / (3*a); c[1] = cy / (3*a); return; }
	cx = cy = 0;
	for ( int i = 0 ; i < n ; ++i ) { cx += p[i].x; cy += p[i].y; }
	c[0] = cx / n; c[1] = cy / n;
}

static void mf_loft_cross3(const double a[3], const double b[3], double r[3]) {
	r[0] = a[1]*b[2] - a[2]*b[1]; r[1] = a[2]*b[0] - a[0]*b[2]; r[2] = a[0]*b[1] - a[1]*b[0];
}
static double mf_loft_dot3(const double a[3], const double b[3]) {
	return a[0]*b[0] + a[1]*b[1] + a[2]*b[2];
}

/* 1 断面。多角形は **局所座標のまま**・枠は #3526 のもの。 */
struct mfLoftRing {
	std::vector<manifold::vec2>	p;      /* 外周 1 本 (閉・先頭と末尾は繋がっている) */
	double				o[3], u[3], v[3];
	std::vector<double>		cum;    /* 頂点 i までの累積弧長 / 全長 (cum[0]=0) */
	double				start;  /* 始点の正規化パラメータ */

	void world(const manifold::vec2 &q, double w[3]) const {
		for ( int i = 0 ; i < 3 ; ++i ) w[i] = o[i] + q.x*u[i] + q.y*v[i];
	}
	/* 弧長パラメータ表を作る。全長 0 (全頂点が同一点) なら 0 を返す。 */
	double build_param() {
		int n = (int)p.size();
		cum.resize((size_t)n);
		double s = 0;
		for ( int i = 0 ; i < n ; ++i ) {
			cum[(size_t)i] = s;
			const manifold::vec2 &a = p[i], &b = p[(i+1) % n];
			double dx = b.x - a.x, dy = b.y - a.y;
			s += ::sqrt(dx*dx + dy*dy);
		}
		if ( s <= 0 ) return 0;
		for ( int i = 0 ; i < n ; ++i ) cum[(size_t)i] /= s;
		return s;
	}
	/* 正規化パラメータ t (小数部で巻く) の位置を局所座標で返す。
	 *
	 * ★★ 自分の頂点の **eps 以内**なら補間せず **その頂点そのもの**を返す。
	 *   ⚠ これが無いと「断面の頂点は 1 つも動かない」という設計の前提が、丸めの分だけ崩れる。
	 *     実測 (2026-09-13): 同じ式で cache から decode した断面はリングの開始頂点がずれて
	 *     返り、累積和の丸めが変わって弧長パラメータが動いた。結果 *同じ式が下位桁の違う
	 *     体積の 2 通りになった*。
	 *   ★ 補間そのものを禁じるわけではない — 他の断面の頂点が稜の途中に落ちたときは
	 *     きちんと補間する (そこが「和集合で標本化する」意味)。 */
	manifold::vec2 at(double t, double eps) const {
		int n = (int)p.size();
		t -= ::floor(t);
		int i = (int)(std::upper_bound(cum.begin(), cum.end(), t) - cum.begin()) - 1;
		if ( i < 0 ) i = 0;
		double t0 = cum[(size_t)i], t1 = ( i+1 < n ) ? cum[(size_t)(i+1)] : 1.0;
		if ( t - t0 <= eps ) return p[i];
		if ( t1 - t <= eps ) return p[(i+1) % n];
		double f = ( t1 > t0 ) ? (t - t0) / (t1 - t0) : 0.0;
		const manifold::vec2 &a = p[i], &b = p[(i+1) % n];
		return manifold::vec2(a.x + f*(b.x - a.x), a.y + f*(b.y - a.y));
	}
};

/* 側面 + 蓋の頂点総数の上限。★ 標本は **全断面の頂点の和集合** なので頂点数は
 * (断面数) x (頂点数の合計) で伸びる。黙って何 GB も掴まずに名指しで断る。 */
const double MF_LOFT_MAX_VERTS = 4.0e6;

/* ★★ 標本パラメータの重複を潰す許容 (正規化パラメータ = 周長に対する割合)。
 *
 * ⚠⚠ **機械 epsilon 並みの許容では足りない** (2026-09-13 に実測で判明)。manifold の 2D は
 *   **cache から decode するとき CrossSection を作り直す** ので Clipper2 の整数格子に載り、
 *   *同じ式でも経路によって座標が動く* (area で既にそうなっている)。
 *   ⇒ 弧長パラメータもその分ずれ、「同じ位置」の標本が 2 つ残って **薄い三角形の帯**
 *     ができる。実測では同じ式が違う頂点数の 2 通りになり、valid が 0 に落ちた。
 * ⚠ もう 1 つ: リングの **開始頂点は経路で変わる** (その場で計算した断面と cache から
 *   decode した断面で ToPolygons() の先頭がずれていた)。始点合わせ (④) がこれを
 *   吸収するが、吸収した後のパラメータには上の丸め差が残る。
 * ★ この値は「周長に対してこれより近い 2 点は同じ点」という宣言。実用的な多角形の頂点間隔
 *   より桁で小さいので、本当に別の頂点を潰すことはない。 */
const double MF_LOFT_PARAM_EPS = 1e-7;

}  /* anonymous namespace */

sPtr<mfGeom>
mf_loft_ruled_from_args(sArray<sPtr<pigData> > *args, const char **errmsg, char *errbuf, int errbufsz)
{
	int na = ( args != 0 ) ? args->length() : 0;
	if ( na < 2 ) { *errmsg = "needs at least two cross sections"; return sPtr<mfGeom>(); }

	/* ---- ① 断面を取り出す (外周 1 本であることを検査) ---- */
	std::vector<mfLoftRing> R((size_t)na);
	for ( int i = 0 ; i < na ; ++i ) {
		sPtr<mfCross> ci = sPtr<mfCross>::d_cast((*args)[i]);
		if ( ! ci.is_notNull() ) {
			*errmsg = "every section must be a 2D region (mf-cross2d)";
			return sPtr<mfGeom>();
		}
		const manifold::Polygons &ps = ci->polys();   /* ★ #3529 */
		if ( ps.size() == 0 ) {
			if ( errbuf != 0 && errbufsz > 0 ) {
				::snprintf(errbuf, (size_t)errbufsz, "section %d is empty", i);
				*errmsg = errbuf;
			} else
				*errmsg = "a section is empty";
			return sPtr<mfGeom>();
		}
		/* ⚠ occt の「a section is made of N faces」と同じ判断: どの輪をどの輪につなぐかが
		 *   決まらない。穴あき断面もここに来る (外周 + 穴 = 2 本)。 */
		if ( ps.size() > 1 ) {
			if ( errbuf != 0 && errbufsz > 0 ) {
				::snprintf(errbuf, (size_t)errbufsz,
				    "section %d is made of %d outlines; loft needs one closed outline per "
				    "section (a region with a hole cannot be lofted)", i, (int)ps.size());
				*errmsg = errbuf;
			} else
				*errmsg = "a section is made of several outlines";
			return sPtr<mfGeom>();
		}
		if ( ps[0].size() < 3 ) {
			if ( errbuf != 0 && errbufsz > 0 ) {
				::snprintf(errbuf, (size_t)errbufsz,
				    "section %d has only %d points; a closed outline needs at least 3",
				    i, (int)ps[0].size());
				*errmsg = errbuf;
			} else
				*errmsg = "a section has fewer than 3 points";
			return sPtr<mfGeom>();
		}
		R[(size_t)i].p.assign(ps[0].begin(), ps[0].end());
		const double *o = ci->frame_o(), *u = ci->frame_u(), *v = ci->frame_v();
		for ( int k = 0 ; k < 3 ; ++k ) {
			R[(size_t)i].o[k] = o[k]; R[(size_t)i].u[k] = u[k]; R[(size_t)i].v[k] = v[k];
		}
		if ( R[(size_t)i].build_param() <= 0 ) {
			if ( errbuf != 0 && errbufsz > 0 ) {
				::snprintf(errbuf, (size_t)errbufsz,
				    "section %d has zero perimeter (all its points coincide)", i);
				*errmsg = errbuf;
			} else
				*errmsg = "a section has zero perimeter";
			return sPtr<mfGeom>();
		}
	}

	/* ---- ② 軸 a = 末端の重心 − 先頭の重心 ---- */
	double a[3];
	{
		double c0[3], cn[3], t[2];
		mf_loft_centroid(R[0].p, t);              R[0].world(manifold::vec2(t[0], t[1]), c0);
		mf_loft_centroid(R[(size_t)na-1].p, t);   R[(size_t)na-1].world(manifold::vec2(t[0], t[1]), cn);
		for ( int k = 0 ; k < 3 ; ++k ) a[k] = cn[k] - c0[k];
		double la = ::sqrt(mf_loft_dot3(a, a));
		/* ⚠ 先頭と末端の重心が同じ (対称な配置など) なら軸が決まらないので先頭の法線で代用する。
		 *   本当に全断面が重なっているなら、下の体積検査が拾う。 */
		if ( la <= 1e-15 ) {
			mf_loft_cross3(R[0].u, R[0].v, a);
			la = ::sqrt(mf_loft_dot3(a, a));
			if ( la <= 1e-15 ) { *errmsg = "the first section has a degenerate frame"; return sPtr<mfGeom>(); }
		}
		for ( int k = 0 ; k < 3 ; ++k ) a[k] /= la;
	}

	/* ---- ③ 巡回の向きを軸に対して揃える ---- */
	for ( int i = 0 ; i < na ; ++i ) {
		double n[3];
		mf_loft_cross3(R[(size_t)i].u, R[(size_t)i].v, n);
		if ( mf_loft_sarea(R[(size_t)i].p) < 0 )   /* 局所が CW なら向きも裏 */
			for ( int k = 0 ; k < 3 ; ++k ) n[k] = -n[k];
		double d = mf_loft_dot3(n, a);
		/* ⚠ 断面の平面が掃引方向を含む = 側面が断面を突き抜けるので立体にならない。
		 *   ★ 文言は extrude (面が掃引方向を含む) と同じ状況なので揃えてある。 */
		if ( d > -1e-12 && d < 1e-12 ) {
			if ( errbuf != 0 && errbufsz > 0 ) {
				::snprintf(errbuf, (size_t)errbufsz,
				    "section %d lies in a plane that contains the loft direction, so the "
				    "solid has no well-defined inside; move the sections or reorder them", i);
				*errmsg = errbuf;
			} else
				*errmsg = "a section plane contains the loft direction";
			return sPtr<mfGeom>();
		}
		if ( d < 0 ) {
			std::reverse(R[(size_t)i].p.begin(), R[(size_t)i].p.end());
			R[(size_t)i].build_param();   /* 反転したので弧長表を作り直す */
		}
	}

	/* ---- ④ 始点合わせ: 前の断面の始点に世界座標で最も近い頂点 ---- */
	R[0].start = 0.0;
	{
		double prev[3];
		R[0].world(R[0].p[0], prev);
		for ( int i = 1 ; i < na ; ++i ) {
			mfLoftRing &r = R[(size_t)i];
			int best = 0;
			double bd = -1.0;
			for ( int j = 0 ; j < (int)r.p.size() ; ++j ) {
				double w[3];
				r.world(r.p[(size_t)j], w);
				double dx = w[0]-prev[0], dy = w[1]-prev[1], dz = w[2]-prev[2];
				double d = dx*dx + dy*dy + dz*dz;
				if ( bd < 0 || d < bd ) { bd = d; best = j; }
			}
			r.start = r.cum[(size_t)best];
			r.world(r.p[(size_t)best], prev);
		}
	}

	/* ---- ⑤ 標本パラメータ = 全断面の (頂点パラメータ − 始点) の和集合 ---- */
	std::vector<double> P;
	{
		double total = 0;
		for ( int i = 0 ; i < na ; ++i ) total += (double)R[(size_t)i].p.size();
		if ( total * (double)na > MF_LOFT_MAX_VERTS ) {
			if ( errbuf != 0 && errbufsz > 0 ) {
				::snprintf(errbuf, (size_t)errbufsz,
				    "loft would need %.0f vertices (%d sections x %.0f sample points): every "
				    "section is sampled at every other section's vertices; use fewer sections "
				    "or coarser outlines", total * na, na, total);
				*errmsg = errbuf;
			} else
				*errmsg = "loft would need too many vertices";
			return sPtr<mfGeom>();
		}
		P.reserve((size_t)total);
		for ( int i = 0 ; i < na ; ++i ) {
			const mfLoftRing &r = R[(size_t)i];
			for ( size_t j = 0 ; j < r.cum.size() ; ++j ) {
				double t = r.cum[j] - r.start;
				P.push_back(t - ::floor(t));
			}
		}
		std::sort(P.begin(), P.end());
		/* ★ 重なった標本は潰す (同じ断面を 2 枚渡すと全部重なる)。潰さないと長さ 0 の稜や
		 *   薄い帯ができる。許容の根拠は MF_LOFT_PARAM_EPS の但し書き (1e-12 では足りない)。 */
		std::vector<double> q;
		q.reserve(P.size());
		for ( size_t i = 0 ; i < P.size() ; ++i )
			if ( q.size() == 0 || P[i] - q.back() > MF_LOFT_PARAM_EPS ) q.push_back(P[i]);
		/* 0 と 1 の巻き込み (末尾が 1 の側から先頭 0 に回り込んで重なる場合) */
		if ( q.size() > 1 && (1.0 - q.back()) + q[0] <= MF_LOFT_PARAM_EPS ) q.pop_back();
		P.swap(q);
	}
	int M = (int)P.size();
	if ( ::getenv("MF_LOFT_DEBUG") ) {
		::fprintf(stderr, "[loft] na=%d M=%d  rings:", na, M);
		for ( int i = 0 ; i < na ; ++i )
			::fprintf(stderr, " n%d=%d(p0=%.17g,%.17g start=%.17g o=%.3g,%.3g,%.3g)", i,
			    (int)R[(size_t)i].p.size(), R[(size_t)i].p[0].x, R[(size_t)i].p[0].y,
			    R[(size_t)i].start, R[(size_t)i].o[0], R[(size_t)i].o[1], R[(size_t)i].o[2]);
		::fprintf(stderr, "\n");
	}
	if ( M < 3 ) { *errmsg = "the sections collapse to fewer than 3 distinct points"; return sPtr<mfGeom>(); }

	/* ---- ⑥ 頂点 (断面 i の標本 k = i*M + k) ---- */
	std::vector<double> vp;
	vp.reserve((size_t)na * (size_t)M * 3);
	std::vector<std::vector<manifold::vec2> > loc((size_t)na);
	for ( int i = 0 ; i < na ; ++i ) {
		const mfLoftRing &r = R[(size_t)i];
		loc[(size_t)i].resize((size_t)M);
		for ( int k = 0 ; k < M ; ++k ) {
			manifold::vec2 q = r.at(P[(size_t)k] + r.start, MF_LOFT_PARAM_EPS);
			loc[(size_t)i][(size_t)k] = q;
			double w[3];
			r.world(q, w);
			vp.push_back(w[0]); vp.push_back(w[1]); vp.push_back(w[2]);
		}
	}

	/* ---- ⑥-b ⚠ 隣り合う断面が **重なっていない** ことを先に見る ----
	 * ★ 後段 (⑩) の「閉じた曲面にならない」でも落ちるが、それだと文言が原因を名指しできない
	 *   (「順番を確かめろ」と言われても重なっているとは分からない)。⇒ ここで名指しする。
	 * ★ 許容は **全点の bbox の対角** に対する相対 (絶対値だと寸法の単位で意味が変わる)。 */
	{
		double mn[3], mx[3];
		for ( int k = 0 ; k < 3 ; ++k ) { mn[k] = vp[k]; mx[k] = vp[k]; }
		for ( size_t i = 0 ; i + 2 < vp.size() ; i += 3 )
			for ( int k = 0 ; k < 3 ; ++k ) {
				if ( vp[i+k] < mn[k] ) mn[k] = vp[i+k];
				if ( vp[i+k] > mx[k] ) mx[k] = vp[i+k];
			}
		double dx = mx[0]-mn[0], dy = mx[1]-mn[1], dz = mx[2]-mn[2];
		double diag = ::sqrt(dx*dx + dy*dy + dz*dz);
		double eps2 = ( diag > 0 ) ? (1e-9 * diag) * (1e-9 * diag) : 0.0;
		for ( int i = 0 ; i + 1 < na ; ++i ) {
			double worst = 0;
			for ( int k = 0 ; k < M ; ++k ) {
				const double *p0 = &vp[(size_t)3*((size_t)i*M + k)];
				const double *p1 = &vp[(size_t)3*((size_t)(i+1)*M + k)];
				double a0 = p1[0]-p0[0], a1 = p1[1]-p0[1], a2 = p1[2]-p0[2];
				double d = a0*a0 + a1*a1 + a2*a2;
				if ( d > worst ) worst = d;
			}
			if ( worst <= eps2 ) {
				if ( errbuf != 0 && errbufsz > 0 ) {
					::snprintf(errbuf, (size_t)errbufsz,
					    "sections %d and %d are at the same place, so the piece between them "
					    "has no thickness; move one of them or drop the duplicate", i, i+1);
					*errmsg = errbuf;
				} else
					*errmsg = "two consecutive sections are at the same place";
				return sPtr<mfGeom>();
			}
		}
	}

	/* ---- ⑦ 側面: 隣り合う断面の間を四角形 1 枚 = 三角形 2 枚で埋める ----
	 * ★ 巡回が軸まわりで揃っているので (A,A2,B2) / (A,B2,B) が外向きになる
	 *   (単位立方体で検算済み)。⚠ 非平面の四角形ではこの対角線の選び方が値を決める
	 *   = ねじれた loft が occt と一致しない理由 (上の但し書き)。 */
	std::vector<uint64_t> tv;
	tv.reserve((size_t)(na-1) * (size_t)M * 6 + (size_t)M * 6);
	for ( int i = 0 ; i + 1 < na ; ++i ) for ( int k = 0 ; k < M ; ++k ) {
		uint64_t A  = (uint64_t)i*M + k,       A2 = (uint64_t)i*M + (k+1) % M;
		uint64_t Bv = (uint64_t)(i+1)*M + k,   B2 = (uint64_t)(i+1)*M + (k+1) % M;
		tv.push_back(A); tv.push_back(A2); tv.push_back(B2);
		tv.push_back(A); tv.push_back(B2); tv.push_back(Bv);
	}

	/* ---- ⑧ 蓋: 両端の断面を局所座標で三角形分割し、**世界法線で向きを決める** ----
	 * ★ TriangulateIdx の向きの規約に頼らず、出てきた三角形ごとに外向き (先頭は −a・
	 *   末端は +a) と比べて必要なら入れ替える。⇒ 規約が変わっても壊れない。 */
	for ( int side = 0 ; side < 2 ; ++side ) {
		int i = side ? (na - 1) : 0;
		double want[3];
		for ( int k = 0 ; k < 3 ; ++k ) want[k] = side ? a[k] : -a[k];
		manifold::SimplePolygonIdx sp;
		sp.reserve((size_t)M);
		int ccw = ( mf_loft_sarea(loc[(size_t)i]) > 0 ) ? 1 : 0;
		for ( int k = 0 ; k < M ; ++k ) {
			int kk = ccw ? k : (M - 1 - k);
			manifold::PolyVert pv;
			pv.pos = loc[(size_t)i][(size_t)kk];
			pv.idx = i*M + kk;
			sp.push_back(pv);
		}
		manifold::PolygonsIdx pl;
		pl.push_back(sp);
		std::vector<manifold::ivec3> tris;
		/* ⚠ 上流は退化した多角形で throw する (geometryErr)。落とさずに名指しで返す。 */
		try {
			tris = manifold::TriangulateIdx(pl);
		} catch ( const std::exception &e ) {
			if ( errbuf != 0 && errbufsz > 0 ) {
				::snprintf(errbuf, (size_t)errbufsz,
				    "the cap of section %d could not be triangulated (%s); check that the "
				    "outline does not cross itself", i, e.what());
				*errmsg = errbuf;
			} else
				*errmsg = "a cap could not be triangulated";
			return sPtr<mfGeom>();
		}
		if ( tris.size() == 0 ) {
			if ( errbuf != 0 && errbufsz > 0 ) {
				::snprintf(errbuf, (size_t)errbufsz, "the cap of section %d is degenerate", i);
				*errmsg = errbuf;
			} else
				*errmsg = "a cap is degenerate";
			return sPtr<mfGeom>();
		}
		for ( size_t t = 0 ; t < tris.size() ; ++t ) {
			int i0 = tris[t][0], i1 = tris[t][1], i2 = tris[t][2];
			const double *p0 = &vp[(size_t)3*i0], *p1 = &vp[(size_t)3*i1], *p2 = &vp[(size_t)3*i2];
			double e1[3] = { p1[0]-p0[0], p1[1]-p0[1], p1[2]-p0[2] };
			double e2[3] = { p2[0]-p0[0], p2[1]-p0[1], p2[2]-p0[2] };
			double n[3];
			mf_loft_cross3(e1, e2, n);
			if ( mf_loft_dot3(n, want) < 0 ) { int s = i1; i1 = i2; i2 = s; }
			tv.push_back((uint64_t)i0); tv.push_back((uint64_t)i1); tv.push_back((uint64_t)i2);
		}
	}

	/* ---- ⑨ 全体が裏返っていたら戻す (符号つき体積で見る) ----
	 * ★ 三角形の向きは揃っているので、符号が負 = 全体が内向き。全部入れ替えれば直る。 */
	{
		double v6 = 0;
		for ( size_t t = 0 ; t + 2 < tv.size() ; t += 3 ) {
			const double *p0 = &vp[3*tv[t]], *p1 = &vp[3*tv[t+1]], *p2 = &vp[3*tv[t+2]];
			double c[3];
			mf_loft_cross3(p1, p2, c);
			v6 += p0[0]*c[0] + p0[1]*c[1] + p0[2]*c[2];
		}
		if ( v6 < 0 )
			for ( size_t t = 0 ; t + 2 < tv.size() ; t += 3 ) {
				uint64_t s = tv[t+1]; tv[t+1] = tv[t+2]; tv[t+2] = s;
			}
	}

	MeshGL64 gl;
	gl.numProp = 3;
	gl.vertProperties = vp;
	gl.triVerts = tv;
	sPtr<mfMesh> out = thNEW(mfMesh,(Manifold(gl)));

	/* ---- ⑩ 出口の検査 (#3518 の 5 と同じ網) ----
	 * ⚠ **黙って 0 を返す口**を潰す: 断面が同じ位置に重なっていると Manifold は
	 *   Status()==NoError のまま体積 0 を返す (実測)。
	 * ★ 閾値は bbox 体積に対する **相対** (絶対値だと寸法の単位で意味が変わる)。 */
	/* ★ 見るのは「閉じた向きつき曲面として成立したか」だけ (Manifold の型不変条件)。
	 * ⚠⚠ **@op_valid()@ を使ってはいけない**。あれは #3487 の共通定義なので **自己交差も** 見る
	 *   (③)。ねじれた loft は自己交差しうるが、ひさ判断は *そのまま受ける* なので、ここで断ると
	 *   「ねじれを受ける」と矛盾する。⇒ 自己交差の報告は @valid(...)@ の仕事として残し、
	 *   op は refuse しない (自己交差する @tube@ を #3445 で受けているのと同じ立場)。
	 *   ★ 実際これで断ってしまい、断面 3 枚 (r=1→2→1) が通らなかった。 */
	if ( out->manifold().Status() != manifold::Manifold::Error::NoError || out->manifold().IsEmpty() ) {
		*errmsg = "the sections do not bound a closed surface; check that they are ordered along "
		          "the path and that each outline is simple";
		return sPtr<mfGeom>();
	}
	{
		double vol = out->op_volume();
		double mn[3], mx[3], scale = 1.0;
		if ( out->op_bbox(mn, mx) == 3 ) {
			double bv = (mx[0]-mn[0]) * (mx[1]-mn[1]) * (mx[2]-mn[2]);
			if ( bv > 0 ) scale = bv;
		}
		if ( ( (vol < 0) ? -vol : vol ) <= 1e-9 * scale ) {
			*errmsg = "the sections do not bound a solid (the signed volume cancels to 0); "
			          "check that they are not coincident and that they are ordered along the path";
			return sPtr<mfGeom>();
		}
	}
	return sPtr<mfGeom>::d_cast(out);
}


/* ---- ★★ 空間に置いた 2D を **world Y 軸**まわりに回す (#3526) --------------------
 *
 * ★ 軸を world Y に固定するのは ひさ判断 (extrude の「world +Z のまま」と同じ論法)。
 *   ⇒ occt (ocaRevolve) と同じ規約になり、3 カーネルで同じことを答える。
 *
 * ★★ 「局所で回してから枠を当てる」では **書けない** — それは軸を枠の V に固定することに
 *   なり、world Y とは違う立体になる。⇒ **掃引そのものを世界座標でやる**:
 *     ① 輪 (外周 + 穴) を枠で世界座標へ置く
 *     ② world Y まわりに nseg 分割で回した **列**を作る
 *     ③ 隣り合う列の間を四角形で張る (対応は添字 — 回転は輪の頂点の順序を保つので
 *        loft の弧長合わせのような machinery は要らない)
 *     ④ 全周なら末尾が先頭に巻き付くので **蓋は要らない**。部分回転なら両端に蓋を張る
 *
 * ⚠ **断面が軸をまたぐと立体にならない** (掃引が自分自身を通り抜ける)。占有領域は体積を持つのに
 *   境界の符号つき体積は打ち消すので、黙って 0 が返る口になる ⇒ 先に検査して名指しで断る。
 *   ★ 検査は 2 通りに分かれる:
 *     ・断面の平面が **軸を含む**  … 平面の中で軸の直線をまたがないこと (古典的な条件)
 *     ・含まない (1 点で交わる)    … その交点が領域の中に無いこと
 * ⚠ 枠が既定のときはこの関数を通さない (従来の Manifold::Revolve のまま) — *既存の値を
 *   1 ビットも動かさない*ため。
 */
sPtr<mfMesh>
mf_revolve_placed(sPtr<mfCross> in, double angle, int nseg,
                  const char **errmsg, char *errbuf, int errbufsz)
{
	if ( ! in.is_notNull() ) { *errmsg = "needs a 2D region"; return sPtr<mfMesh>(); }
	manifold::Polygons polys = in->polys();   /* ★ #3529 */
	if ( polys.size() == 0 ) { *errmsg = "the 2D region is empty"; return sPtr<mfMesh>(); }

	const double *U = in->frame_u(), *V = in->frame_v(), *O = in->frame_o();
	double nrm[3];
	mf_loft_cross3(U, V, nrm);   /* 枠は正規直交なので単位法線 */

	/* ---- ① ⚠ 軸をまたいでいないか (黙って 0 を返す口を塞ぐ) ----
	 * world Y 軸 = 点 (0,0,0) + 方向 ŷ。 */
	{
		const double yh[3] = { 0, 1, 0 };
		double ndy = mf_loft_dot3(nrm, yh);
		if ( ndy > -1e-12 && ndy < 1e-12 ) {
			/* 平面が軸を含む (または軸と平行)。⇒ 平面の中で「軸までの符号つき距離」を見る。
			 * ★ 平面内で軸に垂直な向き = nrm x ŷ。原点から枠原点へのずれも足して測る。 */
			double perp[3];
			mf_loft_cross3(nrm, yh, perp);
			double lp = ::sqrt(mf_loft_dot3(perp, perp));
			if ( lp <= 1e-12 ) { *errmsg = "the 2D region lies in a plane perpendicular to the "
			                               "axis of revolution, so revolving it sweeps no solid";
			                     return sPtr<mfMesh>(); }
			for ( int k = 0 ; k < 3 ; ++k ) perp[k] /= lp;
			int neg = 0, pos = 0;
			for ( size_t r = 0 ; r < polys.size() ; ++r )
				for ( size_t i = 0 ; i < polys[r].size() ; ++i ) {
					double w[3];
					in->to_world(polys[r][i].x, polys[r][i].y, w);
					double d = mf_loft_dot3(w, perp);
					if ( d < -1e-12 ) ++neg;
					else if ( d > 1e-12 ) ++pos;
				}
			if ( neg > 0 && pos > 0 ) {
				*errmsg = "the 2D region crosses the axis of revolution (the world Y axis), so "
				          "the swept solid passes through itself; move the region to one side";
				return sPtr<mfMesh>();
			}
		} else {
			/* 平面は軸と 1 点で交わる。⇒ その交点が領域の中に無いこと。
			 * 交点: O + s*nrm を満たす t で (t*ŷ - O)・nrm = 0 ⇒ t = (O・nrm)/(ŷ・nrm)。 */
			double t = mf_loft_dot3(O, nrm) / ndy;
			double hit[3] = { 0, t, 0 };
			/* 局所座標へ戻す (枠は正規直交なので内積で射影できる) */
			double d0[3] = { hit[0]-O[0], hit[1]-O[1], hit[2]-O[2] };
			double hx = mf_loft_dot3(d0, U), hy = mf_loft_dot3(d0, V);
			/* 偶奇則の点内包判定 (全リング合算 = 穴も正しく扱える) */
			int inside = 0;
			for ( size_t r = 0 ; r < polys.size() ; ++r ) {
				const manifold::SimplePolygon &ring = polys[r];
				int n = (int)ring.size();
				for ( int i = 0, j = n-1 ; i < n ; j = i++ ) {
					double xi = ring[(size_t)i].x, yi = ring[(size_t)i].y;
					double xj = ring[(size_t)j].x, yj = ring[(size_t)j].y;
					if ( ((yi > hy) != (yj > hy))
					  && (hx < (xj - xi) * (hy - yi) / (yj - yi) + xi) ) inside = !inside;
				}
			}
			if ( inside ) {
				*errmsg = "the axis of revolution (the world Y axis) passes through the 2D "
				          "region, so the swept solid passes through itself; move the region "
				          "off the axis";
				return sPtr<mfMesh>();
			}
		}
	}

	/* ---- ② 回転の列 ---- */
	if ( nseg < 3 ) nseg = 3;
	if ( angle > 360.0 ) angle = 360.0;
	int full = ( angle >= 360.0 ) ? 1 : 0;
	/* ★ 分割は **全周 nseg** を基準に刻む (部分回転でも刻み幅が同じになる)。 */
	int steps = full ? nseg : (int)(nseg * angle / 360.0 + 0.5);
	if ( steps < 1 ) steps = 1;
	int nlev = full ? steps : (steps + 1);   /* 全周は末尾が先頭に巻き付く */
	double dth = ( angle * 3.14159265358979323846 / 180.0 ) / (double)steps;

	/* リングごとの頂点の並びを平坦化 (リング r の点 i は base[r]+i) */
	std::vector<int> base((size_t)polys.size() + 1, 0);
	for ( size_t r = 0 ; r < polys.size() ; ++r )
		base[r+1] = base[r] + (int)polys[r].size();
	int npt = base[polys.size()];
	if ( npt < 3 ) { *errmsg = "the 2D region has too few points"; return sPtr<mfMesh>(); }

	std::vector<double> vp;
	vp.reserve((size_t)nlev * (size_t)npt * 3);
	for ( int L = 0 ; L < nlev ; ++L ) {
		double th = dth * (double)L, c = ::cos(th), s = ::sin(th);
		for ( size_t r = 0 ; r < polys.size() ; ++r )
			for ( size_t i = 0 ; i < polys[r].size() ; ++i ) {
				double w[3];
				in->to_world(polys[r][i].x, polys[r][i].y, w);
				/* Y 軸まわりの回転: (x,z) を回す */
				vp.push_back( c*w[0] + s*w[2] );
				vp.push_back( w[1] );
				vp.push_back( -s*w[0] + c*w[2] );
			}
	}

	/* ---- ③ 側面 ---- */
	std::vector<uint64_t> tv;
	for ( int L = 0 ; L + 1 < nlev + full ; ++L ) {
		int L2 = ( L + 1 ) % nlev;   /* 全周なら巻き付く */
		for ( size_t r = 0 ; r < polys.size() ; ++r ) {
			int n = (int)polys[r].size();
			for ( int i = 0 ; i < n ; ++i ) {
				int i2 = (i + 1) % n;
				uint64_t A  = (uint64_t)L *npt + base[r] + i,  A2 = (uint64_t)L *npt + base[r] + i2;
				uint64_t Bv = (uint64_t)L2*npt + base[r] + i,  B2 = (uint64_t)L2*npt + base[r] + i2;
				tv.push_back(A); tv.push_back(A2); tv.push_back(B2);
				tv.push_back(A); tv.push_back(B2); tv.push_back(Bv);
			}
		}
	}

	/* ---- ④ 蓋 (部分回転だけ)。局所座標で三角形分割して両端に張る ---- */
	if ( ! full ) {
		manifold::PolygonsIdx pl;
		for ( size_t r = 0 ; r < polys.size() ; ++r ) {
			manifold::SimplePolygonIdx sp;
			for ( size_t i = 0 ; i < polys[r].size() ; ++i ) {
				manifold::PolyVert pv;
				pv.pos = polys[r][i];
				pv.idx = base[r] + (int)i;
				sp.push_back(pv);
			}
			pl.push_back(sp);
		}
		std::vector<manifold::ivec3> tris;
		try {
			tris = manifold::TriangulateIdx(pl);
		} catch ( const std::exception &e ) {
			if ( errbuf != 0 && errbufsz > 0 ) {
				::snprintf(errbuf, (size_t)errbufsz,
				    "the end cap could not be triangulated (%s); check that the outline does "
				    "not cross itself", e.what());
				*errmsg = errbuf;
			} else
				*errmsg = "the end cap could not be triangulated";
			return sPtr<mfMesh>();
		}
		int last = nlev - 1;
		for ( size_t t = 0 ; t < tris.size() ; ++t ) {
			/* 先頭の蓋と末尾の蓋。向きは下の符号つき体積で最後に揃える。 */
			tv.push_back((uint64_t)tris[t][0]);
			tv.push_back((uint64_t)tris[t][2]);
			tv.push_back((uint64_t)tris[t][1]);
			tv.push_back((uint64_t)last*npt + tris[t][0]);
			tv.push_back((uint64_t)last*npt + tris[t][1]);
			tv.push_back((uint64_t)last*npt + tris[t][2]);
		}
	}

	/* ---- ⑤ 全体が裏返っていたら戻す ---- */
	{
		double v6 = 0;
		for ( size_t t = 0 ; t + 2 < tv.size() ; t += 3 ) {
			const double *p0 = &vp[3*tv[t]], *p1 = &vp[3*tv[t+1]], *p2 = &vp[3*tv[t+2]];
			double c[3];
			mf_loft_cross3(p1, p2, c);
			v6 += p0[0]*c[0] + p0[1]*c[1] + p0[2]*c[2];
		}
		if ( v6 < 0 )
			for ( size_t t = 0 ; t + 2 < tv.size() ; t += 3 ) {
				uint64_t sw = tv[t+1]; tv[t+1] = tv[t+2]; tv[t+2] = sw;
			}
	}

	MeshGL64 gl;
	gl.numProp = 3;
	gl.vertProperties = vp;
	gl.triVerts = tv;
	sPtr<mfMesh> out = thNEW(mfMesh,(Manifold(gl)));
	if ( out->manifold().Status() != manifold::Manifold::Error::NoError || out->manifold().IsEmpty() ) {
		*errmsg = "the swept surface does not close (the region may touch the axis or cross "
		          "itself); move the region off the axis";
		return sPtr<mfMesh>();
	}
	{
		double vol = out->op_volume();
		double mn[3], mx[3], scale = 1.0;
		if ( out->op_bbox(mn, mx) == 3 ) {
			double bv = (mx[0]-mn[0]) * (mx[1]-mn[1]) * (mx[2]-mn[2]);
			if ( bv > 0 ) scale = bv;
		}
		if ( ( (vol < 0) ? -vol : vol ) <= 1e-9 * scale ) {
			*errmsg = "the swept solid has no volume (its signed volume cancels to 0)";
			return sPtr<mfMesh>();
		}
	}
	return out;
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
/* ---- ★★ #3498: 遅延 CSG 木の評価を **compute() の中へ引き出す** ----------------
 * 但し書きは全部 mfMesh.h の mfGeom::force_eval に書いた。ここは実装だけ。
 *
 * ★ 木の評価結果は @c CsgOpNode の @c cache_ (mutable メンバ) に入るので、ここで
 *   *コピー越しに* 評価しても本体 m_ が後で使い回せる。⇒ encode() の GetMeshGL64 は
 *   評価済みの葉を読むだけになり、**総仕事量は増えない**。
 * ⚠ 逆に言えば「評価がどの段で計上されるか」だけが変わる。測定への影響はそこ。 */
/* ⚠ manifold の @c ToString(Error) は **MANIFOLD_DEBUG でしか定義されない** (manifold.h)。
 *   既定ビルドには無いので、名前が要るならこちらで持つ。 */
static const char *
mf_error_name(manifold::Manifold::Error e)
{
	typedef manifold::Manifold M;
	switch ( e ) {
	case M::Error::NoError:                      return "no error";
	case M::Error::NonFiniteVertex:              return "non-finite vertex";
	case M::Error::NotManifold:                  return "not manifold";
	case M::Error::VertexOutOfBounds:            return "vertex out of bounds";
	case M::Error::PropertiesWrongLength:        return "properties wrong length";
	case M::Error::MissingPositionProperties:    return "missing position properties";
	case M::Error::MergeVectorsDifferentLengths: return "merge vectors different lengths";
	case M::Error::MergeIndexOutOfBounds:        return "merge index out of bounds";
	case M::Error::TransformWrongLength:         return "transform wrong length";
	case M::Error::RunIndexWrongLength:          return "run index wrong length";
	case M::Error::FaceIDWrongLength:            return "face ID wrong length";
	case M::Error::InvalidConstruction:          return "invalid construction";
	case M::Error::ResultTooLarge:               return "result too large";
	case M::Error::InvalidTangents:              return "invalid tangents";
	case M::Error::Cancelled:                    return "cancelled";
	}
	return "unknown error";
}

int
mfMesh::force_eval(const pigBreak *brk, const char **why)
{
	manifold::ExecutionContext ctx;
	/* ★ 押し出し口。dtor が返るまでの間だけ、別スレッドの cancel() がここへ届く。
	 *   登録の時点で既に旗が立っていれば pigBreakHook がその場で撃つので、
	 *   「destroy が先・計算が後」の順でも取りこぼさない。 */
	pigBreakHook hook(brk, [&ctx]{ ctx.Cancel(); });

	const Manifold::Error st = m_.WithContext(ctx).Status();
	if ( st == Manifold::Error::Cancelled ) {
		if ( why ) *why = "cancelled";
		return 0;
	}
	if ( st != Manifold::Error::NoError ) {
		if ( why ) *why = mf_error_name(st);
		return 0;
	}
	return 1;
}

/* ★ #3498: 2D は ExecutionContext を取る口が無い (CrossSection の API に無い)。
 *   入口で旗を見るだけ = 中断の粒度は op 単位。 */
int
mfCross::force_eval(const pigBreak *brk, const char **why)
{
	if ( brk != 0 && brk->cancelled() ) {
		if ( why ) *why = "cancelled";
		return 0;
	}
	return 1;
}

double mfMesh::op_volume() { return m_.Volume(); }
double mfMesh::op_area()   { return m_.SurfaceArea(); }

/* ---- 計測: 頂点数 / 面数 (#3443) ----
 * ★ planner が cache の先頭バイトを読んで表示していたのを op へ移した (planner はカーネル中立へ)。
 * ★ @NumVert()@ は Manifold の内部頂点数。cache に書くのは @GetMeshGL64()@ の頂点なので、
 *   同じ値になるように **抽出後の数** を返す (lazy CSG はここで評価される = volume/area と同じコスト)。 */
int
mfMesh::op_nverts()
{
	manifold::MeshGL64 g = m_.GetMeshGL64();
	return (int)(g.vertProperties.size() / g.numProp);
}

int
mfMesh::op_nfaces()
{
	manifold::MeshGL64 g = m_.GetMeshGL64();
	return (int)(g.triVerts.size() / 3);
}

int
mfMesh::op_valid()
{
	/* ★ #3487: 共通定義は ① 空でない ∧ ② 閉じている ∧ ③ 自己交差が無い
	 *   (定義の全文と経緯は src/h/common/meshprops.h の冒頭)。
	 *   Status()==NoError ∧ !IsEmpty() が見ているのは ①② まで — **Manifold の型不変条件は
	 *   自己交差を含まない**。実測 (2026-09-05): #3445 の自己交差 tube (体積 52.02 =
	 *   交差部を二重に数えた値) を cgal / nef / geogram / cherchi は 0 と答えるのに
	 *   manifold だけが 1 と答えていた。③ を足して揃える。
	 *   ⚠ ③ の実装は common/meshprops.h (double の述語)。Manifold の座標はもともと
	 *     double なので、厳密述語を持ち込んでも意味は増えない。
	 *
	 * ★★ #3537 (2026-09-15): ② も **共通定義の is_closed で見る**ようにした。
	 *   「Status()==NoError が ② を見ている」は *代理* で、実体とずれていた —
	 *   2 球の xor で得た「交線の円で接する三日月 2 つ」は、溶接すると 4 回使われる辺が
	 *   108 本ある非多様体なのに、Manifold は継ぎ目の頂点を別番号で持っているので
	 *   自分の不変条件では NoError と答える。⇒ ①②③ を全部こちらの定義で答える。
	 *   ⚠ この歯抜けは **③ の誤検出に隠れていた** (誤検出を止めるまで答えだけ合っていた)。 */
	if ( m_.Status() != Manifold::Error::NoError || m_.IsEmpty() ) return 0;
	MeshGL64 g = m_.GetMeshGL64();
	const size_t np = (size_t)g.numProp;
	const size_t nv = ( np > 0 ) ? g.vertProperties.size() / np : 0;
	std::vector<double>   c(nv * 3);
	for ( size_t i = 0 ; i < nv ; ++i )
		for ( int k = 0 ; k < 3 ; ++k )
			c[3*i + k] = g.vertProperties[i*np + k];
	std::vector<uint32_t> t(g.triVerts.begin(), g.triVerts.end());
	srava_mesh::TriView v(c.empty() ? 0 : &c[0], (int)nv,
	                      t.empty() ? 0 : &t[0], (int)(t.size()/3));
	if ( ! srava_mesh::is_closed(v) )    return 0;   /* ② 閉じている (溶接後の辺で数える) */
	return srava_mesh::self_intersects(v) ? 0 : 1;  /* ③ 自己交差が無い */
}

/* ---- 位相: シェル数 / 塊数 / 総種数 (#3514) ----
 * ★ 定義と導き方 (塊 = 符号つき体積が正のシェル) は common/meshprops.h の topology() に一本で
 *   書いてある。ここは GetMeshGL64 から素の配列へ写して渡すだけ。
 * ⚠ 上流の Genus() / Decompose() を使わない理由は mfMesh.h の宣言に付けてある。 */
int
mfMesh::op_topology(int *nshells, int *nparts, int *genus)
{
	MeshGL64 g = m_.GetMeshGL64();
	const size_t np = (size_t)g.numProp;
	const size_t nv = ( np > 0 ) ? g.vertProperties.size() / np : 0;
	std::vector<double>   c(nv * 3);
	for ( size_t i = 0 ; i < nv ; ++i )
		for ( int k = 0 ; k < 3 ; ++k )
			c[3*i + k] = g.vertProperties[i*np + k];
	std::vector<uint32_t> t(g.triVerts.begin(), g.triVerts.end());
	srava_mesh::TriView v(c.empty() ? 0 : &c[0], (int)nv,
	                      t.empty() ? 0 : &t[0], (int)(t.size()/3));
	srava_mesh::Topology p = srava_mesh::topology(v);
	if ( nshells ) *nshells = p.nshells;
	if ( nparts  ) *nparts  = p.nparts;
	if ( genus   ) *genus   = p.genus;
	return p.closed;
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
	 *   (cast("mf-mesh3d", exactMesh) の損失変換。cg agent 非依存=文字列パースのみ)。
	 *   ⚠ 旧 cast("manifold", …) のカーネル名指しは rev4 で廃止 (cast は **目標型名** を取る)。 */
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
	/* ★ #3433: nef の "NEF3" も受理する。payload 先頭 1 バイトが形式で、境界形式 (=1) なら
	 *   cg の "MESH" と同一フレーミングなので **CGAL 無しで**読める。SNC (=0) は読めない。 */
	/* ★★ #3499: 受理するのは **"NEFB" (nef_hybrid) だけ**。#3478 は nef_snc の "NEF3" も
	 *   受けていたが、それは nef_snc が SNC の後ろに厳密境界を付録として書いていたから
	 *   成立していた。その付録は encode ごとの to_mesh() が高くつくので撤回し、nef_snc は
	 *   「常に SNC だけ」へ戻した。nf-mesh3d → mf-mesh3d の変換は **橋モジュール nef_mf.so**
	 *   が持つ。ここで名乗ったままにすると、橋があるのに mf 側の reader が先に掴んで
	 *   「読めない」で落ちる。 */
	if ( len == 4 && ::memcmp(meta, "NEFB", 4) == 0 ) {
		sPtr<mfMesh> m = thNEW(mfMesh,(Manifold()));
		m->set_nef3_input();
		return m;
	}
	return sPtr<mfGeom>();
}
