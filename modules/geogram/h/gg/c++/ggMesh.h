#ifndef GG_MESH_H
#define GG_MESH_H
/*
 * ggGeom / ggMesh — geogram (Bruno Lévy・BSD-3) の GEO::Mesh を pigData でラップした値ハンドル
 * (#3435 P3)。mfMesh (Manifold) / nfMesh (CGAL Nef) のミラー。
 *
 *  - geogram のブールは **mesh arrangement + 厳密述語 / 厳密構成** (arXiv:2405.12949)。
 *    交差の座標は厳密に構成されるが、結果メッシュの頂点は **double に落として**保持される
 *    (CGAL EPECK のように有理数を持ち回らない)。よって:
 *      * cache の wire 形式は **raw double** = manifold の "MFM3" と同じ並びでよい
 *      * cgal (厳密境界 "MESH") との一致は「体積の相対誤差」で見る (bit 一致は要求しない)
 *  - 本命は多オペランド (variadic CSG) だが、そこは本体改修 (#3436 P4) が要るので、
 *    ここでは **二項ブールだけ**を入れる (#3435 の方針: モジュール投入と本体改修を分離)。
 *  - ★ geogram は「汚い入力をそのまま食える」= 自己交差した閉メッシュから内外を決め直せる。
 *    これは cgal (corefinement は素通り) / manifold (同じ誤値) / nef (受け取れない) の
 *    どれも持たない能力で、#3445 の solidify に対応する (受け入れ条件: 自己交差 tube = 48.61)。
 *
 * cache 形式 (D_META 4CC "MFM3"):
 *   [u32 nv][u32 nt] 頂点×nv(double x,y,z) 三角形×nt(u32 i,j,k)
 *   ★ 2026-08-19: 自前の 4CC ("GGM3") は**撤去**した。この並びは manifold の "MFM3" と
 *   **完全に同一**で、別の 4CC を名乗る理由が「形式が違う」ではなく「型を 4CC から引き直す
 *   実装があった」でしかなかったため (routing が型スタンプ一本になり、その必要が消えた)。
 *   4CC は **形式** の名前であって型の名前ではないので、同じ形式は同じ 4CC を名乗る。
 *   型の区別 (gg-mesh3d / mf-mesh3d) は codec 行の types 申告と型スタンプが担い、
 *   キャッシュの弁別はレジストリのソルト (モジュール名 + .so 指紋・#3466) が担うので、共有しても衝突しない。
 */
#include	"pig/c++/pigData.h"
#include	"pig/c++/pigModuleError.h"   /* #3475: 自分の名前でエラーを作る */
#include	"pig/c++/pigOpEntry.h"   /* pigWireClass (配線先) */
#include	<geogram/mesh/mesh.h>
#include	<stdint.h>
#include	<vector>

#define GG_MODULE_NAME	"geogram"
#define GG_TYPE		"gg-mesh3d"
/* ★ wire 形式の 4CC。manifold と **同一レイアウトなので同じ 4CC を共有する** (上のコメント参照)。
 * 型 (GG_TYPE) と 4CC は 1:1 ではない — 型は codec 行の types 申告が唯一の根拠。 */
#define GG_TAG		"MFM3"

/* codec の Sink/Source 抽象 (mfChunkSink/nfChunkSink と同シグネチャ)。 */
struct ggChunkSink   { virtual void chunk(const uint8_t *data, int n) = 0; virtual ~ggChunkSink()   {} };
struct ggChunkSource { virtual void pull (uint8_t *dst, int n)        = 0;
                       virtual int  more()                            { return 1; }
                       virtual ~ggChunkSource() {} };

/* ---- 抽象基底: reader/writer が扱う多態ハンドル (今は 3D のみ) ---- */
class ggGeom : public pigDataWireTyped {
public:
	ggGeom(sPtr<pigInfo> i = thNULL) : pigDataWireTyped(i) {}
	virtual const char* meta_tag()  = 0;
	virtual uint16_t    repr_type() = 0;
	virtual int         dim()       = 0;
	virtual void encode(ggChunkSink&)   = 0;
	virtual void decode(ggChunkSource&) = 0;
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
	static sPtr<ggGeom> create_for_meta(const uint8_t *meta, int len);

	/* ★ 2026-08-28 (ABI v12): **この階層への配線先**。op の OPS 行が OPWIRE(Calc, ggGeom) と
	 *   書くと、引数はこの WIRE 経由で実体化される。create_for_meta が 4CC を受理判定し、
	 *   mkReader がこの階層の stream reader を起こす。定義は ggCacheCodec.cpp。 */
	static const pigWireClass WIRE;
};

/* ---- 3D triangle mesh (geogram) ---- */
class ggMesh : public ggGeom {
public:
	ggMesh(sPtr<pigInfo> i = thNULL);

	GEO::Mesh&       mesh()       { return m_; }
	const GEO::Mesh& mesh() const { return m_; }

	virtual sPtr<stdString> get_str();

	virtual const char* meta_tag()  { return GG_TAG; }
	virtual const char* type_name() { return GG_TYPE; }
	virtual uint16_t    repr_type() { return 64; }   /* raw double 三角形メッシュ (MFM3 と同枠) */
	virtual int         dim()       { return 3; }

	virtual void encode(ggChunkSink&   sink);
	virtual void decode(ggChunkSource& src);
	virtual bool write_to(const char *path, const char *unit);

	/* ★ cg→gg 昇格読み: cgal の "MESH" (厳密有理数文字列) を double 化して読む。
	 *   パーサは src/h/common/exact_wire.h (manifold と共通) なので **CGAL 非依存**。
	 *   reader は create_for_meta が立てたこのフラグを見て decode の入口で分岐する。 */
	void	set_mesh_exact_input() { meshExactInput_ = 1; }
	void	decode_mesh_exact(ggChunkSource& src);

	/* ---- 組み立て (geodesic.h / 箱の生成器から使う) ---- */
	int  add_vertex(double x, double y, double z);
	void add_triangle(int a, int b, int c);

	/* ---- 二項ブール (結果は新しい ggMesh。失敗は null) ---- */
	/* ★ err/errsz を渡すと、geogram が例外で失敗したときその理由が書かれる (省略可)。
	 * ⚠ **モジュール側に static を置かない** (ひさ指示 2026-08-26)。in-proc 実行では 1 プロセスに
	 *   複数 op が同居しうるので、理由をモジュール大域に溜めると混線する。リエントラントに保つ。 */
	sPtr<ggMesh> op_union(sPtr<ggMesh> b, char *err = 0, int errsz = 0);
	sPtr<ggMesh> op_intersection(sPtr<ggMesh> b, char *err = 0, int errsz = 0);
	sPtr<ggMesh> op_difference(sPtr<ggMesh> b, char *err = 0, int errsz = 0);

	/* ---- アフィン変換 (行優先 double[12] = 3x4。cgMesh3D::apply_affine と同じ規約) ----
	 * 引数の解釈と行列の組み立ては common/affine.h (カーネル非依存)。ここは適用だけ。
	 * 座標は double なので全頂点を掛けるだけ。
	 * ⚠ 反射 (det<0) では面の向きが裏返るので **三角形の頂点順を入れ替える**。
	 *   geogram のブールは面の向きで内外を決めるので、直さないと裏返った立体
	 *   (体積が負・後段のブールが破綻) を黙って返す。 */
	sPtr<ggMesh> apply_affine(const double e[12]);

	/* ★ #3436 P4: **n 項ブール**。全オペランドを 1 つの mesh に集めて facet 属性 "operand_bit" で
	 *   区別し、arrangement を **1 回**だけ走らせて classify(式) で内外を決める。
	 *   二項を木に積むのと違い中間メッシュを作らない (= 中間キャッシュも無い)。
	 *   kind = "union" / "intersection" / "difference" (差は左 fold: x0-x1-x2-…)。
	 *   ⚠ operand_bit は 32 bit なので **オペランドは最大 32** (GG_MAX_OPERANDS)。 */
	static const int GG_MAX_OPERANDS = 32;
	static sPtr<ggMesh> op_bool_nary(sArray<sPtr<ggMesh> >& ops, const char *kind, char *err = 0, int errsz = 0);

	/* planner から届いた引数配列をそのまま食う入口 (ggaUnion/Intersection/Difference 共通)。
	 * 2 項は従来の二項 API へ (既存キャッシュを byte 不変に保つ)、3 項以上は op_bool_nary へ。
	 * 失敗時は null を返し *errmsg に理由を置く。 */
	/* ★ errbuf/errbufsz は **呼び手が用意する理由の受け皿** (static を置かないため)。
	 * 失敗時 *errmsg はそこを指すか、固定文言を指す。 */
	static sPtr<ggMesh> bool_from_args(sArray<sPtr<pigData> > *args, const char *kind,
	                                   const char **errmsg, char *errbuf = 0, int errbufsz = 0);

	/* ★ #3511: 凸包 hull(a[,b,…]) — **1 個以上**を受け、与えた形すべての凸包を返す。
	 *   geogram は @c GEO::compute_convex_hull_3d を持つ (中身は 3D Delaunay。header いわく
	 *   将来 QuickHull へ置き換え予定)。**入口は「頂点だけを入れた Mesh」** なので、
	 *   全オペランドの頂点を 1 つの Mesh へ写してから 1 回呼べばよい = n 項がそのまま通る。
	 *   ⚠ 面は見ない (arrangement もブールも通らない) ので、**閉じている必要も自己交差が
	 *     無い必要も無い**。かわりに穴も凹みも消える。
	 *   ⚠ 退化 (1 点 / 1 直線上 / 1 平面上) は立体にならない。面数を見て明示エラーにする。
	 *   失敗時は null を返し *errmsg に理由を置く (errbuf は呼び手が用意する受け皿)。 */
	static sPtr<ggMesh> hull_from_args(sArray<sPtr<pigData> > *args, const char **errmsg,
	                                   char *errbuf = 0, int errbufsz = 0);

	/* ★ #3445: 自己交差した境界から**内外を決め直して**ソリッドにする。
	 *   geogram の MeshSurfaceIntersection (arrangement + radial sort) で交差を解き、
	 *   外側シェルだけを残す。cgal/manifold には無い能力。 */
	sPtr<ggMesh> op_solidify(char *err = 0, int errsz = 0);

	double volume() const;

	/* ---- 素性を訊く op (#3487) ----------------------------------------------
	 * 中身は common/meshprops.h (カーネル非依存)。GEO::Mesh から素の配列へ写して渡す。
	 * valid の定義は 7 カーネル共通で ① 空でない ∧ ② 閉じている ∧ ③ 自己交差が無い
	 * (meshprops.h 冒頭に根拠つきで書いてある)。
	 * ⚠ **volume() と違って area() は絶対値ではない** — geogram の mesh_enclosed_volume は
	 *   fabs するが、面積は向きに依らないので素直に三角形の面積を足す。 */
	int    op_bbox(double mn[3], double mx[3]) const;
	int    op_centroid(double c[3]) const;
	double op_area() const;
	int    op_valid() const;
	/* ★ #3514: 位相を **直接**数える。返り 1 = 閉じている (0 なら genus は意味を持たない)。
	 *   ⚠ geogram の @mesh_nb_connected_components@ / @mesh_Xi@ は使っていない。欲しいのは
	 *     **シェルごとの**符号つき体積 (塊と空洞の区別) で、上流はメッシュ全体の値しか返さない
	 *     ⇒ 成分ごとの集計が要り、それは topology() が 1 パスでやっている。 */
	int    op_topology(int *nshells, int *nparts, int *genus) const;
	/* ★ #3514: **点との距離** — p から境界までの最短距離 (符号なし)。返り 1 = 出せた。
	 *   GEO::MeshFacetsAABB (三角形の内部まで含めた最近接) を使う。 */
	int    op_distance_at(const double p[3], double *out) const;
	int    nverts() const { return (int)m_.vertices.nb(); }
	int    nfaces() const { return (int)m_.facets.nb(); }

	/* geogram のグローバル初期化 (プロセスに 1 回)。全 op の入口で呼ぶ。 */
	static void ensure_init();

	/* ★★ #3535①: **点群の法線推定 (Co3Ne)**。中身は geogram なのに *メッシュではない* ので
	 *   ggMesh に置くのは一見ちぐはぐだが、置き場所を決めているのは「何を扱うか」ではなく
	 *   **どの .so に実体があるか**である。
	 *   ⚠⚠ geogram は @c GEO:: の file-scope な可変状態 (CmdLine の変数表・Process の
	 *     スレッド数) を持ち、静的リンクされた .so ごとに **その状態が別物**になる。
	 *     op 側 (geogram.so) から @c GEO::Co3Ne_* を直に呼ぶと、その参照は
	 *     *geogram.so 自身が抱え込んだコピー* に解決され、@c ggMesh::ensure_init() が
	 *     初期化した libsrava_gg 側のコピーとは別になる ⇒ @c CmdLine::desc_ が nullptr のまま
	 *     @c declare_arg に入って SIGSEGV した (2026-09-14・Linux 実測・#3535 4 節)。
	 *   ⇒ **GEO:: に直に触るのは libsrava_gg 側だけ**という規約を、申し送りではなく
	 *     *コードの位置* で守る。op はこの入口を呼ぶだけで GEO:: を一切見ない。
	 *   ⚠ 併せて CMake 側で geogram.so から geogram アーカイブを外してある
	 *     (LINK srava_gg のみ) ので、この規約を破ると **リンクが通らない** = 構造で止まる。
	 *
	 *   xyz  … 平坦な double 配列 (x,y,z の np 個)
	 *   k    … kNN の近傍数 (呼び手が 1..np-1 に丸めておくこと)
	 *   oxyz … 出力の点 (3*np 個。入力と同じ順・同じ数)
	 *   onrm … 出力の法線 (3*np 個・正規化済み)
	 *   返り 1 = 向き付けた / 0 = 推定はしたが向き付けていない / -1 = 失敗 (err に理由)
	 *   ⚠ 「点が減っていないこと」の検査もここで行う (減る実装と組んだら範囲外読みになる)。 */
	static int co3ne_normals(const double *xyz, int np, int k,
	                         std::vector<double> &oxyz, std::vector<double> &onrm,
	                         char *err, int errsz);

	/* ★ #3441 (ひさ設計 2026-08-26): module("geogram.so",{threads:N}) を受ける configure フック。
	 * geogram は GEO::initialize() 経由で **nproc をそのまま**スレッド数に使う (ensure_init 参照)。
	 * 実測で「一番速い arity が一番スレッド圧も高い」ことが分かっており (docs
	 * srava_load_control_design.md §17.6)、絞れば良いとは限らない — **既定は変えず、
	 * 明示指定したときだけ絞れる opt-in の口**として置く。
	 * opts は thNULL のことがある (threads キーが無ければ何もしない・冪等)。 */
	static void configure(sPtr<pigData> opts);

private:
	GEO::Mesh m_;
	int       meshExactInput_ = 0;   /* 1 = 入力が cgal の "MESH" (厳密境界) */
};

/* ★ #3475: このモジュール専用のエラー生成子。文言は "[TAG] <name>/op: message" になる。
 *   素の gga_err(...) を使うとモジュール名が付かない。 */
PIG_DEFINE_MODULE_ERR(gga_err, GG_MODULE_NAME)

#endif
