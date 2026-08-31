/*
 * vdGrid — OpenVDB ボリューム幾何の実装 (#3434 P2)。設計の背景はヘッダ冒頭を参照。
 */
#include	"vd/c++/vdGrid.h"
#include	"pig/c++/pigModuleRegistry.h"   /* モジュール専用データの預かり所 (static を置かないため) */
#include	"ts2/c++/stdString.h"

#include	<openvdb/io/Stream.h>
#include	<openvdb/tools/Composite.h>        /* csgUnionCopy / csgIntersectionCopy / csgDifferenceCopy */
#include	<openvdb/tools/LevelSetMeasure.h>  /* levelSetVolume */
#include	<openvdb/tools/LevelSetRebuild.h>  /* levelSetRebuild (volume の直前の作り直し) */

#include	"vd/c++/vdArena.h"   /* ★ #3441: 予算の保持だけ。arena を張るのは各 compute() 側 */

#include	<stdio.h>
#include	<stdlib.h>   /* getenv / atoi */
#include	<string.h>
#include	<sstream>
#include	<string>
#include	<vector>

/* ---- ★ 環境変数 SRAVA_OP_THREADS — op 内並列のスレッド予算 ----
 *
 * ★★ 意味論 (モジュール共通・#3419 と揃える): **1 つの op が op 内並列に使ってよい
 *    スレッド数の上限**。「プロセス全体の上限」ではない。
 *    - process 実行 (現在の vd / geogram / cgal / nef) は **1 プロセス = 1 op** なので
 *      両者は一致し、global_control で張れば正しい。
 *    - in-proc になると **1 プロセスに op が N 個同居**するので、プロセス全体の意味のまま
 *      張ると「同居する全 op の合計」に化けてしまう。そこでは op ごとに
 *      tbb::task_arena / omp_set_num_threads で張る。
 *    定義を最初から「op あたり」にしておけば、**同じ名前・同じ意味で両方に通る**
 *    (同じ変数が環境によって別の意味を持つ、という一番たちの悪い形を避ける)。
 *    未設定 / 0 以下 = そのライブラリの既定 (TBB ならコア数) で、従来どおりの振る舞い。
 *
 * oneTBB には**公式の環境変数が無い**ので、モジュール側で受けて global_control に落とす。
 *
 * ★ なぜ env なのか: vd は EXEC_PROCESS なので、agent は **planner が spawn する子プロセス**。
 *   子の環境は spawn 時に決まるから、「起動時に読まれるだけ」という env の制約がここでは
 *   好都合になる (動的 API を相手ライブラリに差し込む必要が無い)。
 *
 * ★ op 内並列と op 間並列は **取り合わず補い合う** (#3434 P2)。還元木では、
 *   葉では op 間・根では op 内しか効かないため、片方が構造的に無力な区間をもう片方が埋める。
 *   総スレッド数を絞ると、この埋め合わせを潰すぶんかえって遅くなる。
 *   → **プロセス隔離のうちは静的な調停は要らない (むしろ有害)**。 */

/* ★ #3419 (ABI v7): planner の L_THR もここに落ちる (vdtsAgent::on_env → set_thread_budget)。
 *   env は起動時の初期値、C_ENV は実行中の張り替え、という 2 経路が同じ 1 つの
 *   global_control を差し替える。⚠ oneTBB の global_control は **複数生存すると最小値**が
 *   効く (加算ではない) ので、増やす方向にも効かせるには 1 つを持ち替える必要がある。 */
/* ★ #3441 (2026-08-26): **global_control をやめ、値だけ持って計算の入口で task_arena を張る**。
 *   理由は vdArena.h の冒頭 — global_control はプロセス全体に効くので、in-proc (1 プロセスに
 *   op が N 個同居) では「op あたりの上限」という意味が「同居する全 op の合計」に化ける。
 *   0 以下 = 指定なし = TBB の既定 (コア数)。 */
/* ★★ 設定値を **static に置かない** (ひさ設計 2026-08-26)。openvdb は in-proc で走れる
 *   (module(so,{exec_default:"thread"})) ので、可変な file-scope static は op どうしで混線する。
 *   「そのモジュールにひとつ」で正しい状態は **pigModuleRegistry のモジュール専用スロット**
 *   へ stdObject 派生として預ける (set_module_data / module_data)。
 *   ★ 素の幾何クラスからは pig_current_registry() で辿れるので ABI は変えずに済む。 */
class vdModuleData : public stdObject {
public:
	vdModuleData() { opThreads = 0; }
	int	opThreads;   /* module(so,{threads:N})。0 以下 = 指定なし (TBB の既定) */
};

/* ★★ **モジュール別**に預ける (ひさ判断 2026-08-26)。openvdb / openvdb_mf / openvdb_cg /
 * openvdb_gg は別々のモジュールなので、threads:N は**設定されたモジュールの op にだけ**効く。
 * ⚠ 以前は libsrava_vd の static 1 個を 4 モジュールが共有していた (1 つ設定すると全部に効いた)。
 *
 * ★ 「どのモジュールか」は **ABI を変えずに** 2 つの口から取る (ひさ指摘の sCallSection 経由):
 *     読み (op 実行中)   pig_current_module_id()
 *                        in-proc は caller 鎖の ptsMediatorInternal の moduleName、
 *                        agent プロセスは「その .so は 1 本」の単一解決
 *     書き (configure)   registry の configuring_module_id()
 *                        記述子の configure は素の関数ポインタで自分の id を知らないため
 * ⚠ どちらも取れなければ「設定なし」と同じ扱いへ落とす (勝手に既定を変えない)。 */
static sPtr<vdModuleData>
vd_data(int id, int create)
{
	if ( id < 0 )
		return sPtr<vdModuleData>();
	sPtr<pigModuleRegistry> reg = pig_current_registry();
	if ( reg == thNULL )
		return sPtr<vdModuleData>();
	sPtr<vdModuleData> d = sPtr<vdModuleData>::d_cast(reg->module_data(id));
	if ( d == thNULL && create ) {
		d = thNEW(vdModuleData,());
		reg->set_module_data(id, d);
	}
	return d;
}

int
vd_op_thread_budget(void)
{
	sPtr<vdModuleData> d = vd_data(pig_current_module_id(), 0);
	return ( d != thNULL ) ? d->opThreads : 0;
}

void
vdGrid::set_thread_budget(int n)
{
	sPtr<pigModuleRegistry> reg = pig_current_registry();
	int id = ( reg != thNULL ) ? reg->configuring_module_id() : -1;
	sPtr<vdModuleData> d = vd_data(id, 1);
	if ( d != thNULL )
		d->opThreads = ( n > 0 ) ? n : 0;   /* 0 以下 = 指定なしへ戻す */
}

/* ★ #3441 (ABI v10): module("openvdb.so",{threads:N}) の受け口 (記述子の .configure)。
 * ⚠ configure は **module() が実行されるたびに 1 回**呼ばれるだけで、op ごとには呼ばれない。
 *   なので値を static に置き、各 op が計算の入口で vd_in_arena() 経由で読む
 *   (geogram の g_maxThreads と同じ形)。キー名 threads はモジュール横断の規約。
 *   threads:0 (以下) は既定へ戻す — geogram 側も 1e053fe で同じ意味論に揃えられた。 */
void
vdGrid::configure(sPtr<pigData> opts)
{
	if ( opts == thNULL ) return;
	sPtr<pigData> t = opts->get_ix(thNEW(pigDataString,("threads")));
	if ( t.is_notNull() && ! t->is_error() )
		set_thread_budget((int)t->get_int());
}

static void
vd_apply_thread_budget(void)
{
	const char *e = ::getenv("SRAVA_OP_THREADS");
	if ( e == 0 || e[0] == 0 ) return;
	vdGrid::set_thread_budget(::atoi(e));
}

/* ---- OpenVDB のグローバル初期化 ----
 * ★ openvdb::initialize() を呼ばずに Grid を触ると型レジストリが未登録で落ちる。
 *   GEO::initialize() (geogram) と同じ性質で、プロセスに 1 回だけ。
 *   ★ agent は 1 プロセス 1 モジュールなので競合しないが、in-proc 化 (#3419) を見据えて
 *   OpenVDB 自身の多重呼び出し耐性に頼らず、こちらでも 1 回に畳んでおく。 */
void
vdGrid::ensure_init()
{
	/* ⚠ 「初期化したか」の static は置かない (ひさ指示 2026-08-26)。
	 * **openvdb::initialize() は多重呼び出し可** (ライブラリ側が畳む)。
	 * vd_apply_thread_budget() も冪等 (現在値を設定し直すだけ)。 */
	openvdb::initialize();
	vd_apply_thread_budget();
}

vdGrid::vdGrid(sPtr<pigInfo> i) : vdGeom(i)
{
	ensure_init();
	g_ = openvdb::FloatGrid::create();   /* background = 0。実体は各 op が入れる */
}

sPtr<stdString>
vdGrid::get_str()
{
	char buf[96];
	::snprintf(buf, sizeof buf, "<grid:openvdb voxels=%d dx=%.6g>",
	           active_voxels(), voxel_size());
	return thNEW(stdString,(buf));
}

double
vdGrid::voxel_size() const
{
	if ( ! g_ ) return 0.0;
	return g_->transform().voxelSize()[0];   /* 等方前提 (voxelize が等方でしか作らない) */
}

int
vdGrid::active_voxels() const
{
	if ( ! g_ ) return 0;
	return (int)g_->activeVoxelCount();
}

/* ---- 場が真の距離場かどうかの印 (grid メタデータ = .vdb を越える) ---- */
static const char *VD_META_NORMALIZED = "srava_normalized";

void
vdGrid::set_normalized(bool v)
{
	if ( ! g_ ) return;
	g_->removeMeta(VD_META_NORMALIZED);
	g_->insertMeta(VD_META_NORMALIZED, openvdb::BoolMetadata(v));
}

bool
vdGrid::is_normalized() const
{
	if ( ! g_ ) return true;
	openvdb::BoolMetadata::ConstPtr m = g_->getMetadata<openvdb::BoolMetadata>(VD_META_NORMALIZED);
	/* ★ 印が無ければ正規化済みとみなす。voxelize / renormalize は必ず印を付けるので、
	 *   印が無いのは srava の外で作られた .vdb だけ。 */
	if ( ! m ) return true;
	return m->value();
}

double
vdGrid::volume() const
{
	if ( ! g_ ) return 0.0;
	/* ★ level set の体積は等値面が囲む世界座標系の体積。**メッシュを作らずに**出せるのが
	 *   ボリューム表現の利点 (メッシュ系は面を積む)。解像度に依存する近似値なので、
	 *   メッシュ系との一致は「相対誤差」で見る (bit 一致は要求しない)。 */
	if ( is_normalized() )
		return openvdb::tools::levelSetVolume(*g_, /*useWorldSpace=*/true);
	/* ★ ブールの結果は真の距離場ではない (|grad| = 1 が崩れている) ので levelSetVolume が偏る。
	 *   **測る直前に作り直してから**測る。作り直しのコストより、黙って間違った値を返さないことを
	 *   優先する (ひさ判断 2026-08-19)。 */
	openvdb::FloatGrid::Ptr tmp =
	    openvdb::tools::levelSetRebuild(*g_, /*isovalue=*/0.0f,
	                                    (float)openvdb::LEVEL_SET_HALF_WIDTH);
	if ( ! tmp ) return openvdb::tools::levelSetVolume(*g_, true);   /* 作り直せない = 素で返す */
	return openvdb::tools::levelSetVolume(*tmp, /*useWorldSpace=*/true);
}

/* ---- ブール = 点ごとの min/max (tools/Composite.h) ----
 * ★ csgUnion 等は **破壊的** (a に結果を書き b を空にする) なので、非破壊版 *Copy を使う。
 *   srava は DAG キャッシュで入力を共有しうる (dedup) ため、入力を壊す API は使えない。 */
/* ★ ブールの結果は **真の距離場ではない**ので、必ずその印を付けて返す。
 *   印は grid メタデータなので .vdb キャッシュを越え、warm 実行でも同じ判断になる。 */
static sPtr<vdGrid>
vd_wrap(openvdb::FloatGrid::Ptr g)
{
	if ( ! g ) return sPtr<vdGrid>();
	sPtr<vdGrid> out = thNEW(vdGrid,());
	out->set_grid(g);
	out->set_normalized(false);
	return out;
}

sPtr<vdGrid>
vdGrid::op_union(sPtr<vdGrid> b)
{
	ensure_init();
	if ( b == thNULL || !g_ || !b->grid() ) return sPtr<vdGrid>();
	return vd_wrap(openvdb::tools::csgUnionCopy(*g_, *b->grid()));
}

sPtr<vdGrid>
vdGrid::op_intersection(sPtr<vdGrid> b)
{
	ensure_init();
	if ( b == thNULL || !g_ || !b->grid() ) return sPtr<vdGrid>();
	return vd_wrap(openvdb::tools::csgIntersectionCopy(*g_, *b->grid()));
}

sPtr<vdGrid>
vdGrid::op_difference(sPtr<vdGrid> b)
{
	ensure_init();
	if ( b == thNULL || !g_ || !b->grid() ) return sPtr<vdGrid>();
	return vd_wrap(openvdb::tools::csgDifferenceCopy(*g_, *b->grid()));
}

/* ---- wire 形式 (D_META 4CC "VDB ") ------------------------------------------
 *   [u64 nbytes][openvdb::io::Stream が書いた bytes]
 *
 * ★ 長さ接頭辞を付ける理由: chunk Source の API は「n バイトきっかり取る」pull() と
 *   「まだ続きがあるか」の more() しか無く、「残り全部」を取る手段が無い。1 バイトずつ
 *   more()/pull(1) で回すこともできるが、20〜40MB のグリッドでは論外なので長さを前置する。
 *   接頭辞の 8 バイトを除けば中身は **素の .vdb** (ネイティブ)。
 *
 * ★ std::stringstream を経由する (= 一時的に payload をもう 1 部持つ) のは、
 *   openvdb::io::Archive が **seek する**ため。非 seek 可能な streambuf を被せると壊れる。
 *   メモリを 1 部余分に使うので、実測で効いてくるようなら最適化候補 (P2 の測定項目)。 */
static void put_u64(vdChunkSink &sink, uint64_t v)
{
	uint8_t b[8];
	for ( int i = 0 ; i < 8 ; ++i ) b[i] = (uint8_t)((v >> (8 * i)) & 0xff);
	sink.chunk(b, 8);
}
static uint64_t get_u64(vdChunkSource &src)
{
	uint8_t b[8];
	src.pull(b, 8);
	uint64_t v = 0;
	for ( int i = 7 ; i >= 0 ; --i ) v = (v << 8) | (uint64_t)b[i];
	return v;
}

void
vdGrid::encode(vdChunkSink &sink)
{
	ensure_init();
	std::ostringstream os(std::ios_base::out | std::ios_base::binary);
	openvdb::GridCPtrVec grids;
	if ( g_ ) grids.push_back(g_);
	openvdb::io::Stream(os).write(grids);
	const std::string &s = os.str();
	put_u64(sink, (uint64_t)s.size());
	if ( ! s.empty() )
		sink.chunk((const uint8_t*)s.data(), (int)s.size());
}

void
vdGrid::decode(vdChunkSource &src)
{
	ensure_init();
	uint64_t n = get_u64(src);
	/* 壊れた/巨大な長さで確保して落ちないよう上限を置く (16GB)。超えたら decode 失敗。 */
	if ( n > (uint64_t)16 * 1024 * 1024 * 1024 ) { decodeErr_ = 1; return; }
	std::string buf;
	buf.resize((size_t)n);
	if ( n > 0 ) src.pull((uint8_t*)&buf[0], (int)n);
	std::istringstream is(buf, std::ios_base::in | std::ios_base::binary);
	openvdb::io::Stream strm(is, /*delayLoad=*/false);
	openvdb::GridPtrVecPtr grids = strm.getGrids();
	if ( ! grids || grids->empty() ) { decodeErr_ = 1; return; }
	g_ = openvdb::gridPtrCast<openvdb::FloatGrid>((*grids)[0]);
	if ( ! g_ ) decodeErr_ = 1;   /* FloatGrid 以外 (今は扱わない) */
}

sPtr<vdGeom>
vdGeom::create_for_meta(const uint8_t *meta, int len)
{
	if ( meta == 0 || len < 4 )
		return sPtr<vdGeom>();
	if ( ::memcmp(meta, VD_TAG, 4) == 0 )
		return sPtr<vdGeom>::d_cast(thNEW(vdGrid,()));
	/* ★ 2026-08-29 (ひさ判断): 旧 MFM3 / MESH の枝 (運搬用メッシュ vdMesh) を **撤去**した。
	 *   #3434 で voxelize / isosurface が openvdb_mf/cg/gg へ移り、このモジュールに
	 *   メッシュを扱う op が 1 つも無くなっていた (OPS 表に無い = 到達不能)。 */
	return sPtr<vdGeom>();
}

bool
vdGrid::write_to(const char *path, const char *unit)
{
	(void)unit;   /* 単位付きの形式は未対応 — export_exts で申告していない */
	ensure_init();
	if ( ! g_ ) return false;
	openvdb::io::File f(path);
	openvdb::GridCPtrVec grids;
	grids.push_back(g_);
	f.write(grids);
	f.close();
	return true;
}


/* ---- n 項ブール (#3436 P4) --------------------------------------------------
 * openvdb の CSG は格子点ごとの min/max なので本来 n 項。API は二項なのでここで逐次に畳む。
 * ★ 効くのは「中間結果を .vdb へ書き出して読み直す往復が消える」ところで、geogram/occt の
 *   「n 個をまとめて 1 回の交差計算」とは効き方が違う (P4 の対比ではここを区別する)。
 * ⚠ 格子が揃っていないと min/max が意味を持たない。**黙って resample するフォールバックは
 *   入れない** ので、最初に全オペランドの voxel size をまとめて検査して明示エラーにする。 */
sPtr<vdGrid>
vdGrid::bool_from_args(sArray<sPtr<pigData> > *args, const char *kind,
                       const char **errmsg, char *errbuf, int errbufsz)
{
	int na = ( args != 0 ) ? args->length() : 0;
	if ( na < 2 ) { *errmsg = "needs at least two openvdb grids"; return sPtr<vdGrid>(); }
	sArray<sPtr<vdGrid> > ops;
	ops.length(na);
	double d0 = 0;
	for ( int i = 0 ; i < na ; ++i ) {
		ops[i] = sPtr<vdGrid>::d_cast((*args)[i]);
		if ( ! ops[i].is_notNull() ) { *errmsg = "needs openvdb grids"; return sPtr<vdGrid>(); }
		double d = ops[i]->voxel_size();
		if ( i == 0 ) d0 = d;
		if ( !(d > 0) || !(d0 > 0) || d != d0 ) {
			::snprintf(errbuf, (size_t)errbufsz,
			    "voxel sizes differ (%.17g vs %.17g) — voxelize all operands with the same dx",
			    d0, d);
			*errmsg = errbuf;
			return sPtr<vdGrid>();
		}
	}
	sPtr<vdGrid> acc = ops[0];
	for ( int i = 1 ; i < na ; ++i ) {
		if      ( ::strcmp(kind, "union") == 0 )        acc = acc->op_union(ops[i]);
		else if ( ::strcmp(kind, "intersection") == 0 ) acc = acc->op_intersection(ops[i]);
		else                                            acc = acc->op_difference(ops[i]);
		if ( ! acc.is_notNull() ) { *errmsg = "openvdb CSG failed"; return sPtr<vdGrid>(); }
	}
	return acc;
}
