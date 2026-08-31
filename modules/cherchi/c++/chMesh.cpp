/*
 * chMesh — Cherchi (IRMB) 幾何の実装 (#3438 P6)。設計の背景はヘッダ冒頭を参照。
 *
 * ★ IRMB は **ヘッダだけのライブラリ** (各 .h が末尾で自分の .cpp を include する形) なので、
 *   この TU だけが booleans.h を読む。**コンパイルが重い**うえ、
 *   `-frounding-math` (IEEE 754 準拠が述語の前提) が要るので、巻き込む範囲は最小にしてある。
 */
#include	"ch/c++/chMesh.h"
#include	"ts2/c++/stdString.h"
#include	"common/exact_wire.h"   /* cgal 厳密 wire の有理数文字列パーサ (manifold と共通) */

#include	<stdio.h>
#include	<string.h>
#include	<string>
#include	<stdexcept>
#include	<unordered_map>

/* ★ IRMB 本体。cinolib / Indirect_Predicates / abseil を芋づるで引くのでここだけに閉じる。 */
#include	"booleans.h"

/* ---- 生成 ---------------------------------------------------------------- */
chMesh::chMesh(sPtr<pigInfo> i)
	: chGeom(i)
{
}

int
chMesh::add_vertex(double x, double y, double z)
{
	int v = (int)(coords_.size() / 3);
	coords_.push_back(x); coords_.push_back(y); coords_.push_back(z);
	return v;
}

void
chMesh::add_triangle(int a, int b, int c)
{
	tris_.push_back((uint32_t)a); tris_.push_back((uint32_t)b); tris_.push_back((uint32_t)c);
}

double
chMesh::volume() const
{
	/* 閉曲面が囲む体積 (発散定理)。外向き CCW で正。
	 * ★ cgal は厳密有理数を積んで最後に 1 回だけ丸めるので、下位桁は一致しない
	 *   (kernel_agree の許容誤差の根拠。geogram と同じ事情)。 */
	double s = 0.0;
	for ( size_t i = 0 ; i + 2 < tris_.size() ; i += 3 ) {
		const double *a = &coords_[3 * (size_t)tris_[i]];
		const double *b = &coords_[3 * (size_t)tris_[i+1]];
		const double *c = &coords_[3 * (size_t)tris_[i+2]];
		s += a[0] * (b[1]*c[2] - b[2]*c[1])
		   - a[1] * (b[0]*c[2] - b[2]*c[0])
		   + a[2] * (b[0]*c[1] - b[1]*c[0]);
	}
	return s / 6.0;
}

sPtr<stdString>
chMesh::get_str()
{
	char buf[128];
	::snprintf(buf, sizeof buf, "<ch-mesh3d verts=%d faces=%d>", nverts(), nfaces());
	return thNEW(stdString,(buf));
}

/* ---- 例外を srava のエラーへ落とす ----------------------------------------
 * ★ IRMB は入力が退化していると std::runtime_error / assert で出てくる。捕まえないと
 *   agent が terminate() → SIGABRT で死に、planner からは理由が読めない
 *   (cgal / nef / geogram / occt と同じ作法)。
 * ⚠ 捕まえるのは例外だけ。SIGSEGV 等はここでは受けない (受けてはいけない)。
 * ⚠ 理由は **呼び手のバッファへ書く** (モジュール大域の static を置かない・ひさ指示 2026-08-26)。 */
static void
ch_note_error(char *err, int errsz, const char *what)
{
	if ( err == 0 || errsz <= 0 )
		return;
	const char *w = ( what != 0 && *what != '\0' ) ? what : "cherchi fatal error";
	::snprintf(err, (size_t)errsz, "%s", w);
	for ( char *p = err ; *p ; ++p ) if ( *p == '\n' || *p == '\r' ) *p = ' ';
}

template <class F>
static int
ch_guard(F f, char *err, int errsz)
{
	try {
		f();
		return 1;
	} catch ( const std::exception &e ) {
		ch_note_error(err, errsz, e.what());
		return 0;
	} catch ( ... ) {
		ch_note_error(err, errsz, 0);
		return 0;
	}
}

/* ---- 結果の健全性検査 (★ 黙って誤らせないための最後の砦) -------------------
 * ⚠⚠ IRMB は **オペランドの配置が退化していると壊れる**。壊れ方は 2 通りあり、どちらも
 *   Release ビルド (NDEBUG) では上流の assert が効かないので静かに進む:
 *     ① 誤った値を返す (境界は閉じたまま)
 *          例: 面でちょうど接する 2 箱の union。共有壁が両側から残って体積が過大になるが、
 *          辺の対応は取れているので**下の検査には掛からない** (→ 既知の限界として文書化)。
 *     ② arrangement 自体が壊れる
 *          例: 同じ形を軸に沿って並べて多重に重ねた union。デバッグビルドでは
 *          triangulation.cpp:700 の assert が落ち、Release では **実行のたびに違う値**が返る。
 *          このとき結果には必ず **境界辺** (逆向きの相手がいない有向辺) が残る。
 *   ⇒ ② を **ここで捕まえてエラーにする**。非決定な値が返るのが最悪なので、そこは必ず塞ぐ。
 *
 * 検査: **逆向きの相手がいない有向辺 (= 境界辺) が 1 本でもあれば不合格**。
 *   ⚠ 「有向辺の重複 (非多様体)」は**不合格にしない** — 正しい結果でも出るため。
 *     球の中心が箱の角にある配置などでは、重複辺を持ちながら体積は正しく出る
 *     (coincident な三角形が打ち消し合うため)。ここで弾くと**正しい結果まで捨てる**。
 *   空 (三角形 0) は真 — intersection が空になるのは正しい結果なので通す。
 *   コストは O(F) で、arrangement 本体に比べれば無視できる。 */
static int
ch_has_no_boundary(const std::vector<uint32_t> &tris)
{
	std::unordered_map<uint64_t, int> dir;
	dir.reserve(tris.size() * 2);
	for ( size_t i = 0 ; i + 2 < tris.size() ; i += 3 ) {
		const uint32_t v[3] = { tris[i], tris[i+1], tris[i+2] };
		for ( int k = 0 ; k < 3 ; ++k )
			dir[((uint64_t)v[k] << 32) | (uint64_t)v[(k+1)%3]]++;
	}
	for ( std::unordered_map<uint64_t,int>::const_iterator it = dir.begin() ; it != dir.end() ; ++it ) {
		uint64_t rev = ((it->first & 0xffffffffULL) << 32) | (it->first >> 32);
		if ( dir.find(rev) == dir.end() )
			return 0;   /* 逆向きが無い = 境界がある = arrangement が壊れている */
	}
	return 1;
}

/* ---- ブール (二項も n 項も同じ道) ------------------------------------------
 * IRMB の booleanPipeline は **三角形ごとの label** を受け取り、全オペランドを 1 つの
 * ソウプとして arrangement に掛けてから label の内外で分類する。よって:
 *   union        : どの label の内側でもない三角形を残す
 *   intersection : 全 label の内側 (または表面) の三角形を残す
 *   difference   : label 0 から他の全部を引く (= 左 fold・上流のコメントに明記)
 * ⇒ **中間メッシュを 1 つも作らない**。#3436 P4 の「n 項 (arrangement 1 回) vs 二分木」の
 *   対照相手として、geogram と並ぶ 2 例目になる。 */
sPtr<chMesh>
chMesh::op_bool_nary(sArray<sPtr<chMesh> >& ops, const char *kind, char *err, int errsz)
{
	int n = ops.length();
	if ( n < 2 || n > CH_MAX_OPERANDS || kind == 0 )
		return sPtr<chMesh>();
	for ( int i = 0 ; i < n ; ++i )
		if ( ! ops[i].is_notNull() ) return sPtr<chMesh>();

	BoolOp op;
	if      ( ::strcmp(kind, "union") == 0 )        op = UNION;
	else if ( ::strcmp(kind, "intersection") == 0 ) op = INTERSECTION;
	else if ( ::strcmp(kind, "difference") == 0 )   op = SUBTRACTION;
	else
		return sPtr<chMesh>();

	/* 全オペランドを 1 本のソウプへ連結し、三角形ごとに label (= オペランド番号) を振る。 */
	std::vector<double> in_coords;
	std::vector<uint>   in_tris, in_labels;
	for ( int i = 0 ; i < n ; ++i ) {
		const std::vector<double>   &c = ops[i]->coords();
		const std::vector<uint32_t> &t = ops[i]->tris();
		const uint base = (uint)(in_coords.size() / 3);
		in_coords.insert(in_coords.end(), c.begin(), c.end());
		for ( size_t k = 0 ; k < t.size() ; ++k )
			in_tris.push_back(base + (uint)t[k]);
		for ( size_t k = 0 ; k < t.size() / 3 ; ++k )
			in_labels.push_back((uint)i);
	}

	sPtr<chMesh> out = thNEW(chMesh,());
	std::vector<double>              bool_coords;
	std::vector<uint>                bool_tris;
	std::vector<std::bitset<NBIT> >  bool_labels;
	if ( ! ch_guard([&]{
		booleanPipeline(in_coords, in_tris, in_labels, op, bool_coords, bool_tris, bool_labels);
	}, err, errsz) )
		return sPtr<chMesh>();

	out->coords() = bool_coords;
	out->tris().assign(bool_tris.begin(), bool_tris.end());
	/* ★ 退化した配置で IRMB が壊れた結果を **黙って返さない** (上のコメント参照)。 */
	if ( ! ch_has_no_boundary(out->tris()) ) {
		ch_note_error(err, errsz,
		    "the arrangement produced an open surface (operands in a degenerate configuration: "
		    "the result would be a different value on every run)");
		return sPtr<chMesh>();
	}
	return out;
}

sPtr<chMesh>
chMesh::bool_from_args(sArray<sPtr<pigData> > *args, const char *kind, const char **errmsg,
                       char *errbuf, int errbufsz)
{
	int na = ( args != 0 ) ? args->length() : 0;
	if ( na < 2 ) { *errmsg = "needs at least two cherchi meshes"; return sPtr<chMesh>(); }
	if ( na > CH_MAX_OPERANDS ) {
		/* ★ label は bitset<32>。超えると **黙って壊れる**ので明示エラーにする
		 *   (sig の "(32)" が planner 側の上限だが、直接呼ばれた場合の最後の砦)。 */
		*errmsg = "too many operands for the cherchi n-ary boolean (max 32)";
		return sPtr<chMesh>();
	}
	sArray<sPtr<chMesh> > ops;
	ops.length(na);
	for ( int i = 0 ; i < na ; ++i ) {
		ops[i] = sPtr<chMesh>::d_cast((*args)[i]);
		if ( ! ops[i].is_notNull() ) { *errmsg = "needs cherchi meshes"; return sPtr<chMesh>(); }
	}
	static const int CHERRSZ = 512;
	char why[CHERRSZ];
	why[0] = '\0';
	sPtr<chMesh> r = op_bool_nary(ops, kind, why, CHERRSZ);
	if ( ! r.is_notNull() ) {
		if ( errbuf != 0 && errbufsz > 0 && why[0] != '\0' ) {
			::snprintf(errbuf, (size_t)errbufsz, "%s", why);
			*errmsg = errbuf;
		} else
			*errmsg = "boolean failed";
	}
	return r;
}

/* ---- wire 形式 (MFM3 と同一レイアウト) ------------------------------------ */
static void
put_u32(chChunkSink &sink, uint32_t v)
{
	uint8_t b[4] = { (uint8_t)(v & 0xff), (uint8_t)((v >> 8) & 0xff),
	                 (uint8_t)((v >> 16) & 0xff), (uint8_t)((v >> 24) & 0xff) };
	sink.chunk(b, 4);
}

static uint32_t
get_u32(chChunkSource &src)
{
	uint8_t b[4];
	src.pull(b, 4);
	return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

void
chMesh::encode(chChunkSink &sink)
{
	put_u32(sink, (uint32_t)nverts());
	put_u32(sink, (uint32_t)nfaces());
	for ( size_t v = 0 ; v + 2 < coords_.size() ; v += 3 ) {
		double xyz[3] = { coords_[v], coords_[v+1], coords_[v+2] };
		sink.chunk((const uint8_t*)xyz, (int)sizeof xyz);
	}
	for ( size_t f = 0 ; f + 2 < tris_.size() ; f += 3 ) {
		uint32_t t[3] = { tris_[f], tris_[f+1], tris_[f+2] };
		sink.chunk((const uint8_t*)t, (int)sizeof t);
	}
}

/* ---- cg→ch 昇格読み: cgal の "MESH" (厳密有理数文字列) を double 化して読む。
 *   framing は mfMesh::decode_mesh_exact / ggMesh::decode_mesh_exact と同一 (共通パーサ)。
 *   面は n-gon がありうる → ファン三角化 (平面なら体積は変わらない)。 */
void
chMesh::decode_mesh_exact(chChunkSource &src)
{
	coords_.clear();
	tris_.clear();
	uint32_t nv = srava_exact::get_u32(src);
	uint32_t nf = srava_exact::get_u32(src);
	coords_.reserve((size_t)nv * 3);
	for ( uint32_t i = 0 ; i < nv ; ++i ) {
		double x = srava_exact::get_rational_d(src);
		double y = srava_exact::get_rational_d(src);
		double z = srava_exact::get_rational_d(src);
		coords_.push_back(x); coords_.push_back(y); coords_.push_back(z);
	}
	for ( uint32_t f = 0 ; f < nf ; ++f ) {
		uint32_t nidx = srava_exact::get_u32(src);
		std::vector<uint32_t> idx((size_t)nidx);
		for ( uint32_t j = 0 ; j < nidx ; ++j ) idx[j] = srava_exact::get_u32(src);
		for ( uint32_t j = 1 ; j + 1 < nidx ; ++j )   /* ファン三角化 (index は共有のまま) */
			add_triangle((int)idx[0], (int)idx[j], (int)idx[j+1]);
	}
	/* 色 section は読まない (必要バイトのみ pull 済み・reader が残りを閉じる)。 */
}

void
chMesh::decode(chChunkSource &src)
{
	if ( meshExactInput_ ) { decode_mesh_exact(src); return; }   /* ★ cgal "MESH" → double */
	coords_.clear();
	tris_.clear();
	uint32_t nv = get_u32(src);
	uint32_t nt = get_u32(src);
	coords_.reserve((size_t)nv * 3);
	tris_.reserve((size_t)nt * 3);
	for ( uint32_t i = 0 ; i < nv ; ++i ) {
		double xyz[3];
		src.pull((uint8_t*)xyz, (int)sizeof xyz);
		coords_.push_back(xyz[0]); coords_.push_back(xyz[1]); coords_.push_back(xyz[2]);
	}
	for ( uint32_t i = 0 ; i < nt ; ++i ) {
		uint32_t t[3];
		src.pull((uint8_t*)t, (int)sizeof t);
		tris_.push_back(t[0]); tris_.push_back(t[1]); tris_.push_back(t[2]);
	}
	/* ★ MFM3 の後ろに色 section が続くことがあるが読み飛ばす (このモジュールは色を扱わない)。 */
}

/* ---- 書き出し --------------------------------------------------------------
 * ★ cinolib の write_* ではなく自前で書く。IRMB が引く cinolib の IO ヘッダを足すと
 *   この TU の依存とコンパイル時間が増えるだけで、OFF/STL/OBJ は数十行で書けるため。
 *   拡張子は descriptor.export_exts ("off,stl,obj") と一致させること。 */
static bool
ch_write_off(const char *path, const std::vector<double> &c, const std::vector<uint32_t> &t)
{
	FILE *f = ::fopen(path, "wb");
	if ( f == 0 ) return false;
	::fprintf(f, "OFF\n%d %d 0\n", (int)(c.size()/3), (int)(t.size()/3));
	for ( size_t i = 0 ; i + 2 < c.size() ; i += 3 )
		::fprintf(f, "%.17g %.17g %.17g\n", c[i], c[i+1], c[i+2]);
	for ( size_t i = 0 ; i + 2 < t.size() ; i += 3 )
		::fprintf(f, "3 %u %u %u\n", t[i], t[i+1], t[i+2]);
	::fclose(f);
	return true;
}

static bool
ch_write_obj(const char *path, const std::vector<double> &c, const std::vector<uint32_t> &t)
{
	FILE *f = ::fopen(path, "wb");
	if ( f == 0 ) return false;
	for ( size_t i = 0 ; i + 2 < c.size() ; i += 3 )
		::fprintf(f, "v %.17g %.17g %.17g\n", c[i], c[i+1], c[i+2]);
	for ( size_t i = 0 ; i + 2 < t.size() ; i += 3 )   /* OBJ の index は 1 始まり */
		::fprintf(f, "f %u %u %u\n", t[i]+1, t[i+1]+1, t[i+2]+1);
	::fclose(f);
	return true;
}

static bool
ch_write_stl(const char *path, const std::vector<double> &c, const std::vector<uint32_t> &t)
{
	FILE *f = ::fopen(path, "wb");
	if ( f == 0 ) return false;
	::fprintf(f, "solid srava\n");
	for ( size_t i = 0 ; i + 2 < t.size() ; i += 3 ) {
		const double *a = &c[3*(size_t)t[i]], *b = &c[3*(size_t)t[i+1]], *d = &c[3*(size_t)t[i+2]];
		double u[3] = { b[0]-a[0], b[1]-a[1], b[2]-a[2] };
		double v[3] = { d[0]-a[0], d[1]-a[1], d[2]-a[2] };
		double nx = u[1]*v[2] - u[2]*v[1], ny = u[2]*v[0] - u[0]*v[2], nz = u[0]*v[1] - u[1]*v[0];
		double len = ::sqrt(nx*nx + ny*ny + nz*nz);
		if ( len > 0 ) { nx /= len; ny /= len; nz /= len; }
		::fprintf(f, "  facet normal %.9g %.9g %.9g\n    outer loop\n", nx, ny, nz);
		::fprintf(f, "      vertex %.9g %.9g %.9g\n", a[0], a[1], a[2]);
		::fprintf(f, "      vertex %.9g %.9g %.9g\n", b[0], b[1], b[2]);
		::fprintf(f, "      vertex %.9g %.9g %.9g\n", d[0], d[1], d[2]);
		::fprintf(f, "    endloop\n  endfacet\n");
	}
	::fprintf(f, "endsolid srava\n");
	::fclose(f);
	return true;
}

bool
chMesh::write_to(const char *path, const char *unit)
{
	(void)unit;   /* 単位付きの形式 (3MF/AMF) は未対応 — export_exts で申告していない */
	if ( path == 0 )
		return false;
	const char *dot = ::strrchr(path, '.');
	if ( dot == 0 )
		return false;
	if      ( ::strcasecmp(dot, ".off") == 0 ) return ch_write_off(path, coords_, tris_);
	if      ( ::strcasecmp(dot, ".obj") == 0 ) return ch_write_obj(path, coords_, tris_);
	if      ( ::strcasecmp(dot, ".stl") == 0 ) return ch_write_stl(path, coords_, tris_);
	return false;   /* 知らない拡張子は **黙って別形式で書かない** (黙るフォールバックはバグのもと) */
}

/* ---- reader 用ファクトリ ----
 * 形式 "MFM3" (raw double。manifold / geogram と共有する wire 形式) を受ける。 */
sPtr<chGeom>
chGeom::create_for_meta(const uint8_t *meta, int len)
{
	if ( meta == 0 || len < 4 )
		return sPtr<chGeom>();
	if ( ::memcmp(meta, CH_TAG, 4) == 0 )
		return sPtr<chGeom>::d_cast(thNEW(chMesh,()));
	/* ★ cgal の厳密メッシュ "MESH" も受理する (cast("ch-mesh3d", cgMesh) の昇格読み)。
	 *   decode 時に有理数文字列 → double へ落とす (表現力の高→低なので cast だけがやってよい)。 */
	if ( ::memcmp(meta, "MESH", 4) == 0 ) {
		sPtr<chMesh> m = thNEW(chMesh,());
		m->set_mesh_exact_input();
		return sPtr<chGeom>::d_cast(m);
	}
	return sPtr<chGeom>();
}
