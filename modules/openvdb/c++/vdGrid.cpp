/*
 * vdGrid — OpenVDB ボリューム幾何の実装 (#3434 P2)。設計の背景はヘッダ冒頭を参照。
 */
#include	"vd/c++/vdGrid.h"
#include	"common/blockframe.h"   /* ★ #3507: ブロック分割フレーミング */
#include	"pig/c++/pigModuleRegistry.h"   /* モジュール専用データの預かり所 (static を置かないため) */
#include	"ts2/c++/stdString.h"

#include	<openvdb/io/Stream.h>
#include	<openvdb/tools/Composite.h>        /* csgUnionCopy / csgIntersectionCopy / csgDifferenceCopy */
#include	<cmath>
/* ★ #3489: LevelSetMeasure.h / LevelSetRebuild.h はもう要らない。計測は符号つきボクセル
 *   積分に置き換えた (volume() / op_area() の上のコメントに理由)。renormalize は
 *   vdaRenormalize.cpp が自前で levelSetRebuild を呼ぶ。 */

#include	"vd/c++/vdArena.h"   /* ★ #3441: 予算の保持だけ。arena を張るのは各 compute() 側 */

#include	<stdio.h>
#include	<stdlib.h>   /* getenv / atoi */
#include	<string.h>
#include	<sstream>
#include	<string>
#include	<vector>
#include	<openvdb/tools/LevelSetSphere.h>
#include	<openvdb/tools/MeshToVolume.h>
#include	<openvdb/tools/GridTransformer.h>

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

/* ---- ★ #3462 プリミティブ ----
 * OpenVDB 本体の生成器をそのまま使う。**メッシュを経由しない**ので、
 * manifold で球を作って voxelize する従来の経路より 1 段短い。
 * ⚠ どちらも **原点中心**。他カーネルの box / sphere と揃えてある。 */
sPtr<vdGrid>
vdGrid::make_sphere(double r, double dx, const pigBreak *brk)
{
	ensure_init();
	/* ★ #3498: createLevelSetSphere は interrupter を取る (LevelSetSphere.h に 1 箇所)。
	 *   ⚠ box (LevelSetPlatonic.h) には中断点が **無い**ので、そちらは渡す先が無い。 */
	vdBreakScope br(brk);
	openvdb::FloatGrid::Ptr g = openvdb::tools::createLevelSetSphere<openvdb::FloatGrid>(
	    (float)r, openvdb::Vec3f(0.0f, 0.0f, 0.0f), (float)dx,
	    (float)openvdb::LEVEL_SET_HALF_WIDTH, br.ptr());
	if ( ! g ) return sPtr<vdGrid>();
	sPtr<vdGrid> out = thNEW(vdGrid,());
	out->set_grid(g);
	return out;
}

sPtr<vdGrid>
vdGrid::make_box(double w, double h, double d, double dx)
{
	ensure_init();
	/* ★ OpenVDB は一般の直方体の生成器を持たない (createLevelSetCube は立方体のみ) ので、
	 *   8 頂点 12 三角形を組んで meshToLevelSet へ渡す。voxelize と同じ経路であり、
	 *   **メッシュカーネルには依存しない** (頂点をこの場で作るだけ)。 */
	/* ★ 2026-09-05 (#3486 で発覚): 置き場所は **角が原点** ([0,0,0]〜[w,h,d])。
	 *   規約は docs/srava_function_reference.md が言語の契約として書いており、
	 *   cgal / manifold / nef / geogram / cherchi / occt の 6 本はこれに揃っている
	 *   (occt は #3474 で原点中心から直した)。openvdb だけ原点中心のまま残っていた。
	 *   ⚠ kernel_agree の box_cut は openvdb を含まない (ボクセル近似なので体積の
	 *     突き合わせに入れられない) ので、この食い違いは誰も見張っていなかった。
	 *     ⇒ 位置は test/srava_affine.sh の place が probe 箱で見る。 */
	const float x = (float)w, y = (float)h, z = (float)d;
	std::vector<openvdb::Vec3s> pts = {
	    {0,0,0}, { x,0,0}, { x, y,0}, {0, y,0},
	    {0,0, z}, { x,0, z}, { x, y, z}, {0, y, z}
	};
	std::vector<openvdb::Vec3I> tri = {
	    {0,2,1},{0,3,2},  {4,5,6},{4,6,7},   /* -z / +z */
	    {0,1,5},{0,5,4},  {1,2,6},{1,6,5},   /* -y / +x */
	    {2,3,7},{2,7,6},  {3,0,4},{3,4,7}    /* +y / -x */
	};
	openvdb::math::Transform::Ptr xform =
	    openvdb::math::Transform::createLinearTransform(dx);
	openvdb::FloatGrid::Ptr g =
	    openvdb::tools::meshToLevelSet<openvdb::FloatGrid>(*xform, pts, tri);
	if ( ! g ) return sPtr<vdGrid>();
	sPtr<vdGrid> out = thNEW(vdGrid,());
	out->set_grid(g);
	return out;
}

/* ---- ★ #3463 affine: 変換 op の共通の入口 ----
 * 格子は不変のまま、ボクセルを動かす。手順:
 *   ① 入力の transform だけを「元の map ∘ 目的の world 変換」に差し替えた一時グリッドを
 *      作る (木は共有・O(1)。deepCopy ではないので中身は複製しない)
 *   ② 出力を **元の (正規の) 格子**で用意する
 *   ③ resampleToMatch で焼き直す
 * ⇒ transform の差し替えは①の中だけで完結し、外に出ない。csgUnion の前提 (transform 一致)
 *   も voxel_size() の等方前提も破れない。
 *
 * ★ 実測で確認済み (r=1.5・dx=0.02・levelSetVolume で検証):
 *     translate(3,0,0)  誤差 0.00%   scale 2 等方     誤差 0.01%
 *     scale [2,1,1]     誤差 0.00%   scale [0.2,1,1]  誤差 0.05%
 *   非等方でも縮小でも正しく rebuild される (narrow band が薄くなる懸念は
 *   resampleToMatch が halfWidth を出力側から決め直すので吸収される)。 */
sPtr<vdGrid>
vdGrid::op_affine(const double e[12], const pigBreak *brk)
{
	if ( ! g_ ) return sPtr<vdGrid>();
	/* 目的の world 変換 M (行優先 3x4) を Mat4R へ。⚠ OpenVDB は**行ベクトル規約**
	 * (applyMap(in) = in * mMatrix) なので、転置して入れる。 */
	openvdb::Mat4R M = openvdb::Mat4R::identity();
	for ( int i = 0 ; i < 3 ; ++i )
		for ( int j = 0 ; j < 3 ; ++j )
			M[j][i] = e[i*4+j];
	M.setTranslation(openvdb::Vec3R(e[3], e[7], e[11]));

	const openvdb::math::Transform &base = g_->transform();
	if ( ! base.isLinear() ) return sPtr<vdGrid>();   /* 非線形 map は対象外 */
	openvdb::Mat4R B = base.baseMap()->getAffineMap()->getMat4();

	openvdb::FloatGrid::Ptr tmp = g_->copy();          /* 木は共有 (shallow) */
	tmp->setTransform(openvdb::math::Transform::createLinearTransform(B * M));

	openvdb::FloatGrid::Ptr res = openvdb::FloatGrid::create(g_->background());
	res->setTransform(base.copy());                    /* ★ 元の格子のまま */
	res->setGridClass(g_->getGridClass());
	/* ★ #3498: resampleToMatch は interrupter を取る (GridTransformer.h に 1 箇所)。 */
	vdBreakScope br(brk);
	openvdb::tools::resampleToMatch<openvdb::tools::BoxSampler>(*tmp, *res, br.ref());

	sPtr<vdGrid> out = thNEW(vdGrid,());
	out->set_grid(res);
	return out;
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

/* ---- 素性を訊く op (#3487) — 格子から直に出す ---------------------------------- */
int
vdGrid::op_bbox(double mn[3], double mx[3]) const
{
	mn[0] = mn[1] = mn[2] = mx[0] = mx[1] = mx[2] = 0.0;
	if ( ! g_ || g_->activeVoxelCount() == 0 ) return 3;
	/* ★ 活性ボクセルの箱をそのまま使うと **狭帯域の厚み**ぶん膨らむ (既定 3 ボクセル =
	 *   dx の 3 倍)。零等値面をまたぐボクセル (|値| <= dx) だけを見れば dx の精度で済む。 */
	const double dx = voxel_size();
	bool first = true;
	for ( openvdb::FloatGrid::ValueOnCIter it = g_->cbeginValueOn() ; it ; ++it ) {
		if ( std::fabs((double)*it) > dx ) continue;
		openvdb::CoordBBox bb;
		if ( it.isVoxelValue() ) bb = openvdb::CoordBBox(it.getCoord(), it.getCoord());
		else                     it.getBoundingBox(bb);
		/* ★ ボクセルの **中心**で見る。±0.5 ボクセルぶん膨らませると誤差が 1.5dx になるが、
		 *   中心なら「真の面から dx 以内」に収まる (|値| <= dx で選んでいるため)。 */
		openvdb::Vec3d lo = g_->indexToWorld(openvdb::Vec3d(bb.min().x(), bb.min().y(), bb.min().z()));
		openvdb::Vec3d hi = g_->indexToWorld(openvdb::Vec3d(bb.max().x(), bb.max().y(), bb.max().z()));
		for ( int k = 0 ; k < 3 ; ++k ) {
			double a = lo[k] < hi[k] ? lo[k] : hi[k];
			double b = lo[k] < hi[k] ? hi[k] : lo[k];
			if ( first ) { mn[k] = a; mx[k] = b; }
			else { if ( a < mn[k] ) mn[k] = a;  if ( b > mx[k] ) mx[k] = b; }
		}
		first = false;
	}
	return 3;
}

int
vdGrid::op_centroid(double c[3]) const
{
	c[0] = c[1] = c[2] = 0.0;
	if ( ! g_ ) return 3;
	/* ★ 内側 = 値が負。狭帯域の外の内部は **タイル**で持たれている (活性ではない) ので、
	 *   ValueOn では届かない。cbeginValueAll でタイルも一緒に走査し、
	 *   タイルは「中心 × 含むボクセル数」で重み付けする。 */
	double sx = 0, sy = 0, sz = 0, w = 0;
	for ( openvdb::FloatGrid::ValueAllCIter it = g_->tree().cbeginValueAll() ; it ; ++it ) {
		if ( (double)*it >= 0.0 ) continue;
		openvdb::CoordBBox bb;
		double n;
		if ( it.isVoxelValue() ) { bb = openvdb::CoordBBox(it.getCoord(), it.getCoord()); n = 1.0; }
		else { it.getBoundingBox(bb); n = (double)bb.volume(); }
		openvdb::Vec3d ctr(0.5*(bb.min().x()+bb.max().x()),
		                   0.5*(bb.min().y()+bb.max().y()),
		                   0.5*(bb.min().z()+bb.max().z()));
		openvdb::Vec3d wp = g_->indexToWorld(ctr);
		sx += n * wp.x(); sy += n * wp.y(); sz += n * wp.z(); w += n;
	}
	if ( w > 0.0 ) { c[0] = sx/w; c[1] = sy/w; c[2] = sz/w; }
	return 3;
}

/* ---- ★ #3489 (2026-09-06): 計測は **符号つきボクセル積分** -------------------
 *
 * ★★ 直したもの: 中空の殻 (sphere(1.5) --- sphere(1.0)) の体積が「詰まった球」になっていた。
 *
 * ★ 犯人は **測る直前に掛けていた levelSetRebuild** だった (当初 levelSetVolume を疑ったが
 *   実測で否定された)。切り分けの実測値 (dx=0.02・真値 V=9.948377 / A=40.840704):
 *
 *       csgDifference の格子そのまま  V=9.948079  A=40.841548   ← 正しい
 *       levelSetRebuild を通すと      V=14.136805 A=28.272725   ← 空洞が埋まる
 *       同じ格子を volumeToMesh →
 *              meshToLevelSet        V=14.135969 A=28.271071   ← 同じく埋まる
 *
 *   rebuild は volumeToMesh → meshToLevelSet の往復で、この **メッシュ → 距離場**の側が
 *   「外から届かない領域 = 内側」と塗るため、閉じた空洞を材料として埋めてしまう。
 *   voxelize (meshToLevelSet) が同じ値を出すのもこれと同じ理由で、格子の側の欠陥ではない。
 *
 * ★ ではなぜ rebuild を掛けていたか (#3440・2026-08-19): ブールや offset の結果は真の距離場
 *   ではなく (|grad| = 1 が崩れる)、|grad| = 1 を仮定する levelSetVolume が **偏り、しかも
 *   dx を細かくしても誤差が減らない** (offset(box,0.1) で +1.6e-3 に張り付く)。
 *   rebuild はその偏りを消すために入れていた。
 *
 * ★ 符号つきボクセル積分は **両方を同時に解く**:
 *
 *       volume = Σ H(-φ) dx³        H は平滑化した Heaviside
 *       area   = Σ δ(φ) |∇φ| dx³    δ は H の導関数 (共面積公式)
 *
 *   - 符号だけで決まるので **空洞は自然に差し引かれる** (rebuild が要らない)。
 *   - H は界面について反対称なので、|∇φ| ≠ 1 でも零等値面の位置しか効かない = **偏らない**。
 *     area 側は |∇φ| を掛けることで共面積公式がそのまま補正になる。
 *   - **メッシュを作らない**ので「測るのにメッシュを経由しない」という利点も保つ。
 *     rebuild (メッシュ往復) が消えるぶん **速くもなる**。
 *
 * ★ 平滑化の形と半幅は OpenVDB の LevelSetMeasure (DiracDelta(1.5)) に合わせてある。
 *   空洞の無い形では levelSetArea / levelSetVolume と 6〜7 桁一致する = これまでの
 *   union / intersection 系列の数値は動かない。
 *
 * ⚠⚠ 平滑化を掛けるのは **狭帯域 = 活性ボクセル**だけ。帯の外 (タイル・非活性ボクセル) は
 *   符号だけで決める。ここを分けずに全域へ H を掛けると、場が縮んだ格子 (|背景値| < eps) で
 *   **外側のタイルが「半分内側」に化けて総和が発散する** (実測で 14.14 が 54815 になった)。
 *
 * ⚠⚠⚠ **ゼロ交差ゲート** — 差が残す「φ = 0 の膜」を数えないこと。
 *   difference = max(φa, -φb) は、a と b が **面を共有している**ところに
 *   φ = |距離| という **0 に触れるが負にならない膜**を残す。
 *   例: box(2,2,2) --- translate(box(2,2,2),[1,0,0]) の共有面 y=0 (x∈[1,2]) では
 *   φa = -y・-φb = +y なので max = |y| ≥ 0。等値面 (符号の変化) は無いので isosurface は
 *   何も作らないが、**平滑化した H は φ=0 に 0.5 を返す**ため、この膜が丸ごと材料に化ける。
 *   実測 (dx=0.05・真値 4): 素の積分 4.2395 / levelSetVolume 4.2007 / 等値面メッシュ 3.9926。
 *   ★ levelSetVolume も同じ罠にかかっており、rebuild (メッシュ往復) が偶然それを消していた。
 *   ⇒ **半径 2 ボクセルの近傍に符号の変化が無いボクセルは平滑化しない** (符号だけで決める)。
 *     半径 2 なのは δ の台が 1.5 ボクセルあるため — 半径 1 では area が 15% 落ちる (実測)。
 *     負のボクセルは自分が負なので必ずゲートを通る = **材料を取りこぼすことはない**。
 */
static const double VD_PI = 3.14159265358979323846;

/* 平滑化した Heaviside の内側側 H(-φ)。eps は φ の単位。DiracDelta の原始関数。 */
static inline double
vd_heaviside_in(double phi, double eps)
{
	if ( phi <= -eps ) return 1.0;
	if ( phi >=  eps ) return 0.0;
	return 0.5*(1.0 - phi/eps) - ::sin(VD_PI*phi/eps)/(2.0*VD_PI);
}

/* 平滑化した Dirac δ。OpenVDB tools/LevelSetMeasure.h の DiracDelta と同じ形。 */
static inline double
vd_dirac(double phi, double eps)
{
	if ( phi <= -eps || phi >= eps ) return 0.0;
	return (0.5/eps)*(1.0 + ::cos(VD_PI*phi/eps));
}

/* volume と area を **1 回の走査**で出す。want_area が 0 なら勾配を取らない。
 * ★ 逐次に足す = 実行ごとに同じ値 (cold と warm で volume が動かないことは renorm が見張る)。 */
/* ★ #3498: brk が非 0 なら走査中に中断を見る。中断したら *aborted に 1 を立てて途中で戻る。
 * ⚠ **途中までの総和を答えとして返さないこと**。見た目は普通の数値なので、通すと中断が
 *   「小さめの正しい答え」として焼き付く。呼び手は必ず *aborted を見て vd_abort_err を返す。 */
static void
vd_measure(const openvdb::FloatGrid &g, int want_area, double *vol, double *area,
           const pigBreak *brk = 0, int *aborted = 0)
{
	vdBreakPoll poll(brk);
	if ( aborted ) *aborted = 0;
	const double dx  = g.transform().voxelSize()[0];
	const double eps = 1.5 * dx;   /* LevelSetMeasure と同じ半幅 (3 ボクセル幅) */
	double sv = 0.0, sa = 0.0;
	openvdb::FloatGrid::ConstAccessor acc = g.getConstAccessor();
	for ( openvdb::FloatGrid::ValueAllCIter it = g.tree().cbeginValueAll() ; it ; ++it ) {
		if ( poll.cancelled() ) { if ( aborted ) *aborted = 1; return; }
		const double phi = (double)*it;
		if ( ! it.isVoxelValue() ) {          /* タイル = 帯の外。符号だけ */
			if ( phi < 0.0 ) {
				openvdb::CoordBBox bb;
				it.getBoundingBox(bb);
				sv += (double)bb.volume();
			}
			continue;
		}
		if ( ! it.isValueOn() ) {             /* 非活性ボクセルも帯の外 */
			if ( phi < 0.0 ) sv += 1.0;
			continue;
		}
		/* ★ 近傍 (±1・±2 ボクセル) を集める。ゲートの判定と勾配の両方に使う。 */
		const openvdb::Coord c = it.getCoord();
		double nb[3][2];              /* [軸][+/-] の ±1 (勾配用) */
		double mn = phi, mx = phi;
		for ( int r = 1 ; r <= 2 ; ++r )
			for ( int k = 0 ; k < 3 ; ++k ) {
				openvdb::Coord p = c, m = c;
				p[k] += r; m[k] -= r;
				const double vp = (double)acc.getValue(p);
				const double vm = (double)acc.getValue(m);
				if ( r == 1 ) { nb[k][0] = vp; nb[k][1] = vm; }
				if ( vp < mn ) mn = vp;   if ( vp > mx ) mx = vp;
				if ( vm < mn ) mn = vm;   if ( vm > mx ) mx = vm;
			}
		if ( ! (mn < 0.0 && mx >= 0.0) ) {   /* 等値面が近傍に無い = 膜。符号だけで決める */
			if ( mx < 0.0 ) sv += 1.0;
			continue;
		}
		sv += vd_heaviside_in(phi, eps);
		if ( ! want_area ) continue;
		const double d = vd_dirac(phi, eps);
		if ( d == 0.0 ) continue;
		/* ★ 中心差分。δ が消える |φ| >= 1.5dx の外は見ないので、隣は必ず帯の中 (幅 3dx)。 */
		double gr[3];
		for ( int k = 0 ; k < 3 ; ++k )
			gr[k] = (nb[k][0] - nb[k][1]) / (2.0*dx);
		sa += d * ::sqrt(gr[0]*gr[0] + gr[1]*gr[1] + gr[2]*gr[2]);
	}
	const double dv = dx*dx*dx;
	if ( vol  ) *vol  = sv * dv;
	if ( area ) *area = sa * dv;
}

double
vdGrid::op_area(const pigBreak *brk) const
{
	if ( ! g_ || g_->activeVoxelCount() == 0 ) return 0.0;   /* 空は 0 (throw させない) */
	double a = 0.0;
	/* ★ #3498: 中断されたら a は途中までの総和。呼び手が vd_abort_err で弾く約束
	 *   (ここで 0 を返して「空」に化けさせない — 0 は空という *答え* なので嘘が濃くなる)。 */
	vd_measure(*g_, /*want_area=*/1, 0, &a, brk);
	return a;
}

int
vdGrid::op_valid() const
{
	/* ★ 共通定義の ②③ (閉じている / 自己交差が無い) は距離場では恒真なので ① だけ見る。 */
	return ( g_ && g_->activeVoxelCount() > 0 ) ? 1 : 0;
}


double
vdGrid::volume(const pigBreak *brk) const
{
	if ( ! g_ ) return 0.0;
	/* ★ #3474 続き (2026-09-05): **活性ボクセルが 1 つも無い格子 = 空集合**は体積 0。
	 *   ⚠ ここで先に返さないと OpenVDB が
	 *     "LevelSetMeasure does not support empty grids" を **throw** し、
	 *     ワーカースレッドから漏れて agent ごと死ぬ (geogram で踏んだのと同じ形)。
	 *   空は empty3d() だけでなく、差で丸ごと消えた中間結果としても普通に出る。 */
	if ( g_->activeVoxelCount() == 0 ) return 0.0;
	/* ★ level set の体積は等値面が囲む世界座標系の体積。**メッシュを作らずに**出せるのが
	 *   ボリューム表現の利点 (メッシュ系は面を積む)。解像度に依存する近似値なので、
	 *   メッシュ系との一致は「相対誤差」で見る (bit 一致は要求しない)。
	 * ★ #3489: 中身は **符号つきボクセル積分** (理由と切り分けの実測は op_area の上)。
	 *   ⚠ 印 (is_normalized) はもう見ない。積分は |grad| = 1 を仮定しないので分ける必要が
	 *     無く、しかも分岐先で掛けていた levelSetRebuild こそが **空洞を埋めていた**。 */
	double v = 0.0;
	vd_measure(*g_, /*want_area=*/0, &v, 0, brk);   /* ★ #3498: 中断時の扱いは op_area と同じ */
	return v;
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
 *   [u32 blocklen][block] … [u32 0]   ブロックを繋ぐと **素の .vdb** (ネイティブ)
 *
 * ★ #3507 (2026-09-10): 旧形式は先頭に全長を置く [u64 nbytes][bytes] だった。長さは書き
 *   終えるまで分からないので、その形式が **全文を一度作ること自体を強制していた**。
 *   ブロック長は「そのブロックを埋めた時点で」分かるので、一時領域は固定 1 MiB で済む。
 *   (長さ接頭辞を選んだ元の理由は「chunk Source に *残り全部* を取る手段が無い」ことだったが、
 *    終端を長さ 0 のブロックで自己記述すれば pull() だけで足りる。)
 *
 * ⚠ **ここに書いてあった旧説明は誤りだった** (2026-09-10・#3507 で訂正)。
 *   「openvdb::io::Archive が seek するので stringstream が要る」と書いていたが、seek するのは
 *   @io::File@ の方で、ここで使っている @io::Stream@ は
 *     @Archive::write(os, grids, seekable=false, metadata)@   (openvdb/io/Stream.cc:217)
 *   と **決め打ちで seekable=false を渡す**。@Archive.cc@ の @seekp@ / @tellp@ はすべて
 *   @if (seekable)@ の中にあり、この経路では 1 度も実行されない。ヘッダにも
 *   @hasGridOffsets = 0@ と書かれるので**読み側も逐次**である。
 *   ⇒ したがって *全文バッファは原理的に不要*で、sink へ直接流せる。#3507 で外した。 */
void
vdGrid::encode(vdChunkSink &sink)
{
	ensure_init();
	/* ★★ #3507 (2026-09-10): grid を **ブロック分割**で直接流す (全文バッファを作らない)。
	 *   ★ @io::Stream@ は @Archive::write@ へ seekable=false を決め打ちで渡すので
	 *     **書きは seek しない** (seek するのは @io::File@ の方。Archive.cc の seek は
	 *     すべて @if (seekable)@ の中)。旧コメントの「Archive が seek する」は誤りだった。 */
	blockframe::obuf<vdChunkSink> ob(sink);
	std::ostream                  os(&ob);
	openvdb::GridCPtrVec grids;
	if ( g_ ) grids.push_back(g_);
	openvdb::io::Stream(os).write(grids);
	os.flush();
	ob.finish();
}

void
vdGrid::decode(vdChunkSource &src)
{
	ensure_init();
	/* ★ #3507: ブロック列を逐次に読む (全文バッファを作らない)。
	 *   ★ @io::Stream@ の読みは @hasGridOffsets=0@ なので **前へしか進まない** = seek 不要。 */
	blockframe::ibuf<vdChunkSource> ib(src);
	std::istream                    is(&ib);
	openvdb::io::Stream strm(is, /*delayLoad=*/false);
	openvdb::GridPtrVecPtr grids = strm.getGrids();
	if ( ! grids || grids->empty() ) { set_decode_err("the stored VDB file contains no grid"); return; }
	g_ = openvdb::gridPtrCast<openvdb::FloatGrid>((*grids)[0]);
	if ( ! g_ ) set_decode_err("the stored VDB grid is not a FloatGrid; openvdb only handles FloatGrid");
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
		/* ★ #3462/#3463 (ひさ指示 2026-08-31): **transform 全体**を比べる。
		 *   voxel_size() は map の線形部の X 成分しか見ないので、平行移動・回転・非等方の
		 *   食い違いを素通りさせる。⚠ tools::csgUnion は**ツリーだけを見て transform を
		 *   参照しない**ので、素通りさせると index 空間で重ね合わせて幾何的に誤った結果を
		 *   静かに返す。いまは voxelize が等方 map しか作らないので dx の比較で足りているが、
		 *   一般の transform が入ってくることを見越して**最初から全体で比べる**。
		 *   ★ 一部でも異なればエラー (全項を先頭と突き合わせる)。 */
		if ( ! ops[i]->grid() ) { *errmsg = "needs openvdb grids"; return sPtr<vdGrid>(); }
		if ( i == 0 ) { d0 = ops[0]->voxel_size(); continue; }
		const openvdb::math::Transform &t0 = ops[0]->grid()->transform();
		const openvdb::math::Transform &ti = ops[i]->grid()->transform();
		if ( ! (t0 == ti) ) {
			double d = ops[i]->voxel_size();
			if ( d != d0 )
				::snprintf(errbuf, (size_t)errbufsz,
				    "operand %d has a different voxel size (%.17g vs %.17g) — "
				    "give every operand the same dx", i, d, d0);
			else
				::snprintf(errbuf, (size_t)errbufsz,
				    "operand %d has a different transform (%s vs %s at the same voxel size) — "
				    "grids must live on the same lattice to be combined",
				    i, ti.mapType().c_str(), t0.mapType().c_str());
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
