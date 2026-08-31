/*
 * ggMesh — geogram 幾何の実装 (#3435 P3)。設計の背景はヘッダ冒頭を参照。
 */
#include	"gg/c++/ggMesh.h"
#include	"ts2/c++/stdString.h"

#include	<geogram/basic/common.h>
#include	<geogram/basic/logger.h>
#include	<geogram/mesh/mesh_geometry.h>
#include	<geogram/mesh/mesh_io.h>
#include	<geogram/mesh/mesh_repair.h>
#include	<geogram/mesh/mesh_surface_intersection.h>

#include	"pig/c++/pigModuleRegistry.h"   /* モジュール専用データの預かり所 (static を置かないため) */
#include	"common/exact_wire.h"   /* cgal 厳密 wire の有理数文字列パーサ (manifold と共通) */

#include	<stdio.h>
#include	<string.h>
#include	<vector>
#include	<string>
#include	<stdexcept>

/* ---- geogram のグローバル初期化 ----
 * ★ GEO::initialize() を呼ばずに Mesh を触ると述語が未初期化で落ちる。プロセスに 1 回だけ。
 *   ログは既定で饒舌 (計測の邪魔になり、agent の stdout はワイヤでもある) ので黙らせる。 */
/* ⚠ 「初期化したか」の static は置かない (ひさ指示 2026-08-26)。**GEO::initialize() 自身が
 *   関数内 static (std::optional の singleton) で冪等**なので、srava 側でガードする必要が無い。
 *   GEO::Logger::set_quiet(true) も繰り返して無害。⇒ ensure_init() は何度呼んでもよい。
 *
 * ★★ module(so,{threads:N}) の設定値も **static に置かない** (ひさ設計 2026-08-26)。
 *   モジュールは in-proc で走りうるので、可変な file-scope static は op どうしで混線する。
 *   「そのモジュールにひとつ」で正しい状態は **pigModuleRegistry のモジュール専用スロット**
 *   (set_module_data / module_data) に stdObject 派生として預ける。
 *   ★ 素の幾何クラスは ptsObject 派生ではないので ptsApp に届かないが、
 *     **pig_current_registry()** が sCallSection の TLS から辿ってくれるので ABI は変えずに済む。 */
class ggModuleData : public stdObject {
public:
	ggModuleData() { maxThreads = 0; }
	int	maxThreads;   /* module(so,{threads:N})。0 以下 = 未指定/解除 (nproc のまま) */
};

/* このモジュールの預かり物を引く (無ければ作って預ける)。
 * ★ ggMesh はライブラリ srava_gg に 1 つだけあり、**geogram と openvdb_gg が共有**する。
 *   ただし `.configure = &ggMesh::configure` を宣言しているのは **geogram だけ**なので、
 *   書き手は必ず "geogram" が登録された後に来る。読み手が openvdb_gg 側から来て
 *   "geogram" 未登録なら id=-1 → thNULL → 「設定なし」で正しい (誰も設定できていないため)。
 *   ⚠ openvdb 側は configure を 2 モジュール (openvdb / openvdb_mf) が宣言していて
 *     事情が違う → vdGrid.cpp の VD_FAMILY を参照。
 * ⚠ 呼び出し文脈が取れないと registry は thNULL。そのときは thNULL を返し、呼び手は
 *   「設定が無い」と同じ扱いにする (勝手に既定を変えない)。 */
static sPtr<ggModuleData>
gg_data()
{
	sPtr<pigModuleRegistry> reg = pig_current_registry();
	if ( reg == thNULL )
		return sPtr<ggModuleData>();
	int id = reg->id_of_name("geogram");
	if ( id < 0 )
		return sPtr<ggModuleData>();
	sPtr<ggModuleData> d = sPtr<ggModuleData>::d_cast(reg->module_data(id));
	if ( d == thNULL ) {
		d = thNEW(ggModuleData,());
		reg->set_module_data(id, d);
	}
	return d;
}

void
ggMesh::ensure_init()
{
	GEO::initialize();      /* ★ 冪等 (geogram 側が関数内 static で 1 回に畳む) */
	GEO::Logger::instance()->set_quiet(true);
	sPtr<ggModuleData> d = gg_data();
	/* ★ #3441: configure() が GEO::initialize() より先に呼ばれていた場合(スクリプトの先頭で
	 * module("geogram.so",{threads:N}) した場合)はここで適用する。後に呼ばれた場合は
	 * configure() 自身が即時に適用する(下)。 */
	if ( d != thNULL && d->maxThreads > 0 )
		GEO::Process::set_max_threads((GEO::index_t)d->maxThreads);
}

/* ★ #3441 (ひさ設計 2026-08-26): module("geogram.so",{threads:N}) を受ける。
 * ⚠ **既定は変えない** — threads 未指定 (opts に無い/opts が thNULL) なら何もせず、geogram は
 * 従来どおり GEO::initialize() 内で nproc をそのまま使う。実測で「一番速い arity が一番
 * スレッド圧も高い」ことが分かっているため、絞ることを既定にはしない
 * (docs/srava_load_control_design.md §17.6)。
 * ★ 2026-08-26 追記 (bench 指摘): **`threads:N` の意味づけを N>0=上限・N<=0=既定へ戻す、に統一**
 * (openvdb 側の task_arena 実装と揃える)。「一度絞ったら戻せない」だと in-proc で
 * `module()` を script 中に複数回呼んで途中から緩める、という使い方ができなくなるため。
 * ⚠ 「未指定 (キー自体が無い)」と「明示的に 0 以下を指定」は区別しない — どちらも
 * 「このモジュールの現在の制限は解除する」という同じ操作として扱う。 */
void
ggMesh::configure(sPtr<pigData> opts)
{
	sPtr<ggModuleData> d = gg_data();
	if ( d == thNULL )
		return;   /* 呼び出し文脈が取れない = 預け先が無い。設定は取り込まない */
	if ( opts.is_notNull() ) {
		sPtr<pigData> t = opts->get_ix(thNEW(pigDataString,("threads")));
		if ( t.is_notNull() && ! t->is_error() )
			d->maxThreads = (int)t->get_int();   /* n<=0 も含めてそのまま保持 (「既定へ戻す」に使う) */
	}
	/* ★ 順序の心配をしない: ensure_init() は冪等なのでここで呼んでしまえばよい
	 * (以前は g_geoInit を見て「まだなら諦める」としていた)。 */
	ensure_init();
	/* 即座に反映。n<=0 (未指定/明示解除) は
	 * maximum_concurrent_threads() (= GEO::initialize() が元々使っていた既定値) へ戻す。 */
	GEO::Process::set_max_threads(
	    ( d->maxThreads > 0 ) ? (GEO::index_t)d->maxThreads
	                          : GEO::Process::maximum_concurrent_threads());
}

ggMesh::ggMesh(sPtr<pigInfo> i)
	: ggGeom(i)
{
	ensure_init();
	m_.vertices.set_dimension(3);
}

int
ggMesh::add_vertex(double x, double y, double z)
{
	GEO::index_t v = m_.vertices.create_vertex();
	m_.vertices.point(v) = GEO::vec3(x, y, z);
	return (int)v;
}

void
ggMesh::add_triangle(int a, int b, int c)
{
	m_.facets.create_triangle((GEO::index_t)a, (GEO::index_t)b, (GEO::index_t)c);
}

double
ggMesh::volume() const
{
	/* 閉曲面が囲む体積 (発散定理)。外向き CCW で正。 */
	return GEO::Geom::mesh_enclosed_volume(const_cast<GEO::Mesh&>(m_));
}

sPtr<stdString>
ggMesh::get_str()
{
	char buf[128];
	::snprintf(buf, sizeof buf, "<gg-mesh3d verts=%d faces=%d>", nverts(), nfaces());
	return thNEW(stdString,(buf));
}

/* ---- geogram の FATAL を srava のエラーへ落とす ----------------------------
 * ★ geogram は arrangement が破綻すると **std::runtime_error を投げて**出てくる
 *   (例: mesh_surface_intersection.cpp の RadialSort が「同一半平面に 2 三角形」で
 *   `Did not manage to sort a bundle` → geo_assert_not_reached)。捕まえないと agent が
 *   terminate() → SIGABRT で死に、planner からは原因が読めない。cgal (cgMesh3D) /
 *   nef (nf_try_build) / occt (oc_guard) と同じ作法。
 * ⚠ 捕まえるのは例外だけ。SIGSEGV 等はここでは受けない (受けてはいけない)。
 * ⚠⚠ **理由をモジュール大域 (static) に置かない** (ひさ指示 2026-08-26)。in-proc 実行では
 *   1 プロセスに複数 op が同居しうるので混線する。**呼び手のバッファへ書く**ことで
 *   リエントラントに保つ。err==0 なら理由は捨てる (呼び手が要らない場合)。
 * ⚠ geogram は **並列区間のワーカースレッドからも投げる**。そちらはここを素通りして
 *   terminate する (geogram のスレッド管理が例外を受けないため)。 */
static void
gg_note_error(char *err, int errsz, const char *what)
{
	if ( err == 0 || errsz <= 0 )
		return;
	const char *w = ( what != 0 && *what != '\0' ) ? what : "geogram fatal error";
	::snprintf(err, (size_t)errsz, "%s", w);
	/* 複数行 (geogram は file/line を改行で足す) は 1 行へ潰す。エラー文は 1 行が読みやすい。 */
	for ( char *p = err ; *p ; ++p ) if ( *p == '\n' || *p == '\r' ) *p = ' ';
}

/* 呼ぶ側の定型。f() が例外を投げたら 0 を返し、理由を err へ書く。 */
template <class F>
static int
gg_guard(F f, char *err, int errsz)
{
	try {
		f();
		return 1;
	} catch ( const std::exception &e ) {
		gg_note_error(err, errsz, e.what());
		return 0;
	} catch ( ... ) {
		gg_note_error(err, errsz, 0);
		return 0;
	}
}

/* ---- ブール ---------------------------------------------------------------
 * geogram の mesh_boolean_operation は「閉じていて自己交差の無い 2 つの曲面」を前提にする。
 * 結果は新しい Mesh。失敗 (空になる等) はここでは判定せず、呼び手が volume 等で見る。 */
static sPtr<ggMesh>
gg_bool(const GEO::Mesh &A, sPtr<ggMesh> b, const char *expr, char *err, int errsz)
{
	if ( ! b.is_notNull() )
		return sPtr<ggMesh>();
	ggMesh::ensure_init();
	sPtr<ggMesh> out = thNEW(ggMesh,());
	if ( ! gg_guard([&]{ GEO::mesh_boolean_operation(out->mesh(), A, b->mesh(), expr); }, err, errsz) )
		return sPtr<ggMesh>();
	return out;
}

sPtr<ggMesh> ggMesh::op_union(sPtr<ggMesh> b, char *e, int n)        { return gg_bool(m_, b, "A+B", e, n); }
sPtr<ggMesh> ggMesh::op_intersection(sPtr<ggMesh> b, char *e, int n) { return gg_bool(m_, b, "A*B", e, n); }
sPtr<ggMesh> ggMesh::op_difference(sPtr<ggMesh> b, char *e, int n)   { return gg_bool(m_, b, "A-B", e, n); }

/* ---- n 項ブール (#3436 P4) --------------------------------------------------
 * geogram の n 項ブールは「全オペランドを 1 つの Mesh に集め、facet 属性 "operand_bit" に
 * 1<<i を立て、MeshSurfaceIntersection で **arrangement を 1 回**だけ走らせ、classify(式) で
 * 内外を決める」形。二項 mesh_boolean_operation はこの 2 オペランド版にすぎない
 * (mesh_surface_intersection.cpp の copy_operand + classify と同じ手順をここで n 個に開く)。
 * ★ 中間メッシュを作らないので、二項の木と違って **中間キャッシュが 1 個も無い** —
 *   これが P4 の「n 項 (arrangement 1 回) vs DAG cache + op 間並列」の対比そのもの。 */
static void
gg_copy_operand(GEO::Mesh& result, const GEO::Mesh& operand, GEO::index_t operand_id)
{
	GEO::Attribute<GEO::index_t> operand_bit(result.facets.attributes(), "operand_bit");
	GEO::index_t v_ofs = result.vertices.create_vertices(operand.vertices.nb());
	for ( GEO::index_t v : operand.vertices )
		result.vertices.point(v + v_ofs) = operand.vertices.point(v);
	for ( GEO::index_t f1 : operand.facets ) {
		GEO::index_t N = operand.facets.nb_vertices(f1);
		GEO::index_t f2 = result.facets.create_polygon(N);
		for ( GEO::index_t lv = 0 ; lv < N ; ++lv )
			result.facets.set_vertex(f2, lv, operand.facets.vertex(f1, lv) + v_ofs);
		operand_bit[f2] = GEO::index_t(1) << operand_id;
	}
}

sPtr<ggMesh>
ggMesh::op_bool_nary(sArray<sPtr<ggMesh> >& ops, const char *kind, char *err, int errsz)
{
	int n = ops.length();
	if ( n < 2 || n > GG_MAX_OPERANDS || kind == 0 )
		return sPtr<ggMesh>();
	for ( int i = 0 ; i < n ; ++i )
		if ( ! ops[i].is_notNull() ) return sPtr<ggMesh>();

	/* 式を組む。union/intersection は geogram の特殊値 (「全部の和/積」) をそのまま使う。
	 * difference は左 fold なので x0-x1-x2-… (parse_or は左結合なので ((x0&!x1)&!x2)…)。 */
	std::string expr;
	if ( ::strcmp(kind, "union") == 0 )
		expr = "union";
	else if ( ::strcmp(kind, "intersection") == 0 )
		expr = "intersection";
	else if ( ::strcmp(kind, "difference") == 0 ) {
		char buf[16];
		for ( int i = 0 ; i < n ; ++i ) {
			::snprintf(buf, sizeof buf, "%sx%d", ( i == 0 ? "" : "-" ), i);
			expr += buf;
		}
	} else
		return sPtr<ggMesh>();

	ensure_init();
	sPtr<ggMesh> out = thNEW(ggMesh,());
	GEO::Mesh &R = out->mesh();
	R.clear();
	R.vertices.set_dimension(3);
	for ( int i = 0 ; i < n ; ++i )
		gg_copy_operand(R, ops[i]->mesh(), (GEO::index_t)i);

	if ( ! gg_guard([&]{
		GEO::MeshSurfaceIntersection I(R);
		I.set_radial_sort(true);
		I.intersect();
		I.classify(expr);
		I.simplify_coplanar_facets();   /* 二項 mesh_boolean_operation の既定と揃える */
	}, err, errsz) )
		return sPtr<ggMesh>();
	return out;
}

sPtr<ggMesh>
ggMesh::bool_from_args(sArray<sPtr<pigData> > *args, const char *kind, const char **errmsg,
                       char *errbuf, int errbufsz)
{
	int na = ( args != 0 ) ? args->length() : 0;
	if ( na < 2 ) { *errmsg = "needs at least two geogram meshes"; return sPtr<ggMesh>(); }
	if ( na > GG_MAX_OPERANDS ) {
		/* ★ operand_bit は 32 bit。超えると **黙って壊れる**ので明示エラーにする
		 *   (sig の "(32)" が planner 側の上限だが、直接呼ばれた場合の最後の砦)。 */
		*errmsg = "too many operands for the geogram n-ary boolean (max 32)";
		return sPtr<ggMesh>();
	}
	sArray<sPtr<ggMesh> > ops;
	ops.length(na);
	for ( int i = 0 ; i < na ; ++i ) {
		ops[i] = sPtr<ggMesh>::d_cast((*args)[i]);
		if ( ! ops[i].is_notNull() ) { *errmsg = "needs geogram meshes"; return sPtr<ggMesh>(); }
	}
	sPtr<ggMesh> r;
	/* ★ 理由の受け皿は **この呼び出しのローカル** (static を置かない)。呼び手が errmsg で
	 * 受け取り、自分の文言に組み込む。 */
	static const int GGERRSZ = 512;
	char why[GGERRSZ];
	why[0] = '\0';
	if ( na == 2 ) {
		/* 2 項は従来の二項 API のまま (既存キャッシュを byte 不変に保つ)。 */
		if      ( ::strcmp(kind, "union") == 0 )        r = ops[0]->op_union(ops[1], why, GGERRSZ);
		else if ( ::strcmp(kind, "intersection") == 0 ) r = ops[0]->op_intersection(ops[1], why, GGERRSZ);
		else                                            r = ops[0]->op_difference(ops[1], why, GGERRSZ);
	} else
		r = op_bool_nary(ops, kind, why, GGERRSZ);
	if ( ! r.is_notNull() ) {
		/* ⚠ why はこの関数のローカルなので、そのままポインタを返すと dangling になる。
		 * 呼び手 (gga*) は受け取った直後に自分のバッファへ組むが、それでも安全側に倒して
		 * **errmsg 用の受け皿を呼び手に持たせる**のが本来。ここでは errmsg が指す先へ
		 * 直接書けないため、理由がある場合のみ静的な文言と合成せず why を errbuf へ移す。 */
		if ( errbuf != 0 && errbufsz > 0 && why[0] != '\0' ) {
			::snprintf(errbuf, (size_t)errbufsz, "%s", why);
			*errmsg = errbuf;
		} else
			*errmsg = "boolean failed";
	}
	return r;
}

/* ---- ソリッド再構成 (#3445) -----------------------------------------------
 * 自己交差した閉メッシュを arrangement で解き、**外側のシェルだけ**を残す。
 * cgal の corefinement は自己交差を素通りして誤った体積を返し、Nef は SNC を組めない。
 * geogram はここを正面から扱えるのが質的な差 (#3435 の受け入れ条件)。 */
sPtr<ggMesh>
ggMesh::op_solidify(char *err, int errsz)
{
	ensure_init();
	sPtr<ggMesh> out = thNEW(ggMesh,());
	out->mesh().copy(m_);
	if ( ! gg_guard([&]{
		GEO::MeshSurfaceIntersection I(out->mesh());
		I.set_radial_sort(true);      /* remove_internal_shells の前提 */
		I.set_verbose(false);
		I.intersect();
		I.remove_internal_shells();   /* 内部シェルを捨てて外側だけ残す = 内外の決め直し */
		GEO::mesh_repair(out->mesh(), GEO::MESH_REPAIR_DEFAULT);
	}, err, errsz) )
		return sPtr<ggMesh>();
	return out;
}

/* ---- wire 形式 (MFM3 と同一レイアウト) ------------------------------------ */
static void
put_u32(ggChunkSink &sink, uint32_t v)
{
	uint8_t b[4] = { (uint8_t)(v & 0xff), (uint8_t)((v >> 8) & 0xff),
	                 (uint8_t)((v >> 16) & 0xff), (uint8_t)((v >> 24) & 0xff) };
	sink.chunk(b, 4);
}

static uint32_t
get_u32(ggChunkSource &src)
{
	uint8_t b[4];
	src.pull(b, 4);
	return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

void
ggMesh::encode(ggChunkSink &sink)
{
	uint32_t nv = (uint32_t)m_.vertices.nb();
	uint32_t nt = (uint32_t)m_.facets.nb();
	put_u32(sink, nv);
	put_u32(sink, nt);
	for ( GEO::index_t v = 0 ; v < m_.vertices.nb() ; ++v ) {
		const GEO::vec3 &p = m_.vertices.point(v);
		double xyz[3] = { p.x, p.y, p.z };
		sink.chunk((const uint8_t*)xyz, (int)sizeof xyz);
	}
	for ( GEO::index_t f = 0 ; f < m_.facets.nb() ; ++f ) {
		/* ★ 三角形だけを書く。geogram のブール結果は同一平面の面を統合して**多角形**になりうる
		 *   ので、書き出し前に三角形化しておく必要がある (encode の呼び手が保証する)。 */
		uint32_t t[3] = { (uint32_t)m_.facets.vertex(f, 0),
		                  (uint32_t)m_.facets.vertex(f, 1),
		                  (uint32_t)m_.facets.vertex(f, 2) };
		sink.chunk((const uint8_t*)t, (int)sizeof t);
	}
}

/* ---- cg→gg 昇格読み: cgal の "MESH" (厳密有理数文字列) を double 化して読む。
 *   framing: [u32 nv][u32 nf] / 頂点×nv (x,y,z = 各 [u32 len][len byte の "p/q" or 整数]) /
 *            面×nf ([u32 nidx][u32 idx]…) / 色 section。パーサは common/exact_wire.h なので
 *   **CGAL をリンクしない** (mfMesh::decode_mesh_exact と同じ実体を共有する)。
 *   面は n-gon がありうる → ファン三角化 (平面なら体積は変わらない。encode が三角形しか書けない
 *   ので、ここで三角形に落としておく方が後段も安全)。頂点 index は cgal 側で共有済み = 統合不要。 */
void
ggMesh::decode_mesh_exact(ggChunkSource &src)
{
	ensure_init();
	m_.clear();
	m_.vertices.set_dimension(3);
	uint32_t nv = srava_exact::get_u32(src);
	uint32_t nf = srava_exact::get_u32(src);
	if ( nv > 0 ) {
		GEO::index_t v0 = m_.vertices.create_vertices((GEO::index_t)nv);
		for ( uint32_t i = 0 ; i < nv ; ++i ) {
			double x = srava_exact::get_rational_d(src);
			double y = srava_exact::get_rational_d(src);
			double z = srava_exact::get_rational_d(src);
			m_.vertices.point(v0 + (GEO::index_t)i) = GEO::vec3(x, y, z);
		}
	}
	for ( uint32_t f = 0 ; f < nf ; ++f ) {
		uint32_t nidx = srava_exact::get_u32(src);
		std::vector<uint32_t> idx((size_t)nidx);
		for ( uint32_t j = 0 ; j < nidx ; ++j ) idx[j] = srava_exact::get_u32(src);
		for ( uint32_t j = 1 ; j + 1 < nidx ; ++j )   /* ファン三角化 (index は共有のまま) */
			m_.facets.create_triangle((GEO::index_t)idx[0], (GEO::index_t)idx[j],
			                          (GEO::index_t)idx[j+1]);
	}
	m_.facets.connect();
	/* 色 section は読まない (必要バイトのみ pull 済み・reader が残りを閉じる)。 */
}

void
ggMesh::decode(ggChunkSource &src)
{
	if ( meshExactInput_ ) { decode_mesh_exact(src); return; }   /* ★ cgal "MESH" → double */
	ensure_init();
	m_.clear();
	m_.vertices.set_dimension(3);
	uint32_t nv = get_u32(src);
	uint32_t nt = get_u32(src);
	if ( nv > 0 ) {
		GEO::index_t v0 = m_.vertices.create_vertices((GEO::index_t)nv);
		for ( uint32_t i = 0 ; i < nv ; ++i ) {
			double xyz[3];
			src.pull((uint8_t*)xyz, (int)sizeof xyz);
			m_.vertices.point(v0 + (GEO::index_t)i) = GEO::vec3(xyz[0], xyz[1], xyz[2]);
		}
	}
	for ( uint32_t i = 0 ; i < nt ; ++i ) {
		uint32_t t[3];
		src.pull((uint8_t*)t, (int)sizeof t);
		m_.facets.create_triangle((GEO::index_t)t[0], (GEO::index_t)t[1], (GEO::index_t)t[2]);
	}
	m_.facets.connect();
	/* ★ MFM3 を読んだときはこの後ろに色 section が続くことがあるが、**読み飛ばす**
	 *   (geogram モジュールは色を扱わない)。src はここで止めてよい。 */
}

bool
ggMesh::write_to(const char *path, const char *unit)
{
	(void)unit;   /* 単位付きの形式 (3MF/AMF) は未対応 — export_exts で申告していない */
	ensure_init();
	return GEO::mesh_save(m_, path);
}

/* ---- reader 用ファクトリ ----
 * 形式 "MFM3" (raw double。manifold と共有する wire 形式) を受ける。
 * ★ 2026-08-19: 自前 4CC "GGM3" は撤去したので、mf からの昇格読みは「同じ形式を読む」ことに
 *   なった (特別扱いが 1 本消えた)。 */
sPtr<ggGeom>
ggGeom::create_for_meta(const uint8_t *meta, int len)
{
	if ( meta == 0 || len < 4 )
		return sPtr<ggGeom>();
	if ( ::memcmp(meta, GG_TAG, 4) == 0 )
		return sPtr<ggGeom>::d_cast(thNEW(ggMesh,()));
	/* ★ cgal の厳密メッシュ "MESH" も受理する (cast("gg-mesh3d", cgMesh) の昇格読み)。
	 *   decode 時に有理数文字列 → double へ落とす。geogram 自身が double 表現なので、
	 *   ここでの丸めは cgal→manifold の downgrade と同じ性質の損失変換。 */
	if ( ::memcmp(meta, "MESH", 4) == 0 ) {
		sPtr<ggMesh> m = thNEW(ggMesh,());
		m->set_mesh_exact_input();
		return sPtr<ggGeom>::d_cast(m);
	}
	return sPtr<ggGeom>();
}
