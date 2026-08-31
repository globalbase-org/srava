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
 *   キャッシュの弁別は hash_salt (GG_SALT) が担うので、共有しても衝突しない。
 */
#include	"pig/c++/pigData.h"
#include	"pig/c++/pigOpEntry.h"   /* pigWireClass (配線先) */
#include	<geogram/mesh/mesh.h>
#include	<stdint.h>

#define GG_MODULE_NAME	"geogram"
#define GG_TYPE		"gg-mesh3d"
/* ★ wire 形式の 4CC。manifold と **同一レイアウトなので同じ 4CC を共有する** (上のコメント参照)。
 * 型 (GG_TYPE) と 4CC は 1:1 ではない — 型は codec 行の types 申告が唯一の根拠。 */
#define GG_TAG		"MFM3"
#define GG_SALT		"\x01" "GGM"

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
protected:
	int  decodeErr_ = 0;
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

	/* ★ #3445: 自己交差した境界から**内外を決め直して**ソリッドにする。
	 *   geogram の MeshSurfaceIntersection (arrangement + radial sort) で交差を解き、
	 *   外側シェルだけを残す。cgal/manifold には無い能力。 */
	sPtr<ggMesh> op_solidify(char *err = 0, int errsz = 0);

	double volume() const;
	int    nverts() const { return (int)m_.vertices.nb(); }
	int    nfaces() const { return (int)m_.facets.nb(); }

	/* geogram のグローバル初期化 (プロセスに 1 回)。全 op の入口で呼ぶ。 */
	static void ensure_init();

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

#endif
