#ifndef NF_MESH_H
#define NF_MESH_H
/*
 * nfGeom / nfNefMesh — CGAL Nef_polyhedron_3(SNC)を pigData でラップした値ハンドル(#3433 P1)。
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
 *   [u32 blocklen][block] … [u32 0] のブロック分割フレーミングで D_CHUNK に載せる (#3507)。
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
#include	<vector>
#include	"pig/c++/pigModuleError.h"   /* #3475: 自分の名前でエラーを作る */
#include	"pig/c++/pigOpEntry.h"   /* pigWireClass (配線先) */
/* ★★ #3545: **CGAL のヘッダをここから落とした**。1 枚 include しただけで
 *   CGAL の可変大域 (get_static_error_handler()::_error_handler 等) が
 *   その TU の .o に emit され、-fvisibility=hidden (#3499) で local に落ちて
 *   モジュールごとに別コピーになるため (実測 2026-09-16)。
 *   ⇒ CGAL の実体は不透明な nfNefBox が抱え、CGAL 型を取る API は
 *     **nf/c++/nfMeshCgal.h** (幾何 lib 側だけが読む) に置く。 */
class nfNefBox;
#include	<stdint.h>

/* ★★ #3559 (2026-09-19・ひさ裁定): **変種は「ライブラリ」ではなく「型」で分ける**。
 *   以前は同一ソースを 2 度ビルドして libsrava_nf_snc / libsrava_nf_hybrid の 2 本を建て、
 *   型名・4CC・module 名を -D (NF_WIRE_SNC / NF_WIRE_HYBRID) で焼き分けていた。
 *   ⚠ その形だと **上流 (CGAL) の実体が幾何ライブラリの本数だけ増える**。CGAL は header-only
 *     なので、可変大域 (get_default_random()::default_random 等) が各 .so に emit される。
 *     ELF は @u@ (STB_GNU_UNIQUE) をローダが 1 個に畳むので実害が出ないが、**PE には畳む機構が
 *     無い** (box で実測: 局所シンボル @d@ で 3 個)。⇒ 変種を増やすたびに上流が複製される。
 *   ⇒ 実装は 1 つ (nfNefMesh)、**wire の変種だけを派生型で分ける**:
 *
 *       nfGeom  (抽象・reader/writer が扱う顔)
 *         nfNefMesh  (実装のすべて。CGAL Nef を不透明な箱で抱える)
 *           nfMesh      … hybrid : 境界を取れるなら厳密境界で書く  型 nfb-mesh3d / 4CC NEFB
 *           nfMeshSnc   … snc    : 常に SNC だけを書く             型 nf-mesh3d  / 4CC NEF3
 *
 *   ★ **改名したのは SNC の側** (hybrid が @nfMesh@ の名を踏襲した)。将来 SNC を廃止する
 *     方針 (性能) なので、*削る側* を改名しておけば残る側の名前が動かない。
 *   ★★ 2 つは **兄弟**であって親子ではない。親子にすると @pig_wire_match@ (= d_cast) が
 *     片方の顔でもう片方に当たり、**nef_hybrid の writer が snc の値を NEFB で書く**経路が
 *     開く。⇒ 型で分ける以上、d_cast が 1:1 で割れる形にする。
 *   ★ 型名・4CC・module 名は引き続き**全て分ける**。@pigTypeRegistry@ は「型名 1 エントリに
 *     tag 1 つ」= 型↔タグ 1:1 が構造上の不変条件なので、同じ型名で 2 つの 4CC は持てない
 *     (両方ロードすると後から登録した方で上書きされ、先のタグが引けなくなる)。
 *     salt も分けないと、同じ式のキャッシュキーが両モジュールで一致して別形式の cache を取り違える。
 *   ★消費側 (cgal / manifold) の codec は **4CC キー**なので型名を知らない = 波及しない。
 *
 *   ---- -D はどこに残るか ----
 *   幾何ライブラリ (libsrava_cg) の TU は **-D なしで**この header を読み、2 型とも持つ。
 *   op のモジュール (nef_snc.so / nef_hybrid.so) は従来どおり同一ソースを 2 度ビルドするので、
 *   そちらだけが -D を持つ。-D が選ぶのは **名前と「自分の型はどちらか」だけ**で、
 *   *実装の分岐は 1 つも無い* (このファイルの末尾の NF_MESH / NF_TYPE / NF_TAG を見よ)。 */

/* ★ payload の先頭 1 バイト = 形式 (#3433)。
 *   4CC を増やさずに 2 形式を運ぶための自己記述。型↔タグ 1:1 (pigTypeRegistry の不変条件) を守る。
 *   ★これは **cgal.so / manifold.so とも共有する安定契約**。値を変えたら 3 モジュールを揃えること
 *   (cgMesh3D::decode_nef3 / mfMesh::decode_nef3 が同じ定数を inline 再現している)。 */
enum {
	NF_FORM_SNC      = 0,   /* [u32 blocklen][block]…[u32 0] の SNC テキスト (#3507)
	                         *   — Nef 本来の表現 (非有界/低次元も運べる)。
	                         *   ★ 旧形式は先頭に全長を置く [u32 len][SNC] だったが、それは
	                         *     書き手に全文を一度作らせる = 一時領域を強制していた。
	                         *     ブロック化で一時領域は固定 1 MiB・総量の上限も消えた。 */
	NF_FORM_BOUNDARY = 1,   /* cgaMeshCodec の厳密境界   — cg の "MESH" と同一フレーミング */
	/* ★ #3478: [SNC のブロック列][厳密境界] — SNC が先で、その後ろに境界を **足した**形。
	 *   ★★ #3499 (2026-09-07): **この形式を使うのは nef_hybrid だけ**になった。nef_snc が
	 *     これを使っていた理由 (下記) は、付録のために **encode ごとに to_mesh() が走る**
	 *     という実時間の代償に見合わなかったので撤回した。nef_snc は常に NF_FORM_SNC を書き、
	 *     他カーネルへ渡す口は橋モジュール nef_cg.so / nef_mf.so が持つ (cast のときだけ払う)。
	 *     hybrid にとってこの形式は今も必要 — 「2-多様体でない値でも境界を併記する」ための
	 *     もので、そちらは to_mesh() を元から毎回呼んでいるので追加の代償が無い。
	 *   以下は #3478 当時の設計理由 (hybrid にはそのまま当てはまる):
	 *
	 *   なぜ足すのか: cast("mf-mesh3d", x) は設計上 **目標型を産出できるモジュール (manifold) へ
	 *   振られ、異カーネル 4CC を読むのは行き先の reader** という分担になっている
	 *   ("op sig がディスパッチの唯一の真実")。ところが SNC のパースには CGAL が要り、
	 *   manifold.so は CGAL 非依存 (GPL 非汚染) を設計として守っているので、行き先に
	 *   reader を足すことができない。⇒ **産出側が読める形を供給する**しかない。
	 *
	 *   なぜ SNC が先か: 後続の境界を読み飛ばすには終わりが分かる必要がある。SNC は
	 *   ブロック列の終端 [u32 0] で自己記述しているが、境界形式は終わりを自己記述しない。
	 *   順序を逆にすると **nef 自身が自分の SNC に到達できなくなる**。
	 *
	 *   ★ 2026-09-06: **nef_hybrid もこの形式を使う**ようになった。あちらが SNC で書くのは
	 *   「有界でも 2-多様体でもない値」ではなく「**境界表現を取れない値**」であるべきで、
	 *   両者は一致しない — @to_mesh@ は 2-多様体でなくても marked volume ごとの全シェルから
	 *   境界を作れる (@volume@ / @export@ が通っているのはこの経路)。以前は is_simple() で
	 *   切っていたため、**境界を持てる値まで読めない形で書かれていた**
	 *   (3 球の XOR は 4 つの塊が稜で接するので is_simple()=false)。
	 *   ⚠ hybrid が境界 **だけ** で書けるのは 2-多様体のときに限る。境界だけを書き戻すと
	 *     内部の仕切り面が消え、@convex_decomposition@ の結果が 1 塊に化ける
	 *     (点集合は同じでも @nparts@ / @part@ の答えが変わる = 値が変わる)。
	 *     ⇒ 2-多様体でない値は SNC を本体・境界を付録として **両方**書く。 */
	NF_FORM_SNC_BND  = 2
};

/* ★ #3499: 幾何クラスは **libsrava_cg に 1 つだけ**あり (#3559 で libsrava_nf_<変種> から
 *   移った)、nef_<変種>.so と橋モジュール
 *   (nef_cg.so / nef_mf.so) がその実体を共有する。ところが nef_*.so は HIDDEN でビルドされる
 *   (同一ソースの 2 変種で記述子シンボルが衝突するため) ので、放っておくと **モジュール側の TU が
 *   vtable/typeinfo の hidden な複製を持ち**、共有ライブラリの実体と別物になって
 *   `d_cast` が null を返す (macOS の nef 38 本落ちと同じ機序・#3433)。
 *   ⇒ 幾何クラスだけは **-fvisibility=hidden を明示的に打ち消す**。 */
#if defined(_WIN32) || defined(__CYGWIN__)
#  define NF_PUBLIC
#else
#  define NF_PUBLIC	__attribute__((visibility("default")))
#endif

/* codec の Sink/Source 抽象 (cgChunkSink/mfChunkSink と同シグネチャ)。 */
struct nfChunkSink   { virtual void chunk(const uint8_t *data, int n) = 0; virtual ~nfChunkSink()   {} };
struct nfChunkSource { virtual void pull (uint8_t *dst, int n)        = 0;
                       virtual int  more()                            { return 1; }
                       virtual ~nfChunkSource() {} };

/* ---- 抽象基底: reader/writer が扱う多態ハンドル (今は 3D のみ・2D は将来) ---- */
class NF_PUBLIC nfGeom : public pigDataWireTyped {
public:
	nfGeom(sPtr<pigInfo> i = thNULL) : pigDataWireTyped(i) {}
	virtual const char* meta_tag()  = 0;   /* D_META 4 バイトタグ "NEF3" */
	virtual uint16_t    repr_type() = 0;   /* catalog §5.2: 2=NEF_SNC */
	virtual int         dim()       = 0;
	virtual void encode(nfChunkSink&)   = 0;
	virtual void decode(nfChunkSource&) = 0;
	virtual bool write_to(const char *path, const char *unit) = 0;
};

/* ★★ #3559: **変種 1 つぶんの顔** (reader が読むのはこれだけ)。
 *   reader (ptsnfWireCacheStreamReaderMesh) は「どの 4CC を受けるか」と「読めなかったとき
 *   どのモジュール名で断るか」を要る。どちらも *変種* の性質なので、階層の static ではなく
 *   **この表へ外出しして reader の ctor 引数で渡す**。
 *   ⚠ 以前は @nfGeom::create_for_meta@ と @NF_MODULE_NAME@ を直に参照していたが、それだと
 *     reader の TU が変種ごとに別物になる = 幾何ライブラリを 1 本にできない。 */
struct nfWireVariant {
	const char   *module;                               /* 診断文の module '%s' */
	sPtr<nfGeom> (*create)(const uint8_t *meta, int len);   /* 4CC → 具体型 (未知は null) */
};
NF_PUBLIC extern const nfWireVariant NF_VARIANT_SNC;      /* nef_snc    (nfMeshSnc) */
NF_PUBLIC extern const nfWireVariant NF_VARIANT_HYBRID;   /* nef_hybrid (nfMesh)    */

/* ---- 3D Nef 多面体 ---- */
class NF_PUBLIC nfNefMesh : public nfGeom {
public:

	/* ★★ #3535②: ctor / dtor は **わざとヘッダに実体を置かない**。
	 *   ⚠⚠ @c Nef_polyhedron_3 の構築・破棄は SNC の機械をアサーション込みで実体化するので、
	 *     inline のままだと **nfNefMesh を作るだけの op** (nfaEmpty3D 等) の TU にも
	 *     CGAL の可変大域状態 (@c _error_handler / @c _error_behaviour) の実体ができ、
	 *     幾何ライブラリ (libsrava_cg) 側のものと別物になる。⇒ 片方で設定してももう片方に効かない。
	 *   ★ クラスの *レイアウト* は inline でなくても分かる (sizeof は型が完全なら足りる) ので、
	 *     実体を .cpp へ移すだけで op 側の実体化が止まる。 */
	nfNefMesh(sPtr<pigInfo> i = thNULL);
	virtual ~nfNefMesh();

	/* reader が "MESH" タグで作ったときに立てる: decode は SNC でなく厳密境界として読む。 */
	void set_boundary_input() { boundaryInput_ = 1; }
	/* reader が "MFM3" タグで作ったときに立てる: Manifold の raw-double mesh を読む。
	 * double は 2 進有理数なので EPECK への取り込みは**無損失** (cgMesh3D::decode_mfm3 と同じ理屈)。 */
	void set_mfm3_input() { mfm3Input_ = 1; }

	/* ★ #3545: CGAL の実体は不透明な箱の中。中身を取るのは nfMeshCgal.h の nf_nef()。 */
	nfNefBox&       box()       { return *box_; }
	const nfNefBox& box() const { return *box_; }

	virtual sPtr<stdString> get_str();   /* 表示用 */

	/* ★★ #3559: **変種が名乗るもの**。ここが 2 型に割れる理由そのもので、
	 *   実装 (このクラス) は 1 つのまま。⇒ 派生は下の 3 本 + make_empty だけを書く。 */
	virtual const char* meta_tag()  = 0;   /* D_META 4CC "NEF3" / "NEFB" */
	virtual const char* type_name() = 0;   /* 実装型名 (4CC と 1:1) */
	/* 書き出しの形式を選ぶ: 1 = 境界を取れるなら厳密境界で書く (hybrid) /
	 *                      0 = 常に SNC だけを書く (snc)。⇒ encode の中の唯一の分岐。
	 * ★ 以前は @#if defined(NF_WIRE_HYBRID)@ で、**ライブラリを 2 本建てる唯一の理由**だった。 */
	virtual int         wire_hybrid() const = 0;
	/* ★★ 同じ変種の**空の値**を作る。op の結果は入力と同じ変種でなければならないので
	 *   (nf-mesh3d の union が nfb-mesh3d を返したら型が変わる)、このクラスの中の
	 *   「新しい Nef を返す」経路はすべてこれを通る。⚠ 直に @thNEW(nfMesh,...)@ と書くと
	 *   **snc の op が hybrid の値を作る**。 */
	virtual sPtr<nfNefMesh> make_empty(sPtr<pigInfo> i = thNULL) const = 0;

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
	static sPtr<nfNefMesh> bool_from_args(sArray<sPtr<pigData> > *args, const char *kind,
	                                   const char **errmsg);

	sPtr<nfNefMesh> op_union       (sPtr<nfNefMesh> o);
	sPtr<nfNefMesh> op_intersection(sPtr<nfNefMesh> o);
	sPtr<nfNefMesh> op_difference  (sPtr<nfNefMesh> o);
	sPtr<nfNefMesh> op_complement  ();          /* ★Nef 固有: 結果は非有界になりうる */

	/* ---- アフィン変換 (行優先 double[12] = 3x4。cgMesh3D::apply_affine と同じ規約) ----
	 * 引数の解釈と行列の組み立ては common/affine.h (カーネル非依存)。ここは適用だけ。
	 * ★ **Nef のまま** Aff_transformation_3 で変換する (型維持・境界表現へ戻さない)。
	 *   非有界な Nef も変換できる (CGAL が infimaximal box 側を面倒みる)。
	 * ⚠ 反射 (det<0) の向き補正は **CGAL の Nef_polyhedron_3::transform が中でやる**。
	 *   cgMesh3D のように reverse_face_orientations を後から当てる必要は無い
	 *   (実測 2026-09-05: [0,3]x[0,1]^2 を x 鏡像 → 体積 3 のまま -x 側に移り、
	 *    後段の union / difference も正しい = 補集合に化けていない)。
	 *   ★素の頂点書き換えではこうならない — Nef は面の向きを **球面地図の巡回順**で
	 *   持っており、向きを反転する写像では巡回順も裏返す必要があるため。
	 * ⚠ 回転の cos/sin は double の近似。EPECK なのは「近似された行列を厳密に適用する」
	 *   ところまでで、回転そのものが厳密になるわけではない。 */
	sPtr<nfNefMesh> apply_affine(const double e[12]);

	/* ---- Minkowski 和 A ⊕ B (#3440) ----
	 * ★Nef 固有。offset はこの特殊形 (球との和) なので、これがプリミティブ。
	 *   凸どうしなら頂点対の和の凸包で済むが、凹があると **両者を凸分解して m×n ペア**の和を
	 *   取って union するため重い (CGAL::minkowski_sum_3 が内部で凸分解する)。
	 * ★前提: **両者が有界**であること。CGAL は非有界を渡されると stderr に文言を出して
	 *   片方をそのまま返す (= 黙って嘘の答えになる) ので、呼び側で先に is_bounded() を見て
	 *   明示エラーにすること (nfaMinkowski がやっている)。 */
	sPtr<nfNefMesh> op_minkowski   (sPtr<nfNefMesh> o);

	/* ---- 3D オフセット (#3440 の 2: cgal.so から移設) ----
	 * @d>0@ 膨張 = A ⊕ (半径 d の球) / @d<0@ 収縮 = 補集合トリック
	 * (erode(A,r) = A − dilate(bbox−A, r))。球は icosahedron を @subdiv@ 回 Loop 細分して
	 * 球面へ投影した近似球 (subdiv 大 = 滑らかだが凸分解のペア数が増えて重い)。
	 * ★2D offset は straight skeleton で Nef と無関係なので **cgal.so に残る**。
	 * 非有界は @op_minkowski@ と同じく呼び側が弾く。 */
	sPtr<nfNefMesh> op_offset      (double d, int subdiv);

	/* ---- 有界性 ----
	 * ★SNC の**最初の volume は無限体積**。それが mark されている(= 選択されている)なら
	 *   この Nef は非有界。@is_simple()@ は「境界が 2-多様体か」であって**有界性ではない**
	 *   (箱の補集合は非有界だが is_simple() は真で、境界だけ書き出すと体積 8 の箱に化ける)。 */
	bool is_bounded() const;

	/* ---- 素性を訊く op (#3487) ----------------------------------------------
	 * ★ **境界表現へ落として測る** (to_mesh)。SNC のまま測れるのは体積くらいで、
	 *   bbox / 重心 / 表面積 / 自己交差は境界が要る。volume / export と同じ経路。
	 * ⚠ 非有界 (complement の結果など) と非 2-多様体は to_mesh が失敗する。そのときは
	 *   0 を返し、**呼び側が明示エラーにする** (黙って 0 や原点を返さない)。
	 *   ただし valid だけは違う: to_mesh の失敗そのものが「妥当でない」の答えなので 0 を返す。
	 * ★ valid の定義は 7 カーネル共通で ① 空でない ∧ ② 閉じている ∧ ③ 自己交差が無い。
	 *   nef では ② が to_mesh の成功 (有界かつ 2-多様体) に、③ が CGAL の厳密述語
	 *   does_self_intersect に対応する。⚠ **Nef 構築は面どうしの交差を検査しない**ので、
	 *   自己交差した境界はそのまま SNC に入っている (#3445) — ③ は本当に要る。 */
	int    op_bbox(double mn[3], double mx[3]);
	int    op_centroid(double c[3]);
	int    op_area(double *out);
	int    op_valid();

	/* ★ 境界メッシュから Nef を作れなかった (自己交差など Nef の前提を満たさない入力)。
	 *   立てておいて **呼び側が明示エラー** にする。黙って空集合を返さない。 */
	int  build_failed() const { return buildErr_; }

	/* ---- 凸分解 (#3441) ----
	 * A を凸片へ分解した Nef を返す (@CGAL::convex_decomposition_3@)。返り値は **内部に仕切り面を
	 * 持つ Nef** = @is_simple()@ は偽になる。凸片は @to_mesh@ が volume ごとに取り出すので、
	 * @volume@ (合計 = 元と同じ) も @export@ (別々の連結成分として書く) も通る。
	 * ★片ごとに個別の値として取り出すには **mesh の配列**を返せる必要があり、今の op 表 (出力は
	 *   単一 cache か値) では書けない。当面は「1 つの mesh の中に凸片が別成分として入る」形にする。 */
	sPtr<nfNefMesh> op_convex_decomposition();

	/* ---- 塊 (part) の取り出し (#3441 追補・ひさ提案 2026-08-18) ----
	 * 「mesh の配列」を返す仕組みが無いので、**個数を返す op と n 番目を返す op** の 2 本にする。
	 *   @op_nparts()@ = 塊の数 = SNC の **marked volume の数** (無限体積は数えない)。
	 *   @op_part(i)@  = i 番目の塊 (0 始まり)。その volume の **全シェル** (外殻 + 空洞) から作るので
	 *                   空洞を持つ塊も正しく取り出せる。
	 * 例: @convex_decomposition(m)@ の結果に使えば凸片が 1 つずつ得られる。
	 *     普通の立体は 1・離れた 2 立体は 2・空洞つき立体は 1 (空洞は塊ではない)。 */
	int          op_nparts();
	sPtr<nfNefMesh> op_part(int i);

	/* ---- 内壁除去 (#3442) ----
	 * Nef の **正則化** (@closure(interior())@)。内部に残った仕切り面 (両側とも立体の facet) が
	 * 消える。外側の境界と**空洞の境界**は残る (片側が立体でないので本物の境界)。
	 * ★@repair@ とは別物で **体積が変わりうる** (低次元の破片も落ちる)。自動ではやらない。 */
	sPtr<nfNefMesh> op_unify_shells();

	/* ---- 壊れた境界からソリッドを組み直す (#3445) ----
	 * 面 1 枚ごとに Nef を作って n 項 union → **有界セルを mark** する (@Mark_bounded_volumes@)。
	 * 面どうしの交差線は union の過程で実エッジになる = **自己交差がここで解ける**
	 * (だから autorefine を前段に置く必要は無い。実測でも refine 有無で同じ答えだった)。
	 * ★@Mark_bounded_volumes@ は有界セルを**無差別に**塗るので、そのままだと空洞が埋まる。
	 *   @solidify_mesh@ が **連結成分ごとに**これを適用し、成分どうしを入れ子の深さで
	 *   合成することで空洞を救う。 */
	/* solidify の本体。入力は nf (自己交差したまま入っていてよい — Nef 構築は面どうしの交差を
	 * 検査しないので、壊れた形のまま SNC に入り @to_mesh()@ で面が取り出せる)。
	 * 連結成分ごとに build_from_facets し、成分どうしを入れ子の深さで合成する。 */
	static sPtr<nfNefMesh> solidify_mesh(sPtr<pigData> in);

	/* ---- 境界表現への変換 (volume / export / cache 書き出しの時だけ) ----
	 * 非有界 (complement の結果など) / 非 2-多様体は false。呼び側がエラーにする。 */
	/* ★ 直前の to_mesh の失敗理由 (静的文字列)。まだ失敗していなければ null。
	 *   呼び側 (export など) がそのままエラー文に載せる。 */
	const char *last_error() const { return lastErr_; }
	/* ★ #3545: CGAL 型を取る API は nfMeshCgal.h の**自由関数**になった (公開ヘッダに
	 *   CGAL 型を書けないため)。その 2 本が触る内部状態だけをここから開ける。
	 *   ⚠ 精密な friend 宣言は書けない — 引数型 @nfNefBox::Mesh@ が公開ヘッダでは
	 *     名指しできないため。⇒ 最小のアクセサで開ける方を採った。 */
	void set_last_error(const char *w) { lastErr_ = w; }
	void set_build_error(int e)        { buildErr_ = e; }
	/* Surface_mesh から Nef を作る (leaf op / decode 共通)。 */

	/* ★ #3535②: 直方体を作って Nef へ (make_hexahedron + triangulate)。
	 *   ⚠ op 側に置くと CGAL の可変大域状態の実体が nef_*.so にもできる
	 *     (理由は上の ctor / dtor のところ)。⇒ CGAL に触るのは 幾何ライブラリ (libsrava_cg) 側だけ。 */
	void build_box(double w, double h, double d);

	/* ★ #3535②: 体積。@c to_mesh → @c PMP::volume → @c CGAL::to_double までをここでやる。
	 *   ⚠ @c CGAL::to_double は **Lazy_exact_nt の精度設定 (可変な関数内 static) を読む**ので、
	 *     op 側で呼ぶとその実体がモジュール .so にもできる。⇒ 変換もこちら側に置く。
	 *   返り 1 = 出せた / 0 = 境界が作れない (理由は last_error / nf_why で取る)。 */
	int volume(double *out);

	/* ★ #3545: 境界表現の頂点数 / 面数。**op 側で Mesh を持たなくて済む**ようにする。
	 *   返り 1 = 出せた / 0 = 境界が作れない (理由は last_error / nf_why で取る)。 */
	int boundary_counts(int *nverts, int *nfaces);

	/* ★★ #3545: **素の配列から境界メッシュを作って Nef へ**。
	 *   ⚠ これが無いと生成 op (nfTriSink を使う 9 本 + sphere) が CGAL の Surface_mesh を
	 *     宣言することになり、その TU が CGAL を引き込む。⇒ 受け皿はこちら側に置く。
	 *   xyz = 頂点 3 成分ずつ nv 個 / idx = 三角形 3 索引ずつ nt 個。 */
	void build_from_triangles(const double *xyz, int nv, const int *idx, int nt);
	/* ★★ #3545 段 4: **厳密境界を cg の "MESH" フレーミングで書き出す** (橋の口)。
	 *   返り 1 = 書けた / 0 = 境界表現が無い (非有界 = complement の結果など)。
	 *   ⚠ これは *nef 自身が既に使っている形式*で (nfNefMesh::encode の NF_FORM_BOUNDARY と
	 *     同じ cgaMeshCodec)、新しい依存は増えない。⇒ 橋 (nef_mf) は CGAL を 1 枚も
	 *     引かずに「厳密な境界のバイト列」を受け取れる。
	 *   ⚠ 厳密有理数を文字列で書く形式なので **無損失**。double へは落ちない。 */
	int to_exact_boundary_bytes(std::vector<uint8_t> &out);
	/* 測地球 (sphere / icosphere)。生成器は common/geodesic.h (カーネル非依存)。 */
	void build_geodesic(int seed, int n, double r);

private:
	void	decode_boundary(nfChunkSource&);   /* "MESH" (cg の厳密境界) → SNC 構築 */
	void	decode_mfm3(nfChunkSource&);       /* "MFM3" (Manifold の raw double) → SNC 構築 */

	nfNefBox *box_;   /* ★ #3545: CGAL の実体 (不透明)。ctor で確保・dtor で解放 */
	int	boundaryInput_;
	int	buildErr_;
	int	mfm3Input_;
	/* ★ #3504: to_mesh が false を返した理由 (静的文字列・null = まだ失敗していない)。
	 *   write_to → export の失敗はこれまで理由を 1 つの定型文に丸めていたので、
	 *   「非有界」と「書き出しが空を吐いた」が区別できなかった。 */
	const char *lastErr_;
};

/* ==================================================================
 * ★★ #3559: **wire の変種 = 2 つの型**。実装は上の nfNefMesh に 1 つだけ在り、
 *   ここに在るのは「何と名乗るか」と「どう書き出すか」だけ。
 *   ⚠ 2 つは **兄弟**。片方をもう片方の派生にすると @pig_wire_match@ (d_cast) が
 *     1:1 で割れなくなり、writer の選択が取り違う (nfMesh.h 冒頭の ★★ を見よ)。
 *
 * ⚠⚠ **@make_empty@ は .cpp に置いたまま**にすること (ヘッダへ inline で畳まない)。
 *   C++ は vtable を「最初の非 inline 仮想関数を定義した TU」に出す (key function)。
 *   この 2 型で非 inline なのは @make_empty@ だけなので、いまは vtable / typeinfo が
 *   **libsrava_cg にしか無い** (実測 2026-09-19: nef_snc.so 側は @U@ = 借用)。
 *   全部を inline にすると weak シンボルとして **各モジュール .so にも複製**され、
 *   モジュールは @-fvisibility=hidden@ で建つので local に落ちて別物になる
 *   ⇒ @d_cast@ が null を返す (macOS の nef 38 本落ちと同じ機序・#3433)。
 * ================================================================== */

/* ---- hybrid : 境界表現を取れるなら厳密境界で書く (型 nfb-mesh3d / 4CC NEFB) ----
 * ★ **名前を踏襲した側**。将来 SNC を廃止してもこの型は残る。 */
class NF_PUBLIC nfMesh : public nfNefMesh {
public:
	nfMesh(sPtr<pigInfo> i = thNULL) : nfNefMesh(i) {}

	virtual const char* meta_tag()  { return "NEFB"; }
	virtual const char* type_name() { return "nfb-mesh3d"; }
	virtual int         wire_hybrid() const { return 1; }
	virtual sPtr<nfNefMesh> make_empty(sPtr<pigInfo> i = thNULL) const;

	/* reader 用ファクトリ: D_META タグから具体型を生成 (未知タグは null)。
	 * ★ 自分の 4CC ・ もう一方の nef の 4CC ・ "MESH" (cg の厳密境界) ・ "MFM3"
	 *   (Manifold の raw double) を受理する = 昇格読み。判定の中身は両変種で同じ。 */
	static sPtr<nfGeom> create_for_meta(const uint8_t *meta, int len);

	/* ★ 2026-08-28 (ABI v12): **この型への配線先**。op の OPS 行が OPWIRE(Calc, NF_MESH) と
	 *   書くと、引数はこの WIRE 経由で実体化される。create_for_meta が 4CC を受理判定し、
	 *   mkReader がこの型の stream reader を起こす。定義は nfWire.cpp。
	 *   ⚠ #3559 より前は @nfGeom::WIRE@ の 1 本だった — 階層に 1 つしか無かったのは
	 *     *ライブラリが変種ごとに別だった* からで、1 本になった今は **型ごとに要る**。 */
	static const pigWireClass WIRE;
};

/* ---- snc : 常に SNC だけを書く (型 nf-mesh3d / 4CC NEF3) ----
 * ★ **改名した側** (旧 nfMesh with -DNF_WIRE_SNC)。将来廃止するのはこちら。
 * ⚠ 他カーネルへ渡す口は持たない — cgal / manifold は SNC を読めないので、
 *   橋モジュール (nef_cg.so / nef_mf.so) が cast のときだけ境界を作る。 */
class NF_PUBLIC nfMeshSnc : public nfNefMesh {
public:
	nfMeshSnc(sPtr<pigInfo> i = thNULL) : nfNefMesh(i) {}

	virtual const char* meta_tag()  { return "NEF3"; }
	virtual const char* type_name() { return "nf-mesh3d"; }
	virtual int         wire_hybrid() const { return 0; }
	virtual sPtr<nfNefMesh> make_empty(sPtr<pigInfo> i = thNULL) const;

	static sPtr<nfGeom> create_for_meta(const uint8_t *meta, int len);
	static const pigWireClass WIRE;
};

/* ★ #3504: to_mesh が失敗した理由を 1 行にする。理由を残していない経路のために既定文を持つ。
 *   ⚠ 各 op のエラー文に **原因を決め打ちで書かない** こと。以前は全部が「非有界」と
 *     名指ししていたので、書き出しが空を吐く別の失敗まで「非有界」と嘘をついていた。 */
inline const char* nf_why(sPtr<nfNefMesh> m)
{
	const char *w = m.is_notNull() ? m->last_error() : 0;
	return w ? w : "unbounded or non-manifold";
}

/* ★ #3511: 凸包 hull(a[,b,…]) — **1 個以上**を受け、与えた Nef すべての凸包を返す (nfMesh.cpp)。
 *   ★ Nef の**頂点だけ**を集めて CGAL::convex_hull_3 へ渡し、出来た境界を set_from_mesh で
 *     Nef へ戻す。⇒ SNC のままでは凸包を取れないが、頂点を抜くのは安い。
 *   ⚠ **非有界は弾く** — 補集合などの非有界 Nef は「頂点の凸包」では表せない
 *     (op_minkowski と同じ理由・黙って嘘の答えを返さない)。
 *   失敗時は null + *errmsg。 */
sPtr<nfNefMesh> nf_hull_from_args(sArray<sPtr<pigData> > *args, const char **errmsg);

/* ==================================================================
 * ★★ #3559: **op のモジュールだけが持つ -D**。nef_snc.so / nef_hybrid.so は従来どおり
 *   同一ソースを 2 度ビルドするので、そちらは「自分がどちらか」を知る必要がある。
 *   ⚠ ここで選ぶのは **名前と型だけ**で、実装の分岐は 1 つも無い (それが #3559 の主眼)。
 *   ⚠ 幾何ライブラリ (libsrava_cg) の TU は -D **なし**でこの header を読む。だから
 *     「どちらでもない」は誤りではない ⇒ @#error@ は置かない。-D の要る op が -D 無しで
 *     建てば NF_TYPE / nfa_err が未定義でコンパイルが落ちる (黙っては通らない)。
 * ================================================================== */
#if defined(NF_WIRE_SNC)
#  define NF_MODULE_NAME "nef_snc"
#  define NF_MESH        nfMeshSnc      /* この module が作る型 (thNEW / OPWIRE の綴り) */
#  define NF_TYPE        "nf-mesh3d"
#  define NF_TAG         "NEF3"
#  define NF_OTHER_TAG   "NEFB"   /* もう一方 (hybrid) の 4CC — reader は両方受ける */
#  define NF_VARIANT     NF_VARIANT_SNC
#elif defined(NF_WIRE_HYBRID)
#  define NF_MODULE_NAME "nef_hybrid"
#  define NF_MESH        nfMesh
#  define NF_TYPE        "nfb-mesh3d"
#  define NF_TAG         "NEFB"
#  define NF_OTHER_TAG   "NEF3"   /* もう一方 (snc) の 4CC — reader は両方受ける */
#  define NF_VARIANT     NF_VARIANT_HYBRID
#endif

#if defined(NF_MODULE_NAME)
/* ★ #3475: このモジュール専用のエラー生成子。文言は "[TAG] <name>/op: message" になる。
 *   素の nfa_err(...) を使うとモジュール名が付かない。 */
PIG_DEFINE_MODULE_ERR(nfa_err, NF_MODULE_NAME)
#endif

#endif
