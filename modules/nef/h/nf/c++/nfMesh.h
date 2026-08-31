#ifndef NF_MESH_H
#define NF_MESH_H
/*
 * nfGeom / nfMesh — CGAL Nef_polyhedron_3(SNC)を pigData でラップした値ハンドル(#3433 P1)。
 * cgMesh(corefinement)/mfMesh(Manifold)のミラーだが、保持するのは **Nef 多面体そのもの**。
 *
 *  - Nef はブール演算に対して閉じており(定義に補集合を含む)、開閉の区別・低次元の破片・
 *    非有界を表せる。ブールは 2 つの SNC の overlay + mark の選択関数で、union/intersection/
 *    difference が同一アルゴリズム(選択関数だけが違う)。
 *  - ★ **Nef 型を維持したまま op 連鎖する**のがこのモジュールの要件 (#3433)。op ごとに
 *    Surface_mesh へ戻すと変換税で Nef 本来のコストが測れない。境界表現へ落とすのは
 *    volume / export / cache 書き出しの時だけ。
 *
 * cache 形式 (D_META 4CC "NEF3" / catalog §5.2 の repr_type=2 NEF_SNC 枠):
 *   本体は **CGAL の SNC シリアライズそのもの** ("Selective Nef Complex" テキスト)。
 *   [u32 len][len バイト] のフレーミングで D_CHUNK に載せる。
 *   ★EPECK でも**厳密に往復する**ことを実測で確認済み (2026-08-16):
 *     - 箱の補集合 (非有界) が非有界のまま復元され、原本と exact に等しい
 *     - 交線の頂点が汚い有理数になる 2 球 union でも symmetric difference が空・体積 17 桁一致
 *     - ⚠ @operator<<@/@>>@ の**定義は `CGAL/IO/Nef_polyhedron_iostream_3.h`** にある。
 *       これを include し忘れると「宣言だけあって未定義」でリンクエラーになる (一度踏んだ)
 *   → **非有界・低次元・非多様体という Nef 本来の表現力が cache を渡れる**。境界表現で書くと
 *     「箱の補集合」が「箱」に化けるので、そちらは採らない。
 *   ★代償: SNC テキストは冗長 (2 球 union で ~47KB)。これは Nef の正直なコスト。
 *
 *   読みでは "MESH" (cg の厳密境界) も受理する = **MESH → nf の昇格読み**。こちらは
 *   cgaMeshCodec で境界を読んで SNC を組み直す (cg が作った mesh を Nef として消費できる)。
 */
#include	"pig/c++/pigData.h"
#include	"pig/c++/pigOpEntry.h"   /* pigWireClass (配線先) */
#include	<CGAL/Exact_predicates_exact_constructions_kernel.h>
#include	<CGAL/Surface_mesh.h>
#include	<CGAL/Nef_polyhedron_3.h>
#include	<stdint.h>

/* ★ #3433: このソースは **2 つのモジュール**をビルドする (ひさ方針・混在使用は無いが A/B したい)。
 *     NF_WIRE_SNC    → nef_snc.so    : cache は常に SNC (Nef 本来の表現)
 *     NF_WIRE_HYBRID → nef_hybrid.so : 有界かつ 2-多様体なら厳密境界・それ以外は SNC
 *   ★型名・4CC・module 名・hash_salt を**全て分ける**。@pigTypeRegistry@ は「型名 1 エントリに
 *   tag 1 つ」= 型↔タグ 1:1 が構造上の不変条件なので、同じ型名で 2 つの 4CC は持てない
 *   (両方ロードすると後から登録した方で上書きされ、先のタグが引けなくなる)。
 *   salt も分けないと、同じ式のキャッシュキーが両モジュールで一致して別形式の cache を取り違える。
 *   ★消費側 (cgal / manifold) の codec は **4CC キー**なので型名を知らない = 波及しない。 */
#if defined(NF_WIRE_SNC)
#  define NF_MODULE_NAME "nef_snc"
#  define NF_TYPE        "nf-mesh3d"
#  define NF_TAG         "NEF3"
#  define NF_SALT        "\x01" "NEF"
#  define NF_OTHER_TAG   "NEFB"   /* もう一方 (hybrid) の 4CC — reader は両方受ける */
#elif defined(NF_WIRE_HYBRID)
#  define NF_MODULE_NAME "nef_hybrid"
#  define NF_TYPE        "nfb-mesh3d"
#  define NF_TAG         "NEFB"
#  define NF_SALT        "\x01" "NFB"
#  define NF_OTHER_TAG   "NEF3"   /* もう一方 (snc) の 4CC — reader は両方受ける */
#else
#  error "define NF_WIRE_SNC or NF_WIRE_HYBRID"
#endif

/* ★ payload の先頭 1 バイト = 形式 (#3433)。
 *   4CC を増やさずに 2 形式を運ぶための自己記述。型↔タグ 1:1 (pigTypeRegistry の不変条件) を守る。
 *   ★これは **cgal.so / manifold.so とも共有する安定契約**。値を変えたら 3 モジュールを揃えること
 *   (cgMesh3D::decode_nef3 / mfMesh::decode_nef3 が同じ定数を inline 再現している)。 */
enum {
	NF_FORM_SNC      = 0,   /* [u32 len][SNC テキスト]  — Nef 本来の表現 (非有界/低次元も運べる) */
	NF_FORM_BOUNDARY = 1    /* cgaMeshCodec の厳密境界   — cg の "MESH" と同一フレーミング */
};

/* codec の Sink/Source 抽象 (cgChunkSink/mfChunkSink と同シグネチャ)。 */
struct nfChunkSink   { virtual void chunk(const uint8_t *data, int n) = 0; virtual ~nfChunkSink()   {} };
struct nfChunkSource { virtual void pull (uint8_t *dst, int n)        = 0;
                       virtual int  more()                            { return 1; }
                       virtual ~nfChunkSource() {} };

/* ---- 抽象基底: reader/writer が扱う多態ハンドル (今は 3D のみ・2D は将来) ---- */
class nfGeom : public pigDataWireTyped {
public:
	nfGeom(sPtr<pigInfo> i = thNULL) : pigDataWireTyped(i) {}
	virtual const char* meta_tag()  = 0;   /* D_META 4 バイトタグ "NEF3" */
	virtual uint16_t    repr_type() = 0;   /* catalog §5.2: 2=NEF_SNC */
	virtual int         dim()       = 0;
	virtual void encode(nfChunkSink&)   = 0;
	virtual void decode(nfChunkSource&) = 0;
	virtual bool write_to(const char *path, const char *unit) = 0;
	/* reader 用ファクトリ: D_META タグから具体型を生成(未知タグは null)。
	 * "NEF3"(自型) と "MESH"(cg の厳密境界 = 昇格読み) を受理する。 */
	static sPtr<nfGeom> create_for_meta(const uint8_t *meta, int len);

	/* ★ 2026-08-28 (ABI v12): **この階層への配線先**。op の OPS 行が OPWIRE(Calc, nfGeom) と
	 *   書くと、引数はこの WIRE 経由で実体化される。create_for_meta が 4CC を受理判定し、
	 *   mkReader がこの階層の stream reader を起こす。定義は nfCacheCodec.cpp。 */
	static const pigWireClass WIRE;
};

/* ---- 3D Nef 多面体 ---- */
class nfMesh : public nfGeom {
public:
	typedef CGAL::Exact_predicates_exact_constructions_kernel	K;
	typedef K::Point_3						Point_3;
	typedef CGAL::Surface_mesh<Point_3>				Mesh;
	typedef CGAL::Nef_polyhedron_3<K>				Nef;

	nfMesh(sPtr<pigInfo> i = thNULL) : nfGeom(i), boundaryInput_(0), mfm3Input_(0), buildErr_(0) {}
	nfMesh(const Nef &n, sPtr<pigInfo> i = thNULL)
	    : nfGeom(i), n_(n), boundaryInput_(0), mfm3Input_(0), buildErr_(0) {}

	/* reader が "MESH" タグで作ったときに立てる: decode は SNC でなく厳密境界として読む。 */
	void set_boundary_input() { boundaryInput_ = 1; }
	/* reader が "MFM3" タグで作ったときに立てる: Manifold の raw-double mesh を読む。
	 * double は 2 進有理数なので EPECK への取り込みは**無損失** (cgMesh3D::decode_mfm3 と同じ理屈)。 */
	void set_mfm3_input() { mfm3Input_ = 1; }

	Nef&       nef()       { return n_; }
	const Nef& nef() const { return n_; }

	virtual sPtr<stdString> get_str();   /* 表示用 */

	virtual const char* meta_tag()  { return NF_TAG; }
	virtual const char* type_name() { return NF_TYPE; }   /* 実装型名 (4CC と 1:1) */
	virtual uint16_t    repr_type() { return 2; }             /* catalog §5.2 NEF_SNC */
	virtual int         dim()       { return 3; }

	virtual void encode(nfChunkSink&);
	virtual void decode(nfChunkSource&);
	virtual bool write_to(const char *path, const char *unit);

	/* ---- ブール: Nef のまま返す(型維持。ここで Surface_mesh へ戻さない) ---- */
	/* ★ #3436 P4: n 項ブール。CGAL の Nef 演算子は二項なので **agent の中で逐次に畳む**。
	 *   ⚠ geogram/occt の「n 個をまとめて 1 回の交差計算」とは機構が違う — 効くのは
	 *     「中間 SNC を wire へ直列化して読み直す往復が消える」ところ (SNC は重い)。
	 *   失敗は null + *errmsg。 */
	static sPtr<nfMesh> bool_from_args(sArray<sPtr<pigData> > *args, const char *kind,
	                                   const char **errmsg);

	sPtr<nfMesh> op_union       (sPtr<nfMesh> o);
	sPtr<nfMesh> op_intersection(sPtr<nfMesh> o);
	sPtr<nfMesh> op_difference  (sPtr<nfMesh> o);
	sPtr<nfMesh> op_complement  ();          /* ★Nef 固有: 結果は非有界になりうる */

	/* ---- Minkowski 和 A ⊕ B (#3440) ----
	 * ★Nef 固有。offset はこの特殊形 (球との和) なので、これがプリミティブ。
	 *   凸どうしなら頂点対の和の凸包で済むが、凹があると **両者を凸分解して m×n ペア**の和を
	 *   取って union するため重い (CGAL::minkowski_sum_3 が内部で凸分解する)。
	 * ★前提: **両者が有界**であること。CGAL は非有界を渡されると stderr に文言を出して
	 *   片方をそのまま返す (= 黙って嘘の答えになる) ので、呼び側で先に is_bounded() を見て
	 *   明示エラーにすること (nfaMinkowski がやっている)。 */
	sPtr<nfMesh> op_minkowski   (sPtr<nfMesh> o);

	/* ---- 3D オフセット (#3440 の 2: cgal.so から移設) ----
	 * @d>0@ 膨張 = A ⊕ (半径 d の球) / @d<0@ 収縮 = 補集合トリック
	 * (erode(A,r) = A − dilate(bbox−A, r))。球は icosahedron を @subdiv@ 回 Loop 細分して
	 * 球面へ投影した近似球 (subdiv 大 = 滑らかだが凸分解のペア数が増えて重い)。
	 * ★2D offset は straight skeleton で Nef と無関係なので **cgal.so に残る**。
	 * 非有界は @op_minkowski@ と同じく呼び側が弾く。 */
	sPtr<nfMesh> op_offset      (double d, int subdiv);

	/* ---- 有界性 ----
	 * ★SNC の**最初の volume は無限体積**。それが mark されている(= 選択されている)なら
	 *   この Nef は非有界。@is_simple()@ は「境界が 2-多様体か」であって**有界性ではない**
	 *   (箱の補集合は非有界だが is_simple() は真で、境界だけ書き出すと体積 8 の箱に化ける)。 */
	bool is_bounded() const;

	/* ★ 境界メッシュから Nef を作れなかった (自己交差など Nef の前提を満たさない入力)。
	 *   立てておいて **呼び側が明示エラー** にする。黙って空集合を返さない。 */
	int  build_failed() const { return buildErr_; }

	/* ---- 凸分解 (#3441) ----
	 * A を凸片へ分解した Nef を返す (@CGAL::convex_decomposition_3@)。返り値は **内部に仕切り面を
	 * 持つ Nef** = @is_simple()@ は偽になる。凸片は @to_mesh@ が volume ごとに取り出すので、
	 * @volume@ (合計 = 元と同じ) も @export@ (別々の連結成分として書く) も通る。
	 * ★片ごとに個別の値として取り出すには **mesh の配列**を返せる必要があり、今の op 表 (出力は
	 *   単一 cache か値) では書けない。当面は「1 つの mesh の中に凸片が別成分として入る」形にする。 */
	sPtr<nfMesh> op_convex_decomposition();

	/* ---- 塊 (part) の取り出し (#3441 追補・ひさ提案 2026-08-18) ----
	 * 「mesh の配列」を返す仕組みが無いので、**個数を返す op と n 番目を返す op** の 2 本にする。
	 *   @op_nparts()@ = 塊の数 = SNC の **marked volume の数** (無限体積は数えない)。
	 *   @op_part(i)@  = i 番目の塊 (0 始まり)。その volume の **全シェル** (外殻 + 空洞) から作るので
	 *                   空洞を持つ塊も正しく取り出せる。
	 * 例: @convex_decomposition(m)@ の結果に使えば凸片が 1 つずつ得られる。
	 *     普通の立体は 1・離れた 2 立体は 2・空洞つき立体は 1 (空洞は塊ではない)。 */
	int          op_nparts();
	sPtr<nfMesh> op_part(int i);

	/* ---- 内壁除去 (#3442) ----
	 * Nef の **正則化** (@closure(interior())@)。内部に残った仕切り面 (両側とも立体の facet) が
	 * 消える。外側の境界と**空洞の境界**は残る (片側が立体でないので本物の境界)。
	 * ★@repair@ とは別物で **体積が変わりうる** (低次元の破片も落ちる)。自動ではやらない。 */
	sPtr<nfMesh> op_unify_shells();

	/* ---- 壊れた境界からソリッドを組み直す (#3445) ----
	 * 面 1 枚ごとに Nef を作って n 項 union → **有界セルを mark** する (@Mark_bounded_volumes@)。
	 * 面どうしの交差線は union の過程で実エッジになる = **自己交差がここで解ける**
	 * (だから autorefine を前段に置く必要は無い。実測でも refine 有無で同じ答えだった)。
	 * ★@Mark_bounded_volumes@ は有界セルを**無差別に**塗るので、そのままだと空洞が埋まる。
	 *   @solidify_mesh@ が **連結成分ごとに**これを適用し、成分どうしを入れ子の深さで
	 *   合成することで空洞を救う。 */
	static sPtr<nfMesh> build_from_facets(Mesh &m);
	/* solidify の本体。入力は nf (自己交差したまま入っていてよい — Nef 構築は面どうしの交差を
	 * 検査しないので、壊れた形のまま SNC に入り @to_mesh()@ で面が取り出せる)。
	 * 連結成分ごとに build_from_facets し、成分どうしを入れ子の深さで合成する。 */
	static sPtr<nfMesh> solidify_mesh(sPtr<pigData> in);

	/* ---- 境界表現への変換 (volume / export / cache 書き出しの時だけ) ----
	 * 非有界 (complement の結果など) / 非 2-多様体は false。呼び側がエラーにする。 */
	bool to_mesh(Mesh &out);
	/* Surface_mesh から Nef を作る (leaf op / decode 共通)。 */
	void set_from_mesh(Mesh &m);

private:
	void	decode_boundary(nfChunkSource&);   /* "MESH" (cg の厳密境界) → SNC 構築 */
	void	decode_mfm3(nfChunkSource&);       /* "MFM3" (Manifold の raw double) → SNC 構築 */

	Nef	n_;
	int	boundaryInput_;
	int	buildErr_;
	int	mfm3Input_;
};

#endif
