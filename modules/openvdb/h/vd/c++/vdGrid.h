#ifndef VD_GRID_H
#define VD_GRID_H
/*
 * vdGrid — OpenVDB (AcademySoftwareFoundation・Apache-2.0) の疎な階層グリッドを pigData で
 * ラップした値ハンドル (#3434 P2)。ggMesh (geogram) / mfMesh (Manifold) / nfMesh (CGAL Nef) の
 * ミラーだが、★**表現クラスが違う**。
 *
 *  - これは三角形メッシュでも B-rep でもない **第 3 の表現クラス**。空間を格子に切り、格子点に
 *    **符号付き距離 (SDF)** を持つ。★**表面はどこにも書かれておらず、値 0 の等値面として暗黙に
 *    定義される**。
 *  - ブールは **点ごとの min/max だけ**:
 *      union(f,g) = min(f,g) / intersection(f,g) = max(f,g) / difference(f,g) = max(f,-g)
 *    ★**位相の場合分けが存在しない**ので、自己交差・非多様体・汚い入力でも必ず答えが出る
 *    (退化という概念が無い)。メッシュ系が交線と場合分けで苦しむ所 (#3445 の自己交差 tube で
 *    cgal は素通りして誤値・manifold も同じ誤値・nef は受け取れない) が、ここでは問題にならない。
 *  - 代償は **解像度が全て**であること。格子間隔以下の薄板や鋭いエッジは消え、平面は等値面抽出で
 *    階段状になり、メモリは体積オーダーで効く。
 *
 * ★★ 暗黙 cast を持たせない (設計条件・#3434):
 *   解像度は **情報損失を伴うパラメータ**なので、黙って変換してはいけない。出入りは明示 op だけ:
 *     voxelize(mesh, res)  … mesh → vd   (tools::meshToLevelSet)
 *     isosurface(vd, iso)  … vd   → mesh (tools::volumeToMesh)
 *   よって sig には他型の入力行を書かない (cast の (mf-mesh3d)->… のような行を作らない)。
 *
 * cache 形式 (D_META 4CC "VDB "):
 *   ★ **OpenVDB のネイティブシリアライズ** (openvdb::io::Stream) をそのまま wire にする。
 *   中立形式を自前で定義する案もあったが、**読み手が居ない**ので採らなかった:
 *   暗黙 cast が無い以上、vd のキャッシュを読むのは vd モジュール自身だけで、メッシュ系
 *   モジュールが読む場面が構造上発生しない (voxelize / isosurface はどちらも vd モジュールが
 *   実行し、外から見える入出力は mesh)。バックエンド比較も cache dir を分けて行うので
 *   形式互換は要らない (kernel_agree / configs.tsv の方式)。
 *   → 中立形式が買うのは効率ではなく「モジュール非依存」で、その非依存に用が無い。
 *   ネイティブなら **活性マスク圧縮・half-float・zip** がそのまま効く。
 *   (nef が NEF3(SNC) を wire に選んだのと同じ判断。あちらは表現力のため・こちらは効率のため。)
 */
#include	"pig/c++/pigData.h"
#include	"pig/c++/pigModuleError.h"   /* #3475: 自分の名前でエラーを作る */
#include	"pig/c++/pigOpEntry.h"   /* pigWireClass (配線先) */
#include	"vd/c++/vdBreak.h"       /* #3498: 中断 (どの op が止まるかは vdBreak.h の表) */
#include	<openvdb/openvdb.h>
#include	<stdint.h>
#include	<vector>

#define VD_MODULE_NAME	"openvdb"
/* ★ 型名。チケット #3434 の表記は "vd-grid" だが、既存の型はすべて次元接尾辞を持つ
 * (cg-mesh3d / cg-cross2d / mf-mesh3d / gg-mesh3d / nf-mesh3d / d2-shape2d …) ので
 * それに揃えた。2D グリッド (vd-grid2d) を足す余地も残る。 */
#define VD_TYPE		"vd-grid3d"
/* ★ wire 形式の 4CC = OpenVDB ネイティブ。4CC は **形式**の名前なので、将来 2 つ目の
 * ボリュームバックエンドが同じ .vdb を書くなら共有してよい (型は codec 行の types 申告が担う)。 */
#define VD_TAG		"VDB "
/* ★ 2026-08-29 (ひさ判断): 旧 VD_MESH_TYPE ("mf-mesh3d") / VD_MESH_TAG ("MFM3") と
 * クラス vdMesh を **撤去**した。#3434 で voxelize / isosurface が openvdb_mf/cg/gg へ移り、
 * このモジュールにメッシュを扱う op が 1 つも無くなっていたため (OPS 表に無い = 到達不能)。
 * メッシュとの変換は openvdb_mf/cg/gg が **本物の相手クラス** (mfGeom / cgMesh / ggGeom) で行う。 */

/* codec の Sink/Source 抽象 (ggChunkSink/mfChunkSink と同シグネチャ)。 */
struct vdChunkSink   { virtual void chunk(const uint8_t *data, int n) = 0; virtual ~vdChunkSink()   {} };
struct vdChunkSource { virtual void pull (uint8_t *dst, int n)        = 0;
                       virtual int  more()                            { return 1; }
                       virtual ~vdChunkSource() {} };

/* ---- 抽象基底: reader/writer が扱う多態ハンドル (今は 3D grid のみ) ---- */
class vdGeom : public pigDataWireTyped {
public:
	vdGeom(sPtr<pigInfo> i = thNULL) : pigDataWireTyped(i) {}
	virtual const char* meta_tag()  = 0;
	virtual uint16_t    repr_type() = 0;
	virtual int         dim()       = 0;
	virtual void encode(vdChunkSink&)   = 0;
	virtual void decode(vdChunkSource&) = 0;
	int  decode_failed() const { return decodeErr_; }
	/* ★ #3479: 立てた **理由** (立てていなければ 0)。reader がこれを拾って errCode と一緒に
	 *   parent へ渡す。従来は「読めなかった」という事実だけが残り、利用者に届く文は
	 *   「codec が無い / 表現できない / 形式が違う」の 3 択を並べた推測だった。
	 *   ★文字列リテラル前提 (寿命は .so と同じ)。 */
	const char* decode_why() const { return decodeWhy_; }
protected:
	/* decodeErr_ と理由は必ず対で立てる (理由の無い拒否を作らない)。 */
	void set_decode_err(const char* why) { decodeErr_ = 1; decodeWhy_ = why; }
	int  decodeErr_ = 0;
	const char* decodeWhy_ = 0;
public:
	virtual bool write_to(const char *path, const char *unit) = 0;
	/* reader 用ファクトリ: D_META タグから具体型を生成 (未知タグは null)。 */
	static sPtr<vdGeom> create_for_meta(const uint8_t *meta, int len);

	/* ★ 2026-08-28 (ABI v12): **この階層への配線先**。op の OPS 行が OPWIRE(Calc, vdGeom) と
	 *   書くと、引数はこの WIRE 経由で実体化される。create_for_meta が 4CC を受理判定し、
	 *   mkReader がこの階層の stream reader を起こす。定義は vdCacheCodec.cpp。 */
	static const pigWireClass WIRE;
};

/* ---- 3D 疎グリッド (OpenVDB の level set) ---- */
class vdGrid : public vdGeom {
public:
	vdGrid(sPtr<pigInfo> i = thNULL);

	openvdb::FloatGrid::Ptr&       grid()       { return g_; }
	const openvdb::FloatGrid::Ptr& grid() const { return g_; }
	void set_grid(openvdb::FloatGrid::Ptr g) { g_ = g; }

	virtual sPtr<stdString> get_str();

	virtual const char* meta_tag()  { return VD_TAG; }
	virtual const char* type_name() { return VD_TYPE; }
	virtual uint16_t    repr_type() { return 96; }   /* 疎な符号付き距離場 (mesh でも B-rep でもない) */
	virtual int         dim()       { return 3; }

	virtual void encode(vdChunkSink&   sink);
	virtual void decode(vdChunkSource& src);
	virtual bool write_to(const char *path, const char *unit);

	/* ---- ブール = 点ごとの min/max (tools/Composite.h)。★破壊的 API なので複製してから ---- */
	/* ★ #3436 P4: n 項ブール。openvdb の CSG は **格子点ごとの min/max** なので本来 n 項だが、
	 *   API は二項 (csgUnion 等) なので **agent の中で逐次に畳む**。
	 *   ⚠ geogram/occt の「1 回の交差計算」とは機構が違う — 効くのは
	 *     「中間結果を .vdb へ書き出して読み直す往復が消える」ところ。P4 の対比ではここを区別する。
	 *   全オペランドの voxel size が一致していることを最初にまとめて検査する。
	 *   失敗は null + *errmsg。 */
	static sPtr<vdGrid> bool_from_args(sArray<sPtr<pigData> > *args, const char *kind,
	                                   const char **errmsg, char *errbuf, int errbufsz);

	sPtr<vdGrid> op_union(sPtr<vdGrid> b);
	sPtr<vdGrid> op_intersection(sPtr<vdGrid> b);
	sPtr<vdGrid> op_difference(sPtr<vdGrid> b);

	/* ---- ★ 場が「真の符号付き距離場か」の印 (|grad| = 1 が保たれているか) ----
	 * ブール (min/max の合成) の結果は形は正しいが真の距離場ではなくなり、|grad| = 1 を
	 * 仮定する tools::levelSetVolume が偏る。
	 * ⚠ #3489 (2026-09-06) 以降、**volume() / op_area() はこの印を読まない**
	 *   (符号つきボクセル積分は |grad| = 1 を仮定しない)。印を残してあるのは
	 *   「この格子が真の距離場か」という事実そのものが .vdb の情報として意味を持ち、
	 *   renormalize が付け直す対象でもあるため。
	 * ★ **grid のメタデータに載せる**ので .vdb キャッシュを越える。載せないと cold と warm で
	 *   volume が変わる = 「答えが正しく見えたまま変わる」型の欠陥になる
	 *   (型スタンプで一度潰したのと同じ形の罠)。
	 * ★ 印が無い grid は **正規化済みとみなす** (voxelize / renormalize は必ず印を付けるので、
	 *   印が無いのは srava の外で作られた .vdb だけ)。 */
	void set_normalized(bool v);
	bool is_normalized() const;

	/* ---- 計測 ---- */
	/* ★★ #3489 (2026-09-06): **符号つきボクセル積分** に置き換えた。
	 *     volume = Σ H(-φ) dx³ / area = Σ δ(φ)|∇φ| dx³ (平滑化の半幅は 1.5 ボクセル)。
	 *   - 符号だけで決まるので **内部空洞が自然に差し引かれる**。中空の殻が「詰まった球」に
	 *     なっていた欠陥はここが原因だった (犯人は測る直前の levelSetRebuild — 詳細は
	 *     vdGrid.cpp の op_area 直上に切り分けの実測つきで書いてある)。
	 *   - H は界面について反対称なので **|grad| = 1 が崩れていても偏らない**。よって
	 *     「正規化されていなければ作り直してから測る」(2026-08-19) は不要になり、
	 *     is_normalized() は計測からは **読まれない** (印そのものは .vdb に残す)。
	 *   - メッシュ往復 (rebuild) が消えるので **速くもなった**。
	 *   ★ 解像度に依存する近似値であることは変わらない。桁が要るなら isosurface で
	 *     メッシュにしてから測る。これは別の話。 */
	double volume(const pigBreak *brk = 0) const;   /* 符号つきボクセル積分 (world 単位) */
	double voxel_size() const;      /* 格子間隔 (等方前提) */
	int    active_voxels() const;   /* 活性ボクセル数 (= 狭帯域の実サイズ) */

	/* ---- 素性を訊く op (#3487) ----------------------------------------------
	 * ★ **メッシュを作らずに**格子から出す (体積と同じ方針)。解像度に依存する近似値。
	 *   bbox … 零等値面をまたぐボクセルの世界座標での範囲。
	 *          ⚠ 活性ボクセルの箱をそのまま使うと **狭帯域の厚み (既定 3 ボクセル)** ぶん
	 *            膨らむので、符号が変わるボクセルだけを見る。
	 *   centroid … 内側 (値 < 0) のボクセル/タイルの重心。タイルは中心 × ボクセル数で重み付け。
	 *   area … tools::levelSetArea。
	 * ★ valid の共通定義 (① 空でない ∧ ② 閉じている ∧ ③ 自己交差が無い) のうち、
	 *   **②③ は距離場では構造的に恒真** (境界も自己交差も表現できない) なので ① だけを見る。
	 *   これは「別のことを答えている」のではなく「その表現では ②③ が恒真」という事実。 */
	int    op_bbox(double mn[3], double mx[3]) const;
	int    op_centroid(double c[3]) const;
	double op_area(const pigBreak *brk = 0) const;
	int    op_valid() const;

	/* OpenVDB のグローバル初期化 (プロセスに 1 回)。全 op の入口で呼ぶ。
	 * ★ GEO::initialize() と同じ性質で、これを呼ばずに触ると型レジストリが未登録で落ちる。 */
	static void ensure_init();

	/* ★ #3419 (ABI v7): op 内並列 (TBB) のスレッド予算を張り替える。
	 * planner から C_ENV で届いた L_THR を vdtsAgent::on_env が渡す。
	 * n <= 0 は「指定なし」= TBB の既定 (コア数) に戻す。
	 * 起動時の既定は env SRAVA_OP_THREADS (測定用のつまみ・vdGrid.cpp 冒頭を参照)。 */
	/* ★ **モジュール別**の予算 (ひさ判断 2026-08-26)。openvdb / openvdb_mf / openvdb_cg /
	 * openvdb_gg は **別々のモジュール**なので、threads:N は設定されたモジュールの op にだけ効く。
	 * ⚠ 以前は libsrava_vd の static 1 個で 4 モジュールが共有していた (= 1 つ設定すると全部に
	 *   効いた)。実体を pigModuleRegistry のモジュール専用スロットへ移した際に分離した。 */
	static void set_thread_budget(int n);   /* ★ 相手は registry の configuring_module_id() */

	/* ★ #3462: プリミティブ。OpenVDB 本体の生成器をそのまま使う。
	 *   これが入るまで openvdb は leaf を作れず、mesh を manifold に作らせて voxelize していた。 */
	static sPtr<vdGrid> make_sphere(double r, double dx, const pigBreak *brk = 0);
	static sPtr<vdGrid> make_box(double w, double h, double d, double dx);

	/* ★ #3463: 一般アフィン変換 (行優先 3x4)。translate / rotate / scale / mirror /
	 *   transform の **共通の入口** (manifold の apply_affine・occt の op_affine と同じ形)。
	 *
	 *   ★★ 格子 (mTransform) は**動かさない**。動かすのは中身のボクセル。
	 *   mTransform は「そのグリッドが住んでいる格子の定義」= 値ではなく型に近いので、
	 *   それを動かすと他のグリッドと格子が揃わなくなる。⚠ tools::csgUnion は**ツリーだけを
	 *   見て transform を参照しない**ので、map が違うグリッドを渡すと止まらずに index 空間で
	 *   重ね合わせ、幾何的に誤った結果を静かに返す。
	 *
	 *   実装は tools::resampleToMatch。入力の transform だけを一時的に「元の map ∘ 目的の
	 *   world 変換」に差し替え、出力を**元の格子**で用意して呼ぶ。level set の面倒
	 *   (scale/shear で距離が保存されない件) は resampleToMatch が内部の
	 *   doLevelSetRebuild で見てくれる。 */
	sPtr<vdGrid> op_affine(const double e[12], const pigBreak *brk = 0);

	/* ★ #3441 (ABI v10): module("openvdb.so",{threads:N}) の受け口。記述子の .configure に配線。
	 *   n > 0 = op あたりの上限 / n <= 0 = 指定なし (TBB 既定へ戻す)。
	 *   ⚠ configure は **module() が実行されるたびに 1 回**呼ばれるだけで、op ごとには呼ばれない。
	 *     値は static に置き、各 op が計算の入口で vd_in_arena() 経由で読む。 */
	/* ⚠ 記述子の .configure は素の関数ポインタで **自分のモジュール名を知らない**。
	 * 各モジュールの tsAgent.cpp が自分の名前を渡す薄い thunk を用意して配線する
	 * (ABI を変えずにモジュール別を実現する形)。 */
	static void configure(sPtr<pigData> opts);

private:
	openvdb::FloatGrid::Ptr g_;
};


/* ★ #3475: このモジュール専用のエラー生成子。文言は "[TAG] <name>/op: message" になる。
 *   素の vda_err(...) を使うとモジュール名が付かない。 */
PIG_DEFINE_MODULE_ERR(vda_err, VD_MODULE_NAME)

/* ★ #3498: 中断で終わったならそのエラーを、そうでなければ thNULL。
 *
 * ⚠ **失敗を報告する前に必ずこれを見ること**。中断された算法は「作れなかった」(null) や
 *   途中までの値を返してくるので、そのまま報告すると *中断したのに格子が悪いと言う* ことになる。
 * ⚠⚠ 特に **計測 (volume / area) は途中までの総和を返す** — 見た目は普通の数値なので、
 *   これを通すと **中断が「小さめの答え」として成功扱いで焼き付く**。キャッシュに入れば
 *   次回以降それが正しい答えとして引かれる (#3489 の cache_version 上げ忘れと同じ形)。
 *   ⇒ 計測でも必ずここを通す。 */
static inline sPtr<pigData>
vd_abort_err(const pigBreak &b, const char *op)
{
	if ( ! b.cancelled() ) return sPtr<pigData>();
	char m[160];
	::snprintf(m, sizeof m, "%s: aborted (interrupted)", op ? op : "openvdb");
	return sPtr<pigData>(vda_err(m));
}

#endif
