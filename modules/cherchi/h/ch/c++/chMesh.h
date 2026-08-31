#ifndef CH_MESH_H
#define CH_MESH_H
/*
 * chGeom / chMesh — Cherchi らの "Interactive and Robust Mesh Booleans" (MIT・以下 IRMB) を
 * pigData でラップした値ハンドル (#3438 P6)。ggMesh (geogram) / mfMesh (Manifold) のミラー。
 *
 *  - IRMB のブールは **mesh arrangement + indirect predicates**。交点の座標を明示的に
 *    構成せず「どの 3 平面の交わりか」という *間接表現* のまま厳密述語を評価するので、
 *    有理数展開を避けたまま厳密に判定できる (SIGGRAPH Asia 2020 / TOG 2022)。
 *    結果メッシュの頂点は最後に **double へ落として**返る (geogram と同じ精度クラス) ので:
 *      * cache の wire 形式は **raw double** = manifold の "MFM3" と同じ並びでよい
 *      * cgal (厳密境界 "MESH") との一致は「体積の相対誤差」で見る (bit 一致は要求しない)
 *
 *  - ★★ **このカーネルは素で n 項** (#3436 P4 の対照相手として本命):
 *    `booleanPipeline(coords, tris, labels, op, …)` は **三角形ごとに入力メッシュ番号 (label)**
 *    を受け取り、arrangement を **1 回だけ**作ってから label の内外で分類する。
 *    二項ブールはその 2 ラベル版にすぎない。label は `std::bitset<NBIT>` (NBIT=32) なので
 *    **オペランドは最大 32** (CH_MAX_OPERANDS) = geogram の operand_bit と同じ上限。
 *
 *  - ⚠⚠⚠ **2026-08-27: 上流に out-of-bounds read がある** (valgrind で 2 箇所特定・
 *    arrangements/code/triangulation.cpp の findIntersectingElements)。読んだゴミで分岐が変わるので
 *    **同じ入力・同じプロセス・逐次実行でも実行のたびに違う値**が返る。
 *    ★★ 引き金は **退化した配置**で、**同じ形を軸に沿って平行移動する**と踏む
 *    (移動方向に平行な面がちょうど同一平面になるため)。上流自身の実行体で対照済み:
 *      軸に沿って 1.2 ずらした 2 球  → 8 回中 1 回 誤り
 *      一般の向きへずらした 2 球     → 8 回とも一致
 *      一般の位置の 8 球 (srava 経由) → 5 回とも安定・geogram/manifold と 1e-13 で一致
 *    ⇒ **ベンチのモデルは一般の位置で書く**。TBB / ASLR / こちらのフラグ / 入力の不正 /
 *      srava の wire は全部つぶした。既定 OFF・priority 3 のまま。
 *
 *  - ⚠⚠ **上流の限界: オペランドの配置が退化していると壊れる** (2026-08-26 に最小再現)。
 *    壊れ方は 2 通りあり、Release (NDEBUG) では上流の assert が効かないのでどちらも静かに進む。
 *
 *    ① **面でちょうど接する** (体積の重なりがちょうど 0) → **誤った値**。
 *       箱 [0,2]^3 と [2,4]×[0,2]^2 の union は 16 のはずが **18.6667** (共有壁が両側から残る)。
 *       ★ 隙間をごくわずかずらすとどちらも正しい = 限界は「測度 0 の接触」に局在。
 *       ⚠ この壊れ方は **境界辺を残さない**ので下の検査では捕まえられない → **既知の限界**。
 *
 *    ② **多重に重なる** (3 重以上が同じ領域に) → **arrangement 自体が壊れる**。
 *       幅 2 の箱や半径 1 の球を 0.7 刻みで 8 個 union すると、デバッグビルドでは
 *       triangulation.cpp:700 の assert が落ち、Release では **実行のたびに違う値**が返る。
 *       ⇒ ★ このとき結果には必ず **境界辺** が残るので、
 *          chMesh.cpp の ch_has_no_boundary() が捕まえて **エラー**にする。**黙って誤らない**。
 *
 *    ⚠ 入力側の要件 (manifold / watertight / 自己交差なし / 向き付き) は各オペランドとも
 *      満たしているので、これは入力の不正ではなく **オペランドどうしの配置**の問題。
 *    ⇒ ① このモジュールは **既定 routing に入れない** (priority 3)。CAD 的な使い方では
 *         「面で接する立体の和」は普通に出てくるので、既定にすると黙って誤る。
 *       ② ベンチのモデルは **一般の位置**で書く (接触ちょうどを踏むと、測っているのが
 *         カーネルの性能ではなく退化処理になる)。
 *    ★ **値が合うことと結果が正しいことは別**、の実例がここにある: 共通の kernel_agree モデル
 *      `box(2,2,2) &&& sphere(1.2,24)` は球の中心が箱の角にあり、cherchi は**開いた曲面**を作るが、
 *      落ちた面が原点を通る平面上にあって発散定理の寄与が 0 なので **体積だけは cgal と 1e-9 で
 *      一致していた**。境界辺検査を入れて初めて露出した (だから agree の表から intersection を外した)。
 *    テスト: test/srava_cherchi.sh の contact (① の可視化) と guard (② がエラーになること)。
 *
 *  - ⚠ **solidify (#3445) は持たない**。IRMB の分類は「他のラベルの内側か」で決まるため、
 *    自己交差した *1 枚の* メッシュは分類が空振りする (実測: 重なる 2 箱を 1 ラベルで
 *    union させると 15 でなく 16 が返る = 内側の面が落ちない)。#3438 の受け入れ条件は
 *    「arrangement + winding number だから汚い入力を食える」という前提だったが、
 *    **汚い入力を食えるのは arrangement までで、内外の決め直しは label 側の話**だった。
 *    連結成分ごとに label を振れば成分どうしの自己交差は解けるが、#3445 の tube は
 *    1 成分なので救えない。solidify は geogram / nef が持つ (梯子で解決済み)。
 *
 * cache 形式 (D_META 4CC "MFM3"):
 *   [u32 nv][u32 nt] 頂点×nv(double x,y,z) 三角形×nt(u32 i,j,k)
 *   ★ 4CC は **形式** の名前であって型の名前ではないので、manifold / geogram と共有する。
 *   型の区別 (ch-mesh3d) は codec 行の types 申告と型スタンプが担い、キャッシュの弁別は
 *   hash_salt (CH_SALT) が担うので、同じ 4CC でも衝突しない (型の出どころは sig と型スタンプ)。
 *
 * ★ IRMB のヘッダ (booleans.h) は **この .h からは include しない**。テンプレート地獄で
 *   **コンパイルが重い**ので、実体を触る chMesh.cpp だけが include する
 *   (op の TU 12 本が巻き込まれない)。ここで持つのは素の配列だけ。
 */
#include	"pig/c++/pigData.h"
#include	"pig/c++/pigOpEntry.h"   /* pigWireClass (配線先) */
#include	<stdint.h>
#include	<vector>

#define CH_MODULE_NAME	"cherchi"
#define CH_TYPE		"ch-mesh3d"
/* ★ wire 形式の 4CC。manifold / geogram と **同一レイアウトなので同じ 4CC を共有する**。 */
#define CH_TAG		"MFM3"
#define CH_SALT		"\x01" "CHM"

/* codec の Sink/Source 抽象 (mfChunkSink/ggChunkSink と同シグネチャ)。 */
struct chChunkSink   { virtual void chunk(const uint8_t *data, int n) = 0; virtual ~chChunkSink()   {} };
struct chChunkSource { virtual void pull (uint8_t *dst, int n)        = 0;
                       virtual int  more()                            { return 1; }
                       virtual ~chChunkSource() {} };

/* ---- 抽象基底: reader/writer が扱う多態ハンドル (今は 3D のみ) ---- */
class chGeom : public pigDataWireTyped {
public:
	chGeom(sPtr<pigInfo> i = thNULL) : pigDataWireTyped(i) {}
	virtual const char* meta_tag()  = 0;
	virtual uint16_t    repr_type() = 0;
	virtual int         dim()       = 0;
	virtual void encode(chChunkSink&)   = 0;
	virtual void decode(chChunkSource&) = 0;
	int  decode_failed() const { return decodeErr_; }
protected:
	int  decodeErr_ = 0;
public:
	virtual bool write_to(const char *path, const char *unit) = 0;
	/* reader 用ファクトリ: D_META タグから具体型を生成 (未知タグは null)。 */
	static sPtr<chGeom> create_for_meta(const uint8_t *meta, int len);

	/* ★ 2026-08-28 (ABI v12): **この階層への配線先**。op の OPS 行が OPWIRE(Calc, chGeom) と
	 *   書くと、引数はこの WIRE 経由で実体化される。create_for_meta が 4CC を受理判定し、
	 *   mkReader がこの階層の stream reader を起こす。定義は chCacheCodec.cpp。 */
	static const pigWireClass WIRE;
};

/* ---- 3D triangle mesh (IRMB は素の三角形ソウプで受け渡す) ---- */
class chMesh : public chGeom {
public:
	chMesh(sPtr<pigInfo> i = thNULL);

	virtual sPtr<stdString> get_str();

	virtual const char* meta_tag()  { return CH_TAG; }
	virtual const char* type_name() { return CH_TYPE; }
	virtual uint16_t    repr_type() { return 64; }   /* raw double 三角形メッシュ (MFM3 と同枠) */
	virtual int         dim()       { return 3; }

	virtual void encode(chChunkSink&   sink);
	virtual void decode(chChunkSource& src);
	virtual bool write_to(const char *path, const char *unit);

	/* ★ cg→ch 昇格読み: cgal の "MESH" (厳密有理数文字列) を double 化して読む。
	 *   パーサは src/h/common/exact_wire.h (manifold / geogram と共通) なので **CGAL 非依存**。 */
	void	set_mesh_exact_input() { meshExactInput_ = 1; }
	void	decode_mesh_exact(chChunkSource& src);

	/* ---- 組み立て (geodesic.h / 箱の生成器から使う) ---- */
	int  add_vertex(double x, double y, double z);
	void add_triangle(int a, int b, int c);

	/* ---- ブール ----
	 * ★ 二項も n 項も **同じ 1 本の道** (op_bool_nary) を通る。IRMB では二項が n 項の
	 *   特殊形でしかなく、二項専用の速い道が別に有るわけではないため
	 *   (geogram は二項 API が別に有るので分岐していた)。
	 *   kind = "union" / "intersection" / "difference" (difference は左 fold: x0-x1-x2-…)。
	 * ⚠ err/errsz は **呼び手が用意する理由の受け皿**。モジュール側に static を置かない
	 *   (in-proc では 1 プロセスに複数 op が同居しうるので混線する。ひさ指示 2026-08-26)。 */
	static const int CH_MAX_OPERANDS = 32;   /* = IRMB の NBIT */
	static sPtr<chMesh> op_bool_nary(sArray<sPtr<chMesh> >& ops, const char *kind,
	                                 char *err = 0, int errsz = 0);

	/* planner から届いた引数配列をそのまま食う入口 (chaUnion/Intersection/Difference 共通)。
	 * 失敗時は null を返し *errmsg に理由を置く (errbuf は理由の受け皿)。 */
	static sPtr<chMesh> bool_from_args(sArray<sPtr<pigData> > *args, const char *kind,
	                                   const char **errmsg, char *errbuf = 0, int errbufsz = 0);

	double volume() const;
	int    nverts() const { return (int)(coords_.size() / 3); }
	int    nfaces() const { return (int)(tris_.size()   / 3); }

	/* 素の配列 (translate / cast / 生成器から触る)。 */
	std::vector<double>&        coords()       { return coords_; }
	const std::vector<double>&  coords() const { return coords_; }
	std::vector<uint32_t>&      tris()         { return tris_; }
	const std::vector<uint32_t>& tris()  const { return tris_; }

private:
	std::vector<double>   coords_;   /* 3*nv */
	std::vector<uint32_t> tris_;     /* 3*nt */
	int                   meshExactInput_ = 0;   /* 1 = 入力が cgal の "MESH" (厳密境界) */
};

#endif
