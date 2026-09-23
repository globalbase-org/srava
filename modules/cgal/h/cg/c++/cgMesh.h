#ifndef CG_MESH_H
#define CG_MESH_H
/*
 * cgMesh 階層 — CGAL 幾何を pigData でラップした多態な値ハンドル(pigData と同じ考え方)。
 *
 *   cgMesh    … 抽象基底。ブーリアン(op_*)・アフィン(apply_affine)・codec(encode/decode)・
 *               キャッシュ認識(meta_tag/repr_type/dim)を virtual で持つ。次元非依存のコードは
 *               基底ポインタ越しにこれらを呼ぶ(cgaUnion 等は次元を一切知らない)。
 *   cgMesh3D  … 3D Surface_mesh<EPECK> 実装。
 *   cgMesh2D  … (将来)2D Polygon_with_holes_2 実装。
 *
 * 中間 blob は持たない(ひさレビュー 2026-06-06): codec は cgChunkSink/cgChunkSource 越しに
 * d_chunk/pull を直接呼び、mesh 本体 + 高々 1 チャンクしかメモリに乗らない。reader の結果は
 * cgatsAgent の argv(sArray<sPtr<pigData>>)→ 計算本体という既存 pigData 経路を通る。
 * ★★ #3545 段 2: **このヘッダは CGAL を一切 include しない**。
 *   CGAL は header-only なので、1 枚でも include すると *読んだ TU 全部* に CGAL の
 *   可変大域 (@c _error_handler / @c IO::Static::get_mode 等) の実体が emit される。
 *   モジュールは @c -fvisibility=hidden で建つのでそれが local コピーになり、libsrava_cg 側と
 *   状態が食い違う ⇒ op はこのヘッダしか読まない、という境界を **型で**引く。
 *   ⇒ CGAL の実体は不透明な @c cgMesh3DBox / @c cgMesh2DBox が抱え、CGAL 型を取る API は
 *     cg/c++/cgMeshCgal.h に自由関数として置く (読んでよいのは幾何 lib と橋だけ)。
 *   ★ 検査 = test/srava_op_cgal_free.sh (リンクされた .o を 1 本ずつ数える)。
 */
#include	"pig/c++/pigData.h"
#include	"pig/c++/pigModuleError.h"   /* #3475: 自分の名前でエラーを作る */
#include	"pig/c++/pigOpEntry.h"   /* pigWireClass (配線先) */
#include	"common/tube.h"   /* ★ #3535②: TubeV3 (カーネル中立) */
#include	<vector>
#include	<stdint.h>

/* ★ #3545: CGAL の実体を抱える不透明な箱 (定義は cg/c++/cgMeshCgal.h)。 */
class cgMesh3DBox;
class cgMesh2DBox;

/* codec の Sink/Source 抽象(encode/decode を virtual 化するため。writer/reader が adapter で実装)。 */
struct cgChunkSink   { virtual void chunk(const uint8_t *data, int n) = 0; virtual ~cgChunkSink()   {} };
struct cgChunkSource { virtual void pull (uint8_t *dst, int n)        = 0;
                       /* まだ読めるデータが残っているか(後方互換: 旧 blob で末尾の任意セクションを
                        * 読むかの判定。W_END に達していれば 0)。既定 1(完全バッファ前提)。 */
                       virtual int  more()                            { return 1; }
                       virtual ~cgChunkSource() {} };

class cgMesh : public pigDataWireTyped {   /* rev4 Phase A: 型軸 marker 基底 (旧 pigData 直継承) */
public:
	/* ⚠ #3545: 旧 @c K / @c Point_3 / @c Mesh の入れ子 typedef はここから **撤去**した
	 *   (書いた時点でこのヘッダが CGAL を要求してしまう)。⇒ cg/c++/cgMeshCgal.h。 */

	cgMesh(sPtr<pigInfo> i = thNULL) : pigDataWireTyped(i) {}

	virtual sPtr<stdString> get_str();   /* 表示用(out-of-line = vtable/typeinfo anchor) */

	/* ---- キャッシュ認識(D_META)---- */
	virtual const char* meta_tag()  = 0;   /* D_META 4 バイトタグ "MESH"/"PLY2" */
	virtual uint16_t    repr_type() = 0;   /* 1=3D mesh, 32=2D poly(catalog §5) */
	virtual int         dim()       = 0;   /* 2 or 3 */
	/* ---- codec(D_CHUNK ストリーム)---- */
	virtual void encode(cgChunkSink&)   = 0;
	virtual void decode(cgChunkSource&) = 0;
	/* ★ #3433: decode が「この形式は cg の表現力では受け取れない」と判断したときに立てる。
	 *   reader はこれを見て errCode を立てる (空 mesh を黙って返さない)。 */
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
	/* ---- ブーリアン(同次元の新 mesh を返す。異次元/失敗は null=呼び元が A_ERROR)---- */
	virtual sPtr<cgMesh> op_union       (sPtr<cgMesh> b) = 0;
	virtual sPtr<cgMesh> op_intersection(sPtr<cgMesh> b) = 0;
	virtual sPtr<cgMesh> op_difference  (sPtr<cgMesh> b) = 0;
	/* ---- combine(交差を解かず単純合体・viewer 用 `+++`)。ブール演算前の状況確認に。
	 *      3D=両 Surface_mesh を 1 つに連結(corefinement しない)/ 2D=両領域の Pwh をそのまま集める。
	 *      異次元/null は null。重なり/自己交差は許容(閉立体性は保証しない) ---- */
	virtual sPtr<cgMesh> op_combine     (sPtr<cgMesh> b) = 0;
	/* ---- アフィン変換(3D 型の行優先 double[12]。2D は z 行・列を無視)→ 同次元の新 mesh ---- */
	virtual sPtr<cgMesh> apply_affine(const double e[12]) = 0;

	/* ---- オフセット(d>0 膨張 / d<0 収縮)。2D=straight skeleton / 3D=Minkowski(球)。
	 *      subdiv=3D の球(icosphere)細分化レベル(大=滑らか・重い。2D は無視)。失敗は null ---- */
	virtual sPtr<cgMesh> op_offset(double d, int subdiv) = 0;

	/* ---- 計測(値を返す。2D=囲み面積 / 3D=表面積)。値返し op = WriterText で直列化 ---- */
	virtual double op_area() = 0;   /* 2D: 囲み面積(外周−穴) / 3D: 表面積 */
	/* ★ #3443: 頂点数 / 面数。planner が cache のバイト列を直接読んで表示していたのをやめ、
	 *   **幾何の語彙はモジュール側の op が答える**ようにした (planner はカーネル中立に戻る)。
	 *   2D は面を持たないので op_nfaces()=0・op_nverts() は点の総数 (外周 + 穴)。 */
	virtual int    op_nverts() = 0;
	virtual int    op_nfaces() = 0;
	/* ★ #3527: **i 番目の頂点の座標**。out に座標を書き、返り = 次元 (3D:3 / 2D:2)。
	 *   範囲外・取り出せない場合は **0**。⇒ 呼び側 (op) が 0 を見てエラーにする。
	 * ★ 「数える」(op_nverts) と **同じ列を同じ順**で見る。⇒ i は 0 .. op_nverts()-1。
	 * ⚠⚠ 索引は **実装依存** (列挙順をなぞるだけ) — 版・ビルド・入力順で変わりうる。
	 *   キャッシュのキーに焼き付くので、**同じ式が同じ i で同じ頂点を返し続けること**を
	 *   検査で押さえること (#3518 の face と同じ形)。
	 * ⚠ 2D は **枠の中の (x,y)** を返す (op_bbox(2D) が返り 2 なのと同じ約束)。 */
	virtual int    op_vert(int i, double out[3]) = 0;
	/* ★ #3527: **全頂点**を平坦な配列へ。out に dim*nverts 個を積み、返り = 次元 (3 / 2)。
	 * ★★ op_vert と **同じ列を同じ順**で積む。⇒ verts(m)[i] == vert(m,i) が全 i で成り立つ。
	 *   ⚠ 片方だけ順序を変えると黙ってずれるので、**検査でこの等式を見る** (test/srava_vert.sh)。
	 * ★ 平坦な double 配列は ptCloud の内部表現そのものなので、そのまま渡せる。 */
	virtual int    op_verts(std::vector<double> &out) = 0;
	virtual double op_volume()    = 0;   /* 3D: 囲む体積(閉メッシュ)/ 2D: 0(呼び元が dim ガード) */
	virtual double op_perimeter() = 0;   /* 2D: 境界長(外周+穴)/ 3D: 0(呼び元が dim ガード) */
	virtual int    op_centroid(double out[3]) = 0;   /* 面積/体積重心。out に座標、返り=次元(2 or 3) */
	virtual int    op_bbox(double mn[3], double mx[3]) = 0;   /* 軸平行 AABB。mn/mx に min/max 隅、返り=次元(2 or 3) */

	/* ---- 検査/修復 ---- */
	virtual int          op_valid()  = 0;   /* 1=正常 / 0=問題(3D: 閉∧¬自己交差 / 2D: 全リング単純)。値返し */
	/* ★ #3514: 位相を **直接**数える。返り 1 = 閉じている (0 なら genus は意味を持たない)。
	 *   3 つの数の定義 (シェル / 塊 / 種数) と「塊 = 符号つき体積が正のシェル」という導き方は
	 *   src/h/common/meshprops.h の topology() に一本で書いてある。cgal は **CGAL の側で**
	 *   同じ定義を答える (meshprops.h は double の実装なので厳密カーネルでは使わない — valid と同じ方針)。
	 *   2D は位相を持たないので 0 を返す (呼び元が dim ガード)。 */
	virtual int          op_topology(int *nshells, int *nparts, int *genus) = 0;
	/* ★ #3514: **点との距離** — p から境界までの最短距離 (符号なし)。返り 1 = 出せた / 0 = 出せない。
	 *   2D は 0 (呼び元が dim ガード)。 */
	virtual int          op_distance_at(const double p[3], double *out) = 0;
	virtual sPtr<cgMesh> op_repair() = 0;   /* 修復した同次元の新 mesh(3D: autorefine / 2D: even-odd repair)。失敗は null */

	/* ---- 着色: 全面に色(r,g,b: 0-255)を付けた同次元の新 mesh。per-face property map "f:color"。
	 *      combine(+++)で各成分の色が保持され、色対応の export(3MF/AMF/OFF/PLY)で出る。3D 専用(2D は null)。 ---- */
	virtual sPtr<cgMesh> op_color(int r, int g, int b) = 0;

	/* ---- 断面: 点 P を通り法線 N の平面でメッシュを切り、2D 断面(cgMesh2D)を返す。
	 *      3D 専用(2D は null=エラー)。面内の正規直交基底で 2D に射影 → even-odd 充填。失敗/退化 N は null。 ---- */
	/* mode: 0 = 平面ちょうど / -1 = 平面の直下(h-ε)/ +1 = 平面の直上(h+ε)。
	 * coplanarOut != 0 なら「平面と共面の面が在ったか」を 1/0 で返す(呼び側が仕様判定に使う)。 */
	virtual sPtr<cgMesh> op_section(const double P[3], const double N[3],
	                                int mode = 0, int *coplanarOut = 0) = 0;

	/* ---- ファイル書き出し(拡張子で形式判定。3D=OFF/STL/.. / 2D=SVG/DXF)。成否を返す。
	 *      unit = 単位文字列("mm"/"cm"/"in"/...)。SVG=width/height に付与、DXF=$INSUNITS、
	 *      単位概念のない形式(OFF/STL/..)や空文字は無視。 ---- */
	virtual bool write_to(const char *path, const char *unit) = 0;

	/* reader 用ファクトリ: D_META タグから具体型を生成(未知タグは null)。 */
	static sPtr<cgMesh> create_for_meta(const uint8_t *meta, int len);

	/* ★ 2026-08-28 (ABI v12): **この階層への配線先**。op の OPS 行が OPWIRE(Calc, cgMesh) と
	 *   書くと、引数はこの WIRE 経由で実体化される。create_for_meta が 4CC を受理判定し、
	 *   mkReader がこの階層の stream reader を起こす。定義は cgCacheCodec.cpp。 */
	static const pigWireClass WIRE;
};

/* 3D メッシュ(EPECK Surface_mesh)。 */
class cgMesh2D;   /* ★ #3535②: build_extrude の引数 (実体はこの下) */

class cgMesh3D : public cgMesh {
public:
	/* ⚠ #3545: ctor / dtor は **out-of-line** (cgMesh3D.cpp)。ヘッダに inline で置くと
	 *   箱のメンバの ctor/dtor がそこで実体化され、読んだ TU 全部に CGAL が emit される
	 *   — nef が段 1 で踏んだ形そのもの (nfMesh は *作るだけ* の op 11 本に実体ができていた)。 */
	cgMesh3D(sPtr<pigInfo> i = thNULL);
	virtual ~cgMesh3D();

	/* ★ #3545: CGAL の実体 (不透明)。中身を触れるのは cg/c++/cgMeshCgal.h を読める TU だけ。
	 *   ⇒ 旧 @c mesh() は撤去。幾何 lib / 橋は @c cg_mesh(*this) を使う。 */
	cgMesh3DBox&       box()       { return *box_; }
	const cgMesh3DBox& box() const { return *box_; }

	virtual sPtr<stdString> get_str();
	virtual const char* meta_tag()  { return "MESH"; }
	virtual const char* type_name() { return "cg-mesh3d"; }   /* rev4 実装型名 (MESH と 1:1) */
	virtual uint16_t    repr_type() { return 1; }
	virtual int         dim()       { return 3; }
	virtual void encode(cgChunkSink&);
	virtual void decode(cgChunkSource&);
	/* ★ Manifold(MFM3)キャッシュを EPECK Surface_mesh へ **無損失昇格** して取り込む(#3404)。
	 *   create_for_meta が D_META タグ "MFM3" を検出すると set_mfm3_input() を立て、以後 decode() は
	 *   自型の MESH codec でなく MFM3 の raw-double framing 経路(decode_mfm3)を選ぶ。double は 2 進
	 *   有理数なので EPECK への変換は厳密=損失なし(逆 exact→float だけが損失で、そちらは明示 cast)。
	 *   これで cg agent(=CGAL カーネル)が Manifold カーネルの出力キャッシュを透過的に入力できる。
	 *   昇格後は普通の cgMesh3D(meta_tag="MESH")なので encode/ブール/計測は全て既存経路。 */
	void set_mfm3_input() { mfm3Input_ = 1; }
	/* ★ #3433/#3440: nef の出力キャッシュ ("NEFB") を cgMesh3D として受理する。
	 *   ★cg(corefinement)は**有界な 2-多様体しか表現できない**ので、表現できないものは
	 *   decode で **失敗させる**(黙って境界だけ拾うと「箱の補集合」が「箱」に化ける)。
	 *   ★cgal.so は **SNC をパースしない**ので、受理できるのは payload 形式が
	 *   **厳密境界 (NF_FORM_BOUNDARY)** のものだけ。
	 *   ⚠ #3559 で理由が変わった (依存できない → reader を持たない)。cgMesh3D::decode_nef3
	 *     の ⚠⚠ に経緯がある。
	 *   nef 側が SNC で書く値 (非有界・非多様体 = そもそも cg で表現できない) は明示エラーになる。 */
	void set_nef3_input() { nef3Input_ = 1; }

	/* ★★ #3535②: **プリミティブの構成を op 側から引き取る**。
	 *   ⚠⚠ 置き場所を決めているのは「何を作るか」ではなく **どの .so に実体があるか**。
	 *     CGAL は 6.x でヘッダオンリー (libCGAL* が存在しない) なので、op の TU が CGAL の
	 *     **カーネルのヘッダを include すると その .so の中にテンプレートが実体化される**。
	 *     ⚠⚠ #3535② のときここに「*使うと*」と書いたが、**それは誤り** (#3545 で実測):
	 *       @c <CGAL/Exact_predicates_exact_constructions_kernel.h> を 1 枚 include して
	 *       **何も呼ばない** TU にも、可変大域が 2 本・@c CGAL:: の定義が 42 本 emit される。
	 *       ⇒ 非 inline の wrapper を足すだけでは止まらない。**公開ヘッダから CGAL を落とす**
	 *         (= 段 2 でやったこと) のが唯一の道。
	 *     ⚠ ただし「include すれば必ず出る」でもない — @c Surface_mesh.h / @c Polygon_2.h /
	 *       @c IO/Color.h / @c assertions.h は **単独では 0 本**。引き金は *カーネル* のヘッダ。
	 *       ★ 正確には **どのヘッダを読んだか**で決まる (表は cg/c++/cgMeshCgal.h の冒頭)。
	 *     その中には CGAL の
	 *     *可変な大域状態* (@c get_default_random / @c _error_handler / @c _error_behaviour /
	 *     @c IO::Static::get_mode) の実体も含まれ、⇒ cgal.so と libsrava_cg で **状態が別物**になる。
	 *     片方で設定してももう片方に効かない = geogram の CmdLine と同型の地雷 (#3535 4 節)。
	 *   ⚠ geogram (#3535①) と違って「借りる形に寄せる」ことはできない — 上流の実体が
	 *     そもそも存在しないため。⇒ **実体化そのものを止める** = op から CGAL を排す、しかない。
	 *   ⇒ CGAL に触るのは libsrava_cg 側だけ、という規約。op は引数を渡すだけ。
	 *   ★ 検査: test/srava_op_cgal_free.sh (**op の .o を 1 本ずつ**数える = #3545 段 3)・
	 *     test/srava_upstream_copies.sh (.so ごとのコピー数)・test/srava_upstream_globals.sh (柵)。
	 *
	 *   ⚠ 呼び手が引数を検査してから呼ぶこと (ここでは検査しない — エラー文言は op ごとに違う)。 */
	void build_box(double w, double h, double d);
	/* ★★ #3545 段 2: **三角形スープから作る** (cgTriSink / 共通生成器の受け口)。
	 *   ⇒ cone / cylinder / torus / tetrahedron / pyramid の op が CGAL を引かずに済む。
	 *   xyz … 3*nv 個 ・ idx … 3*nt 個の頂点番号。⚠ 既存の面は消さずに **足す**。 */
	void build_from_triangles(const double *xyz, int nv, const int *idx, int nt);
	/* ★★ #3545 段 2: 測地球 (sphere / icosphere)。seed は srava_geo::SEED_*。
	 *   ⚠ 実体 (cga_make_geodesic) は cgMesh3D.cpp。op は分割数と半径だけ渡す。 */
	void build_geodesic(int seed, int n, double r);
	/* n 角柱。⚠ CGAL は高さを Y 軸に作るので **X 軸 +90° 回転で高さを Z 軸へ**移す
	 *   (extrude / box と揃える)。90° は厳密・det=+1 なので面の向きは変わらない。 */
	void build_regular_prism(int n, double h, double r);
	/* ★ #3535②: OFF / STL / OBJ / PLY を拡張子で判別して読む
	 *   (@c CGAL::Polygon_mesh_processing::IO::read_polygon_mesh)。返り 1 = 読めた。
	 *   ⚠⚠ これを op 側に置くと、CGAL の **IO モード (ASCII/binary) の大域変数**
	 *     (@c CGAL::IO::Static::get_mode()::mode) の実体が cgal.so にもできる。2 コピーあると
	 *     片方で @c IO::set_mode してももう片方に効かず、**書き出しの形式が黙って変わる**
	 *     ⇒ 落ちないので気づけない。4 つの可変大域のうち *最も危険*。 */
	int read_file(const char *path);

	/* ★ #3535②: 三角形スープを **素の double / int** で取り出す (橋モジュールの入口)。
	 *   ⚠⚠ @c CGAL::to_double は **Lazy_exact_nt の精度設定 (可変な関数内 static) を読む**ので、
	 *     厳密座標→double の変換を op / 橋の TU でやると、その実体がそちらの .so にもできる。
	 *     ⇒ 変換はこちら側に閉じ込め、外へは素の配列だけ渡す。
	 *   xyz  … 3*nv 個 (頂点順は Surface_mesh の走査順)
	 *   tris … 3*nt 個の頂点番号 (三角形以外の面は落とす — 呼び手は三角形化済みを渡すこと)
	 *   ⚠ 厳密 → double は **情報が落ちる**。厳密なまま扱いたい側はこれを使わないこと。 */
	void to_soup(std::vector<double>& xyz, std::vector<int>& tris) const;
	/* ★ #3535②: 折れ線まわりの掃引立体 (tube)。生成器は共通 (srava_geo::make_tube_3d)、
	 *   CGAL の Surface_mesh へ積む部分と「外向き保証」(is_closed / volume / 反転) が CGAL 依存。
	 *   ⚠ @c CGAL::Polygon_mesh_processing の述語は **アサーション経由で
	 *     @c _error_handler / @c _error_behaviour の実体**を引き込む ⇒ op 側に置けない。
	 *   返りは srava_geo の TUBE_* 状態 (呼び手が自分の文言でエラーにする)。 */
	int build_tube(const std::vector<srava_geo::TubeV3>& P,
	               const std::vector<double>& R, int segs);
	/* ★ #3535②: 2D 領域を +Z へ h だけ押し出した角柱を作る (枠のアフィン当ては呼び手側)。
	 *   ⚠ 天/底キャップは **制約付き Delaunay (CDT)** で三角形化する。CGAL の三角形化は
	 *     述語のアサーションを通るので **@c _error_handler / @c _error_behaviour の実体**を
	 *     引き込む ⇒ op 側に置けない (理由は build_box の宣言のところ)。 */
	void build_extrude(sPtr<class cgMesh2D> in, double h);
	/* ★ #3535②: 断面を world Y まわりに回す (枠が既定のときの経路。置かれた 2D は
	 *   cg_revolve_placed が受ける)。返り 0 = 作れた / 1 = プロファイルの x が負。 */
	int build_revolve(sPtr<class cgMesh2D> in, int nPos, const std::vector<double>& cs,
	                  const std::vector<double>& sn, int full, int segs);
	virtual sPtr<cgMesh> op_union       (sPtr<cgMesh> b);
	virtual sPtr<cgMesh> op_intersection(sPtr<cgMesh> b);
	virtual sPtr<cgMesh> op_difference  (sPtr<cgMesh> b);
	virtual sPtr<cgMesh> op_combine     (sPtr<cgMesh> b);   /* 両 Surface_mesh を連結(corefinement なし) */
	virtual sPtr<cgMesh> apply_affine(const double e[12]);
	virtual sPtr<cgMesh> op_offset(double d, int subdiv);   /* 2D=straight skeleton / ★3D は nef へ移設 (#3440) */
	virtual double op_area();   /* 表面積(PMP::area・√含むので double) */
	virtual int    op_nverts();
	virtual int    op_nfaces();
	virtual int    op_vert(int i, double out[3]);   /* #3527: i 番目の頂点 (返り 3) */
	virtual int    op_verts(std::vector<double> &out);   /* #3527: 全頂点 (返り 3) */
	virtual double op_volume();      /* PMP::volume(閉メッシュの体積) */
	virtual double op_perimeter();   /* 3D は未定義(呼び元 dim ガード)→ 0 */
	virtual int    op_centroid(double out[3]);   /* 体積重心(四面体分割・発散定理) */
	virtual int    op_bbox(double mn[3], double mx[3]);   /* 全頂点走査の AABB(3D・返り 3) */
	/* 近接(3D 限定・binary)。farthest=false で最近接(AABB・両方向頂点-面の近似)、true で最遠(頂点ペア
	 * 総当り=厳密だが O(n·m))。pa/pb に各メッシュ上の点、返り=距離。3D-3D 専用(呼び元が cgMesh3D に d_cast)。 */
	double op_proximity(sPtr<cgMesh3D> b, bool farthest, double pa[3], double pb[3]);
	/* 肉厚解析(SDF=Shape Diameter Function)。各面で内向き錐状レイを飛ばし反対側壁までの距離=
	 * その場所の肉厚を測る(3Dプリント時の「薄くて割れる」箇所検出)。t_min 未満の面の重心+厚みを
	 * out へ flat に push(x,y,z,thk を 4 個ずつ)。返り=全面の最小肉厚(空/解析不能は 0)。
	 * rays=面ごとのレイ本数(計算時間にほぼ比例。少=粗く速い/多=滑らかで遅い)。
	 * レイ投射は EPICK コピー上で行う(EPECK 厳密レイは非現実的に重い=測定値なので double 近似で十分)。 */
	double op_thin_spots(double t_min, int rays, double cone_deg, std::vector<double>& out);
	virtual int          op_valid();   /* is_closed ∧ ¬does_self_intersect */
	virtual int          op_topology(int *nshells, int *nparts, int *genus);   /* #3514 */
	/* ★★ #3525: **順序つきの片リスト** (ひさ判断 2026-09-15)。
	 *   @partEnd_[i]@ = 片 i の面の終端 (片 i は面 @[partEnd_[i-1], partEnd_[i])@)。空 = 片リスト無し。
	 *   ⚠⚠ これを持たせた理由: 3D の voronoi は「セル i = サイト i」が **約束**なのに、
	 *     cgMesh3D は Surface_mesh を 1 つ持つだけで片の並びを明示的に持っていなかった。
	 *     面の連結成分の発見順に頼ると *実装依存の索引* (#3527) になり、上流の版や面の順で
	 *     黙って別の片を返しうる。⇒ **2D の regions_ と同じく、値が順序を実体として持つ**。
	 *   ★ 面は片ごとに連続して並べる規約 (codec が面の順を保つので往復しても崩れない)。
	 *   ⚠ 形を変える op (ブール・repair 等) は片リストを **引き継がない** (片はもうセルではない)。
	 *     アフィン変換だけは引き継ぐ (面も順序も動かないため)。 */
	std::vector<uint32_t>& parts()       { return partEnd_; }
	int  has_parts() const               { return ! partEnd_.empty(); }
	/* ★ #3525: i 番目の片 (0 始まり)。範囲外 / 取り出せない形は null + *why (文字列リテラル)。
	 *   片リストが無い値は **面の連結成分**に落ちる。⚠ ただし空洞 (符号つき体積が負のシェル) が
	 *   在ると入れ子が要るので明示エラー — op_topology が「数えるだけ」にしている理由と同じ。 */
	sPtr<cgMesh3D> op_part(int i, const char **why);
	/* ★ #3527: i 番目の **殻** (= 面の連結成分)。範囲外等は 0 を返し *why に理由。
	 * ★ part (塊) との違いは 1 点 — **空洞を断らない**。part は「値を分割する片」なので
	 *   入れ子 (どの空洞がどの塊のものか) が要るが、殻は *面の連結成分そのもの* なので
	 *   入れ子を知らなくても取り出せる。
	 * ★★ 向きは **そのまま返す** (案 A・ひさ裁定 2026-09-16)。⇒ 空洞の殻は法線が内を
	 *   向いたままなので volume() が **負**になり、**符号がそのまま「外殻か空洞か」の
	 *   判別子**になる。⚠ 向きを揃える案 (B) だと符号が消え、別に訊く手段が要る。
	 *   ★ さらに A なら **Σ 符号つき体積 == 全体の体積** が成り立つ (中空の箱 = 外殻 − 空洞)。 */
	sPtr<cgMesh3D> op_shell(int i, const char **why);
	/* ★ #3527: 点 p に **いちばん近い殻**。⇒ *位置で指す* 側 (occt の face_at と同じ形)。
	 * ★★ なぜ索引と 2 通り要るか: 索引は「列挙のため」のもので **指す先が無い**。
	 *   モデルの書き方を変えると殻の集合そのものが変わるので、番号は当然別の殻を指す。
	 *   ⇒ 書き換えても同じ殻を指し続けたいなら **位置で指すしかない**。
	 * ⚠ 同距離の殻が複数あるときは **明示エラー**。黙って片方を選ぶと
	 *   「同じ式に 2 通りの値」になる (#3516 / #3518-1 で潰してきた穴と同じ)。 */
	sPtr<cgMesh3D> op_shell_at(const double p[3], const char **why);
	/* ★ #3527 段 6 の続き: 3 つ組を **揃える**。@part@ / @shell@ / @shell_at@ は在ったのに
	 *   @part_at@ だけ無く、規約③「片の名前を付けたら 3 つ組で名乗る」を cgal 自身が
	 *   満たしていなかった (2026-09-18 に #3510 の表へ行を立てて判明: cgal だけ 3/4)。
	 * ★★ 意味は geomutils と **同じ** — 点を **含む** 塊 (いちばん近い塊ではない)。
	 *   立体は内側を持ち曲面は持たないので、@shell_at@ (最近傍) とは意図的に非対称。
	 *   ⇒ 空洞の中と立体の外は「そこに材料は無い」と明示エラー。
	 * ★ cgal は **厳密** に判定する (@Side_of_triangle_mesh@) ので、gu の巻き数 (double) より強い。 */
	sPtr<cgMesh3D> op_part_at(const double p[3], const char **why);
	/* ★ 面 i の **頂点番号** [i0,i1,i2]。番号は op_vert / op_verts と同じ列を指す。
	 *   返り 0 = 範囲外。⚠ 三角形以外の面は断る (3 頂点で名乗れないため)。 */
	int            op_face_verts(int i, int out[3]);
	virtual int          op_distance_at(const double p[3], double *out);      /* #3514 */
	virtual sPtr<cgMesh> op_repair();  /* PMP::autorefine(自己交差を幾何解消) */
	virtual sPtr<cgMesh> op_color(int r, int g, int b);   /* 全面に f:color を付けた新 mesh */
	virtual sPtr<cgMesh> op_section(const double P[3], const double N[3],
	                                int mode = 0, int *coplanarOut = 0);   /* 平面で切った 2D 断面 */
	virtual bool write_to(const char *path, const char *unit);   /* OFF/STL/OBJ/PLY(unit 無視) */
protected:
	void	decode_mfm3(cgChunkSource&);   /* MFM3 raw-double framing → EPECK Surface_mesh(無損失昇格) */
	bool	decode_nef3(cgChunkSource&);   /* NEF3(SNC) → EPECK Surface_mesh。表現不能なら false */
	cgMesh3DBox	*box_;   /* ★ #3545: CGAL の実体 (不透明)。ctor で確保・dtor で解放 */
	std::vector<uint32_t>	partEnd_;   /* ★ #3525: 順序つき片リスト (空 = 無し)。上の parts() 参照 */
	int	mfm3Input_ = 0;   /* 1 = decode() が MFM3 framing を読む(create_for_meta が MFM3 タグで立てる) */
	int	nef3Input_ = 0;   /* 1 = decode() が NEF3(SNC) を読む(create_for_meta が NEF3 タグで立てる) */
};

/* 2D 多角形領域(EPECK)。穴あき多角形の集合 = ブール演算結果(複数連結成分・穴)を表せる。
 * extrude/revolve で 3D(cgMesh3D)に持ち上がる。 */
class cgMesh2D : public cgMesh {
public:
	/* ★ #3535②: 2D の tube (折れ線を半径 r で太らせた帯領域・stamp-and-union)。
	 *   ⚠ @c CGAL::Polygon_set_2 は内部で arrangement を使い、**乱数の大域変数
	 *     (@c get_default_random) の実体**まで引き込む ⇒ op 側に置けない。 */
	void build_tube(const std::vector<srava_geo::TubeV3>& P,
	                const std::vector<double>& R, int segs);
	/* ⚠ #3545: 旧 @c Polygon_2 / @c Pwh_2 / @c Guide の入れ子 typedef は **撤去**
	 *   (cg/c++/cgMeshCgal.h へ)。⇒ 旧 @c regions() / @c guides() も同様に撤去し、
	 *     幾何 lib / 橋は @c cg_regions(*this) / @c cg_guides(*this) を使う。 */

	/* ⚠ #3545: ctor / dtor は **out-of-line** (cgMesh2D.cpp)。理由は cgMesh3D と同じ。 */
	cgMesh2D(sPtr<pigInfo> i = thNULL);
	virtual ~cgMesh2D();

	cgMesh2DBox&       box()       { return *box_; }
	const cgMesh2DBox& box() const { return *box_; }

	/* ★★ #3545 段 2: **閉じたリングを 1 本足す** (外周・穴なし)。circle / rect / ngon /
	 *   polygon の op が CGAL を引かずに済む入口。xy … 2*n 個 (局所座標)。
	 *   ⚠ 向きはこちらで CCW に直す (呼び手は並べるだけでよい)。 */
	void add_region_ring(const double *xy, int n);
	/* ★★ #3545 段 2: **リング列を包含関係で組んで足す** (import の SVG / DXF 経路)。
	 *   xy … 全リングの点を連結したもの ・ ringLen[i] … リング i の点数。
	 *   nest = 0 … 先頭が外周・残りは穴 (SVG の 1 <path>) /
	 *          1 … 包含の深さで nest (偶数=外周・奇数=直近外周の穴。DXF)。
	 *   返り = 足した region の数。⚠ 単純でないリングは落とす。 */
	int  add_regions_from_rings(const double *xy, const int *ringLen, int nrings, int nest);
	/* ★★ #3545 段 2: **ガイド (開ポリライン) を 1 本足す** (line の op)。xy … 2*n 個。
	 *   ⚠ 閉じない・向きも直さない (塗りでなく SVG/DXF のストロークで描くだけ)。 */
	void add_guide(const double *xy, int n);
	/* ★★ #3545 段 2: **中身 (regions / guides) を丸ごと複製する** (cast が名乗りだけ下げる経路)。
	 *   ⚠ 枠 / placed_ は複製しない — 呼び手が「何を引き継ぐか」を明示すること。 */
	void copy_contents_from(sPtr<cgMesh2D> src);

	virtual sPtr<stdString> get_str();
	virtual const char* meta_tag()  { return "PLY2"; }
	/* ★★ #3533: **2D 領域の型は 2 つ**。どちらも実体は cgMesh2D・4CC も PLY2 のまま
	 *   (ptCloud が pt-cloud2d / pt-cloud3d を 1 クラスで名乗るのと同じ形)。
	 *     cg-cross2d  z=0 の **簡易表現**   … 生成したまま / cast で降ろしたもの
	 *     cg-face3d   空間に置かれた一般表現 … transform / section を通ったもの
	 *   ⚠⚠ 出し分けの根拠は @frame_is_default()@ では **なく** @placed_@。理由:
	 *     @rotate(rect,"z",90)@ は *平面を動かさない* ので枠は既定のままだが、規約①
	 *     (transform は常に face3d を返す) により **型は face3d** になる。枠で出し分けると
	 *     ここで型スタンプ (sig が決める) と値の名乗りが食い違う。⇒ 別のビットで持つ。
	 *   ★ @frame_is_default()@ は依然として **幾何の述語** (本当に z=0 に居るか) で、
	 *     規約② の cast (降格) の可否はこちらで見る。2 つは役割が違う。 */
	virtual const char* type_name() { return placed_ ? "cg-face3d" : "cg-cross2d"; }   /* rev4 実装型名 (PLY2 は 2 型で共有) */
	int  is_placed() const { return placed_; }
	void set_placed(int p) { placed_ = p ? 1 : 0; }
	virtual uint16_t    repr_type() { return 32; }
	virtual int         dim()       { return 2; }
	virtual void encode(cgChunkSink&);
	virtual void decode(cgChunkSource&);
	/* ★ Manifold 2D(MFC2)キャッシュを cgMesh2D へ **無損失昇格**(#3404・cgMesh3D::decode_mfm3 の 2D 版)。
	 *   create_for_meta が "MFC2" を検出→ set_mfc2_input()→ decode() が MFC2 の raw-double リング列を
	 *   Pwh_2(外周 CCW=正面積/穴 CW=負面積を包含判定で紐付け)へ再構成。cg agent が Manifold 2D を
	 *   透過的に入力できる(混成 2D combine・SVG/DXF 出力を CGAL 側で処理するため)。 */
	void set_mfc2_input() { mfc2Input_ = 1; }
	virtual sPtr<cgMesh> op_union       (sPtr<cgMesh> b);
	virtual sPtr<cgMesh> op_intersection(sPtr<cgMesh> b);
	virtual sPtr<cgMesh> op_difference  (sPtr<cgMesh> b);
	virtual sPtr<cgMesh> op_combine     (sPtr<cgMesh> b);   /* 両領域の Pwh をそのまま集める(ブールなし) */
	virtual sPtr<cgMesh> apply_affine(const double e[12]);
	virtual sPtr<cgMesh> op_offset(double d, int subdiv);   /* straight skeleton(subdiv 無視) */
	virtual double op_area();   /* 囲み面積(Polygon::area・外周−穴。exact→double) */
	virtual int    op_nverts();
	virtual int    op_nfaces();   /* 2D は面を持たない = 0 */
	virtual int    op_vert(int i, double out[3]);   /* #3527: i 番目の点 (枠の中の x,y・返り 2) */
	virtual int    op_verts(std::vector<double> &out);   /* #3527: 全点 (枠の中の x,y・返り 2) */
	virtual double op_volume();      /* 2D は体積なし(呼び元 dim ガード)→ 0 */
	virtual double op_perimeter();   /* 境界長(全 region の外周+穴。√→double) */
	virtual int    op_centroid(double out[3]);   /* 面積重心(shoelace モーメント・穴は負寄与) */
	virtual int    op_bbox(double mn[3], double mx[3]);   /* 外周頂点走査の AABB(2D・返り 2) */
	virtual int          op_valid();   /* 全 region の外周/穴が is_simple */
	/* ★★ #3525: 2D は **塊の数だけ**答える (nshells / genus は持たない ⇒ 0 のまま・sig も 3D だけ)。
	 *   ⚠⚠ 「塊 = 点集合の連結成分」ではない — 既存の約束は最初からそうではなかった:
	 *     nef の nparts は **marked volume の数**なので @convex_decomposition@ の結果は
	 *     *1 つの連結な立体*でも凸片の数を返す / cgal 3D は **符号つき体積が正のシェル**の数なので
	 *     接しているだけの 2 立体は 2 になる。⇒ 約束は「**その値が構造として持っている片の数**」で
	 *     一貫しており、2D をこれに合わせるのは *広げる*のではなく *揃える* (ひさ判断 2026-09-15)。
	 *   ⇒ Voronoi のようにセルが互いに接する分割も、仕切りを実体 (regions()) として持つ値なので
	 *     そのまま @nparts@ / @part@ が索引になる (#3527 ①)。
	 *   ★ 返り 0 (= 閉じていない) は従来どおり。2D に「閉じている」は無く、genus の門はこれで閉じる。 */
	/* ⚠ #3545: 箱 (regions) を触るので **out-of-line** (cgMesh2D.cpp)。 */
	virtual int          op_topology(int *nshells, int *nparts, int *genus);
	/* ★ #3525: i 番目の片 (0 始まり)。範囲外は null (呼び手が明示エラー)。枠と placed_ は引き継ぐ。 */
	sPtr<cgMesh2D> op_part(int i);
	/* ★ #3527: 点を **含む** 片。⚠ 点は **world** で受ける (face3d は平面上にあることを要求)。
	 *   vert / verts / bbox / centroid が #3533 / 段 5 で world に揃ったのと同じ約束。
	 * 返り: >=0 片の番号 / -1 含まない / -2 2 つ以上が含む / -3 平面上に無い。 */
	int            op_part_at(const double p[3]);
	virtual int          op_distance_at(const double *, double *) { return 0; }   /* 2D は対象外 (#3514) */
	virtual sPtr<cgMesh> op_repair();  /* Polygon_repair::repair(even-odd) */
	virtual sPtr<cgMesh> op_color(int r, int g, int b);   /* 2D は非対応(null=エラー) */
	virtual sPtr<cgMesh> op_section(const double[3], const double[3],
	                                int = 0, int * = 0) { return sPtr<cgMesh>(); }   /* 2D は断面なし */
	virtual bool write_to(const char *path, const char *unit);   /* SVG(width/height)/DXF($INSUNITS) */

	/* ★★ #3526: 2D が居る **平面 (枠)**。局所座標 (x,y) → O + xU + yV。
	 *   既定 O=(0,0,0) U=(1,0,0) V=(0,1,0) = z=0 平面 ⇒ *既存の式は一切変わらない*。
	 *   manifold の @mfCross@ と **同じ設計・同じ関数名**にしてある (片方だけ直すのを防ぐため)。
	 *
	 *   ★★ **多角形の座標は局所のまま**にする。ブール・面積・周長・offset・repair は全部
	 *     局所座標 (厳密な K2) で回っているので、そこに枠を持ち込むと全部を見直すことになる。
	 *     ⇒ 枠を足す意味は「**z 成分の行き先を作る**」ことだけ。
	 *   ⚠ 枠は **正規直交に保つ** (U ⊥ V ・どちらも単位)。歪みは局所座標が持つ。
	 *     非等方な写像は長さを変えるので、歪みを枠に置くと perimeter / offset が壊れる。
	 *   ⚠ 枠は **double** で持つ (局所座標は厳密な有理数のまま)。正規化に sqrt が要るので
	 *     枠を厳密にはできない。⇒ *平面の上での計算は厳密・平面の置き場所は double*。 */
	const double* frame_o() const { return fo_; }
	const double* frame_u() const { return fu_; }
	const double* frame_v() const { return fv_; }
	void set_frame(const double o[3], const double u[3], const double v[3]) {
		for ( int i = 0 ; i < 3 ; ++i ) { fo_[i] = o[i]; fu_[i] = u[i]; fv_[i] = v[i]; }
	}
	/* 既定の枠 (z=0 平面) か。★ 既定なら codec に枠の節を **書かない** =
	 *   既存のキャッシュ blob とバイト単位で同じになる。 */
	int frame_is_default() const {
		return ( fo_[0]==0 && fo_[1]==0 && fo_[2]==0
		      && fu_[0]==1 && fu_[1]==0 && fu_[2]==0
		      && fv_[0]==0 && fv_[1]==1 && fv_[2]==0 ) ? 1 : 0;
	}
	/* 枠が同じか (ブールの前提)。★ 違う平面に居る 2 つの 2D の交わりは **線分以下**に落ちるので
	 *   2D 領域として表せない ⇒ 呼び手が明示エラーにする (#3526 ②・ひさ判断)。
	 *   ⚠ 同一平面でも軸の取り方が違えば局所座標が食い違う。いまは *枠が一致すること* を求める。 */
	int same_frame(const double o[3], const double u[3], const double v[3]) const {
		const double EPS = 1e-12;
		for ( int i = 0 ; i < 3 ; ++i ) {
			double d0 = fo_[i]-o[i], d1 = fu_[i]-u[i], d2 = fv_[i]-v[i];
			if ( (d0<0?-d0:d0) > EPS || (d1<0?-d1:d1) > EPS || (d2<0?-d2:d2) > EPS ) return 0;
		}
		return 1;
	}
	/* ★★ #3526: **同じ平面か** (軸の取り方は違ってよい)。法線が平行 (符号は問わない) で、
	 *   相手の原点がこちらの平面に載っていれば同じ平面。
	 *   ⇒ @same_frame@ が偽でもここが真なら **表し直せる** (reexpress)。 */
	int same_plane(const double o[3], const double u[3], const double v[3]) const;
	/* ★★ #3526: **相手の枠で表し直す**。幾何は 1 ミリも動かさず、局所座標の取り方だけを
	 *   相手に合わせる。同じ平面でなければ null。⚠ manifold の @mfCross::reexpress@ と対。
	 *   ⚠ 枠が完全に一致しているときは呼ばない (呼び手が先に same_frame を見る)。 */
	sPtr<cgMesh2D> reexpress(const double o[3], const double u[3], const double v[3]) const;
	/* ★★ #3534: **project_flatten** — world 座標の (x,y) をそのまま取り、z を捨てる。
	 *   ⇒ 結果は world 幾何だけで決まる = **経路非依存** (枠の取り方に依らない)。
	 *   ★ 局所 (x,y) → world → z を捨てる、は平面上の **2x2 アフィン**に畳める:
	 *       x' = Ox + x*Ux + y*Vx        y' = Oy + x*Uy + y*Vy
	 *     ⇒ 新しい枠は既定 (z=0)・型は cross2d へ落ちる (placed_=0)。
	 *   ⚠ det = Ux*Vy - Uy*Vx = (U x V)・ẑ = 法線の z 成分。**0 = 平面が world +Z を含む**
	 *     ⇒ 潰れるので null (呼び手が明示エラー)。★ extrude の既存検査と **同じ判定式**。
	 *   ★ 平面が XY と平行なら等長 (面積・周長は不変)・傾けば cosθ で縮む (影として正しい)。
	 *   ★ 平面上で階数 2 のアフィンは単射なので、単純多角形は単純のまま = **正規化不要**。
	 *   ⚠ 枠が double なので、**傾いた平面では局所座標が厳密でなくなる** (枠を double で持つ
	 *     という #3526 の決定の帰結)。XY と平行なら U/V の成分は 0/±1 なので厳密に保たれる。
	 *   ⚠ mfCross::project_flatten と対。**片方だけ直さないこと**。 */
	sPtr<cgMesh2D> project_flatten() const;

	/* 局所座標 (x,y) → 世界座標。 */
	void to_world(double x, double y, double w[3]) const {
		for ( int i = 0 ; i < 3 ; ++i ) w[i] = fo_[i] + x*fu_[i] + y*fv_[i];
	}
protected:
	void	decode_mfc2(cgChunkSource&);   /* MFC2 raw-double リング列 → Pwh_2(無損失昇格) */
	/* ★ #3533: 型が face3d か (上の type_name を参照)。⚠ 枠とは独立したビット。
	 *   codec は「枠の節を書いたか」で持つ (節があれば face3d)。 */
	int	placed_ = 0;
	/* ★ #3526: 枠。既定 = z=0 平面。⚠ ここを増やすと **ABI** (v26 で上げた)。 */
	double	fo_[3] = {0,0,0};
	double	fu_[3] = {1,0,0};
	double	fv_[3] = {0,1,0};
	cgMesh2DBox	*box_;   /* ★ #3545: CGAL の実体 (regions / guides)。ctor で確保・dtor で解放 */
	int	mfc2Input_ = 0;   /* 1 = decode() が MFC2 framing を読む(create_for_meta が MFC2 タグで立てる) */
};

/* ★ #3511: 凸包 hull(a[,b,…]) — **1 個以上**を受け、与えた形すべての凸包を返す (cgHull.cpp)。
 *   ブールではないので corefinement を通らず、**点しか見ない**。⇒ 入力が閉じている必要も
 *   自己交差していないことも要らないかわりに、穴も凹みも消える (情報を落とす op)。
 *   3D/2D の振り分けはこの関数が持つ。失敗時は null + *errmsg。 */
sPtr<cgMesh> cg_hull_from_args(sArray<sPtr<pigData> > *args, const char **errmsg);

/* ★★ #3525: Voronoi 図 — @voronoi(p, box)@ (cgVoronoi.cpp)。2D 点群 → セルの列。
 *   ★ **Delaunay を経由しない** — セルの定義 (垂直二等分線による半平面の共通部分) の
 *     とおりにクリップ箱を削って作る。⇒ 外心を計算しないので、双対で作ると出る問題
 *     (遠方の外心・退化・並べ替えでサイト順が壊れる) が起きない。
 *   ★★ **regions()[i] は サイト i のセル**。サイトごとに独立のループなので構造的に保たれる
 *     ⇒ @nparts@ / @part@ (2D) がそのまま索引になる。
 *   ⚠ 箱は省略できない・サイトは箱の中・重複サイトは明示エラー (わけは cgVoronoi.cpp 冒頭)。
 *   失敗時は null + **呼び手のバッファ** @err@ にわけ (@errsz@ バイトまで)。
 *   ⚠⚠ 理由を **モジュール大域に溜めない** (ひさ指示 2026-08-26・@010c39f@) — in-proc で
 *     複数 op が同居すると混線するため。#3525 の初版は static に溜めていて
 *     @srava_no_mutable_static@ が赤くなった (2026-09-16)。 */
sPtr<cgMesh2D> cg_voronoi_2d(sPtr<class ptCloud> cloud, const double bmin[2], const double bmax[2],
                             char *err, int errsz);

/* ★ #3525: Delaunay 三角形分割 — @delaunay(p)@ (cgDelaunay.cpp)。2D 点群 → 三角形の列。
 *   ★★ voronoi の **前段ではない** (あちらは二等分線の切り取りで直接作る)。独立した op。
 *   ⚠⚠ 三角形の **番号は実装依存** ⇒ 「i 番目の三角形」を約束に使わないこと (#3527)。
 *   ⚠ 1 直線上に並んだ点は明示エラー (次元が落ちて面が 0 枚になるのを黙って返さない)。 */
sPtr<cgMesh2D> cg_delaunay_2d(sPtr<class ptCloud> cloud, char *err, int errsz);

/* ★ #3525: Delaunay 分割 (3D) — 四面体を **1 つのメッシュの片**として並べて返す (cgDelaunay.cpp)。
 *   ★ 順序つき片リストが入ったので「四面体は新しい表現クラスが要る」という見送り材料は消えた。
 *   ⚠ 同一平面の入力は **エラーにしない** — 次元 2 の正しい三角形分割を平たい片として返す。
 *   ⚠⚠ 片の番号は **実装依存** (#3527)。 */
sPtr<cgMesh3D> cg_delaunay_3d(sPtr<class ptCloud> cloud, char *err, int errsz);

/* ★★ #3525: Voronoi 図 (3D) — @voronoi(p, box)@ の 3D。@cg-mesh3d@ を返す (cgVoronoi.cpp)。
 *   ★ これを素直に入れられることが、voronoi を delaunay から切り離した **目的** (#3525 の 5 節)。
 *     危険 (四面体という新しい表現クラス・最悪 O(N^2)) は delaunay 側に閉じ込められている。
 *   ★★ 索引は **順序つき片リスト** (cgMesh3D::parts()) が持つ ⇒ 「片 i = サイト i」が
 *     面の連結成分の発見順に依らず *値の構造* として決まる。 */
sPtr<cgMesh3D> cg_voronoi_3d(sPtr<class ptCloud> cloud, const double bmin[3], const double bmax[3],
                             char *err, int errsz);

/* ★★ #3511: 線織 loft — @loft_ruled(断面, 断面, …)@ (cgLoft.cpp)。断面の列を直線で結ぶ立体。
 *   ★★ 対応づけの規約は **manifold の @mf_loft_ruled_from_args@ と同一** (弧長で正規化 +
 *     全断面の頂点の和集合で標本化 + 最近点で始点合わせ + 巡回の向きを軸に揃える)。
 *     ⚠ **片方だけ直さないこと** — 同じ式が 2 つのカーネルで別の形になる。
 *   ★ 弧長は double・座標は厳密 (標本が頂点に乗れば動かさず、稜の途中は有理数の比で内分)。
 *   ★ なめらかな @loft@ は置かない (解析曲面が要る = occt だけ)。
 *   失敗時は null + *errmsg (数を含む文言は errbuf に組む・呼び出しのローカルを渡すこと)。 */
sPtr<cgMesh> cg_loft_ruled_from_args(sArray<sPtr<pigData> > *args, const char **errmsg,
                                     char *errbuf, int errbufsz);

/* ★★ #3526: **空間に置かれた 2D を world Y 軸まわりに回す** (cgLoft.cpp)。
 *   ★ 軸を world Y に固定するのは ひさ判断 (extrude の「world +Z のまま」と同じ論法)。
 *     occt / manifold と同じ規約。⚠ 骨は @mf_revolve_placed@ と同一 — 片方だけ直さないこと。
 *   ⚠ 枠が既定のときはこれを **通さない** (従来の CDT 掃引のまま = 既存の値が動かない)。
 *   ⚠ 断面が軸をまたぐ / 軸が断面を貫く場合は明示エラー (黙って体積 0 を返さない)。 */
sPtr<cgMesh> cg_revolve_placed(sPtr<cgMesh2D> in, double angle, int nseg,
                               const char **errmsg, char *errbuf, int errbufsz);

/* ★ #3526 + #3479 の作法: ブールが null を返したとき、**理由を言い分ける**ために使う。
 *   1 = 2D どうしだが **平面が本当に違う** (表し直せない) / 0 = それ以外。
 *   ⚠ これが無いと「枠が違う」も「ブールが失敗した」も同じ文言になり、利用者は
 *     *閉じた立体が触れ合っている* という無関係な説明を読むことになる (2026-09-13 に実測)。 */
static inline int cg_2d_planes_differ(sPtr<cgMesh> a, sPtr<cgMesh> b) {
	sPtr<cgMesh2D> a2 = sPtr<cgMesh2D>::d_cast(a), b2 = sPtr<cgMesh2D>::d_cast(b);
	if ( ! a2.is_notNull() || ! b2.is_notNull() ) return 0;
	return a2->same_plane(b2->frame_o(), b2->frame_u(), b2->frame_v()) ? 0 : 1;
}
/* ★ #3512: 三角形の張り方を作り直す 2 本 (cgRemesh.cpp)。狙いは逆向き —
 *   remesh は**形を保ったまま三角形の質を上げ** (面数は増える)、simplify は
 *   **形を保ったまま面数を落とす** (三角形の質は下がる)。⇒ 対で使う。
 *   ⚠ どちらも **EPICK (double) のコピー上**で解いて EPECK へ厳密に戻す。新しい頂点位置を
 *     決める op なので厳密性は原理的に無く、CGAL 側も浮動小数前提 (LindstromTurk は
 *     EPECK ではコンパイルが通らない)。理由の詳細は cgRemesh.cpp の冒頭。
 *   失敗時は null + *errmsg。3D 専用 (呼び元が cgMesh3D へ d_cast 済み)。 */
sPtr<cgMesh> cg_remesh_3d(sPtr<cgMesh3D> in, double len, int iter, double sharp, const char **errmsg);
/* ★ refine は **EPECK のまま**。新しい頂点が有理数の重心座標で書けるので形が bit 単位で不変
 *   (部分三角形が元と相似 = 最小角も変わらない)。remesh/simplify と違い *質に手を触れない*。 */
sPtr<cgMesh> cg_refine_3d(sPtr<cgMesh3D> in, double len, const char **errmsg);
sPtr<cgMesh> cg_simplify_3d(sPtr<cgMesh3D> in, int nfaces, const char **errmsg);

/* ★★ #3545 段 2: 点群の法線推定 (PCA + MST 向き付け・cgEstimateNormals.cpp)。
 *   ⚠ cgal で **唯一 EPICK を直に使う**ところなので、op には置かず幾何 lib に閉じ込める
 *     (点集合処理の述語が @c _error_handler の実体を引き込む)。
 *   xyz … 3*np 個 ・ k … 近傍数 (呼び手が 1..np-1 に丸めること)。
 *   返り 0 = 成功 (*oriented に 1=全点が向き付いた / 0=一部残った)。
 *   返り 1 = CGAL が例外を投げた (err に上流の文言。呼び手が自分の接頭辞を付ける)。 */
int cg_estimate_normals(const double *xyz, int np, int k,
                        std::vector<double>& outXyz, std::vector<double>& outNrm,
                        int *oriented, char *err, int errsz);

/* ★ #3475: エラー文言に名乗るモジュール名 (記述子の .name と同じ)。 */
#define CG_MODULE_NAME	"cgal"

/* ★ #3475: このモジュール専用のエラー生成子。文言は "[TAG] <name>/op: message" になる。
 *   素の cga_err(...) を使うとモジュール名が付かない。 */
PIG_DEFINE_MODULE_ERR(cga_err, CG_MODULE_NAME)

#endif /* CG_MESH_H */
