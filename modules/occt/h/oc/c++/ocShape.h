#ifndef OC_SHAPE_H
#define OC_SHAPE_H
/*
 * ocShape — Open CASCADE Technology (OCCT・LGPL-2.1 + 例外) の TopoDS_Shape を pigData で
 * ラップした値ハンドル (#3437 P5)。vdGrid / ggMesh / nfMesh のミラーだが、★**表現クラスが違う**。
 *
 *  - これは **B-rep (境界表現)**。位相 (TopoDS: Vertex/Edge/Face/Shell/Solid) と
 *    幾何 (Geom: 平面・円柱・円錐・球・トーラス・NURBS) が**分離**していて、
 *    Face = 「無限に広がる曲面 + その (u,v) 上のトリム境界」として持つ。
 *    ★**円筒の側面は 1 面**であって三角形の集合ではない。分割数という概念が無い。
 *  - ★ ただし「解析曲面」は **任意の f(x,y,z)=0 が書ける**という意味ではない。持てるのは
 *    平面・円柱・円錐・球・トーラス + NURBS という**カタログ**で、代数曲面や対称性を記号的に
 *    扱う仕組みは無い。ACIS / Parasolid と同じ **CAD の B-rep 系譜** = 従来のソリッドモデラの
 *    ブール演算系の延長 (ひさの整理・2026-08-19)。
 *
 * ★★ 他カーネルとの決定的な違い: **不変条件が「厳密な曲面」で、それが演算で目減りする**
 *   - 葉 (プリミティブ) では面が厳密 — 球は**厳密な球**であって内接多面体ではない
 *   - ブールは曲面どうしの交線を要るが、円柱×円柱の交線は 4 次の空間曲線で入力と同じクラスに
 *     入らない → **B-spline で近似**される。しかも頂点・稜・面が**トレランス**を属性に持ち、
 *     判定は「厳密」ではなく「許容誤差以内か」で行う
 *   - よって**根に向かうほど厳密さが目減りする**。CGAL (どこでも厳密) とは対照的
 *   ★ この構造は openvdb と**同型**: あちらもブール後は真の距離場でなくなり levelSetRebuild が
 *     要る。OCCT では shape healing がそれに当たる (→ [[openvdb-volume-module-p2]])
 *
 * ★ offset にとって特別な理由 (#3437 を offset の評価軸に入れたい動機):
 *   解析曲面のオフセットは多くの場合また解析曲面になる (平面→平面・半径 r の円柱→r+d の円柱)。
 *   面は厳密にオフセットでき、仕事は稜 (円筒パッチ) と頂点 (球パッチ) の埋め合わせになる。
 *   これは **Steiner の公式 V + A·d + M·d² + (4/3)πd³ を構成的にやっている**のと同じで、
 *   nef (近似球との Minkowski 和) / openvdb (格子の等値面移動) とは**第 3 の原理**になる。
 *
 * ★ **mesh からの入口は作らない**。メッシュ → B-rep は「三角形ごとに平面 Face」になり、
 *   解析曲面という利点が消えた巨大な B-rep ができるだけ。OCCT は**生成する**カーネルであって
 *   汚い入力を直すカーネルではない。出口 (triangulate) だけを持つ。
 *
 * cache 形式 (D_META 4CC "BREP"):
 *   [u64 nbytes][BinTools::Write が書いたバイナリ BREP]
 *   ★ 長さ接頭辞の理由は vd と同じ — chunk Source に「残り全部」を取る手段が無いため。
 *   中身は OCCT ネイティブ。中立形式を自前定義しないのも vd と同じ理由 (読み手が居ない)。
 */
#include	"pig/c++/pigData.h"
#include	"pig/c++/pigModuleError.h"   /* #3475: 自分の名前でエラーを作る */
#include	"pig/c++/pigOpEntry.h"   /* pigWireClass (配線先) */
#include	<TopoDS_Shape.hxx>
#include	<stdint.h>
#include	<vector>

#define OC_MODULE_NAME	"occt"
#define OC_TYPE		"oc-brep3d"
#define OC_TAG		"BREP"
/* ★ #3471: **2D 領域** (平面上の TopoDS_Face)。OCCT の TopoDS は常に 3D なので、「2D 領域」は
 *   平面上の Face (edge が Geom_Plane 上に pcurve を持つ) として表す。
 *   ⚠ 「occt に 2D が無い」は正確には **2D の位相が無い**という意味で、曲線側 (Geom2d_BezierCurve /
 *     Geom2d_BSplineCurve / GCE2d_* / Geom2dAPI_*) は揃っている。
 *   ★ 4CC は BREP と分ける — 中身は同じ BinTools のバイナリだが、**型が違えば形式も分ける**
 *     (同じタグにすると reader が 2D と 3D を見分けられない)。 */
#define OC2_TYPE	"oc-cross2d"
#define OC2_TAG		"BRP2"

/* ★ OC_MESH_TYPE / OC_MESH_TAG は **撤去した** (akira-project #3452)。
 * occt.so は mesh 型を一切名乗らない (oc-brep3d だけ)。B-rep → メッシュの出口は
 * 境界モジュール occt_mf.so が持ち、そこで **本物の mfMesh** を作る。 */

/* codec の Sink/Source 抽象 (vdChunkSink/ggChunkSink と同シグネチャ)。 */
struct ocChunkSink   { virtual void chunk(const uint8_t *data, int n) = 0; virtual ~ocChunkSink()   {} };
struct ocChunkSource { virtual void pull (uint8_t *dst, int n)        = 0;
                       virtual int  more()                            { return 1; }
                       virtual ~ocChunkSource() {} };

/* ---- 抽象基底: reader/writer が扱う多態ハンドル ---- */
class ocGeom : public pigDataWireTyped {
public:
	ocGeom(sPtr<pigInfo> i = thNULL) : pigDataWireTyped(i) {}
	virtual const char* meta_tag()  = 0;
	virtual uint16_t    repr_type() = 0;
	virtual int         dim()       = 0;
	virtual void encode(ocChunkSink&)   = 0;
	virtual void decode(ocChunkSource&) = 0;
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
	static sPtr<ocGeom> create_for_meta(const uint8_t *meta, int len);

	/* ★ 2026-08-28 (ABI v12): **この階層への配線先**。op の OPS 行が OPWIRE(Calc, ocGeom) と
	 *   書くと、引数はこの WIRE 経由で実体化される。create_for_meta が 4CC を受理判定し、
	 *   mkReader がこの階層の stream reader を起こす。定義は ocCacheCodec.cpp。 */
	static const pigWireClass WIRE;
};

/* ---- B-rep ソリッド ---- */
class ocShape : public ocGeom {
public:
	ocShape(sPtr<pigInfo> i = thNULL) : ocGeom(i) {}

	TopoDS_Shape&       shape()       { return s_; }
	const TopoDS_Shape& shape() const { return s_; }
	void set_shape(const TopoDS_Shape &s) { s_ = s; }

	virtual sPtr<stdString> get_str();

	virtual const char* meta_tag()  { return OC_TAG; }
	virtual const char* type_name() { return OC_TYPE; }
	virtual uint16_t    repr_type() { return 128; }   /* B-rep (mesh でも距離場でもない) */
	virtual int         dim()       { return 3; }

	virtual void encode(ocChunkSink&   sink);
	virtual void decode(ocChunkSource& src);
	virtual bool write_to(const char *path, const char *unit);

	/* ★ #3436 P4: n 項ブール。BRepAlgoAPI_* は SetArguments/SetTools で **リスト**を取れる
	 *   (BOPAlgo_Builder が n 個をまとめて 1 回の交差計算で処理する)。上限なし。
	 *   kind = "union" / "intersection" / "difference" (差は ops[0] から残り全部を引く = 左 fold)。 */
	static sPtr<ocShape> op_bool_nary(sArray<sPtr<ocShape> >& ops, const char *kind, char *err = 0, int errsz = 0);
	/* planner から届いた引数配列の入口 (2 項は従来の二項 API のまま)。失敗は null + *errmsg。 */
	static sPtr<ocShape> bool_from_args(sArray<sPtr<pigData> > *args, const char *kind,
	                                    const char **errmsg, char *errbuf = 0, int errbufsz = 0);

	/* ---- ブール (BRepAlgoAPI)。失敗は null を返す (OCCT は「作れない」で失敗しうる) ---- */
	sPtr<ocShape> op_union(sPtr<ocShape> b, char *err = 0, int errsz = 0);
	sPtr<ocShape> op_intersection(sPtr<ocShape> b, char *err = 0, int errsz = 0);
	sPtr<ocShape> op_difference(sPtr<ocShape> b, char *err = 0, int errsz = 0);

	/* ---- ★ 解析曲面を直接オフセット (BRepOffsetAPI_MakeOffsetShape) ---- */
	sPtr<ocShape> op_offset(double d, char *err = 0, int errsz = 0);

	/* ---- ★ **B-rep でしか書けない加工** (#3437) ----
	 * fillet = 稜を半径 r の転がり球で丸める / chamfer = 稜を距離 d で 45 度に削ぐ。
	 * どちらも **全ての稜**に一律に適用する (部分適用は稜の選択語彙が要るので将来)。
	 * ★ メッシュ系にこれが無いのは偶然ではない — 転がり球の接触軌跡は解析曲面
	 *   (円筒・球・トーラス) であって、三角形分割では**定義そのものが近似になる**。
	 * 失敗は null (OCCT は自己交差する半径などで普通に失敗する)。 */
	sPtr<ocShape> op_fillet(double r, char *err = 0, int errsz = 0);
	sPtr<ocShape> op_chamfer(double d, char *err = 0, int errsz = 0);

	/* ★ #3461: 一般アフィン変換 (行優先 3x4 = m00..m23)。
	 *   translate / rotate / scale / mirror / transform の **共通の入口**。
	 *   manifold の apply_affine(double[12]) と同じ形にしてある (呼び出し側を揃えるため)。
	 *
	 *   ⚠ 線形部が「直交 × 一様スケール」なら gp_Trsf を使う。この場合 **解析曲面が
	 *   解析曲面のまま**運ばれる (球は球のまま)。そうでなければ gp_GTrsf へ落ちる。
	 *   後者では OCCT が曲面を BSpline へ変換するので、厳密な球面という性質は失われる
	 *   (形は正しい)。非等方 scale や剪断を指定した時点で避けられない。 */
	sPtr<ocShape> op_affine(const double e[12], char *err = 0, int errsz = 0);

	/* ★ 入口: STEP / BREP を読む。**mesh → B-rep ではない** (どちらも解析曲面を
	 * そのまま持つ形式なので、読むだけで B-rep が手に入る)。失敗は null。 */
	static sPtr<ocShape> read_file(const char *path);

	double volume() const;      /* BRepGProp::VolumeProperties (厳密な曲面のまま積分) */
	int    nfaces() const;      /* Face の数。★三角形数ではない (円筒の側面は 1 面) */
	int    nverts() const;      /* Vertex の数。★稜の端点であって三角形の頂点ではない */
	int    nsolids() const;

	/* ---- 素性を訊く op (#3487) ----------------------------------------------
	 * ★ **B-rep のまま**測る (三角形に落とさない)。bbox は Bnd_Box・重心と面積は GProp。
	 *   球なら表面積 4πr² がそのまま出る (mesh 系の内接多面体とは構造的に違う値)。
	 * ★ valid の定義は 7 カーネル共通で ① 空でない ∧ ② 閉じている ∧ ③ 自己交差が無い
	 *   (src/h/common/meshprops.h の冒頭)。occt では BRepAlgoAPI_Check が ②③ をまとめて
	 *   答える (BRepCheck_Analyzer のトポロジ/幾何検査 + BOPAlgo_CheckerSI の自己交差検査)。 */
	int    op_bbox(double mn[3], double mx[3]) const;
	int    op_centroid(double c[3]) const;
	double op_area() const;
	int    op_valid() const;

	/* ★★ プロセスに 1 回だけ: **OCCT の診断出力を stdout から追い出す** (#3437)。
	 *
	 *   OCCT の Message_PrinterOStream は既定で **std::cout** へ書く。ところが agent の
	 *   **stdout は pigwire そのもの**なので、OCCT が 1 行でも出すとワイヤが壊れ、planner から
	 *   「agent closed unexpectedly」に見える。STEP ライタは既定で
	 *     "Statistics on Transfer (Write)" / "Step File Name : … Write Done"
	 *   を必ず出すので、**STEP export が毎回 agent を殺していた** (2026-08-20 に実際に踏んだ)。
	 *   ファイル自体は完全に書けているので「書けているのに失敗する」という分かりにくい形になる。
	 *
	 *   ★ 一般則: **stdout に何か書くライブラリをリンクしたモジュールは、必ずこれをやる。**
	 *   幾何が正しくても、プロセス実行の実行体では出力チャネルを壊す。
	 *   全ての op の入口で呼ぶ (openvdb の openvdb::initialize と同じ作法)。 */
	static void ensure_init();

private:
	TopoDS_Shape s_;
};

/* ★ ocMesh (旧: 出口で作る三角形メッシュ) は **撤去した** (akira-project #3452)。
 * 「新しい型を作らない」方針に対し、型名だけ mf-mesh3d を借りた別クラスだったため:
 *   ① in-proc で d_cast<mfMesh> が失敗する
 *   ② codec が (MFM3 -> mf-mesh3d) の読み手としても名乗り、遅延ロード (#3452) で
 *      登録順が module() 順になった結果、本家 manifold より先に選ばれて下流が壊れた
 * → triangulate は境界モジュール **occt_mf.so** へ移し、そこで **本物の mfMesh** を作る
 *   (openvdb + openvdb_mf と同じ構図)。occt.so は oc-brep3d だけを扱う。
 */


/* ---- ★ #3471: 2D 領域 (平面上の TopoDS_Face)。text() の出力・extrude/revolve の入力 ----
 *   ocShape と **中身の持ち方は同じ** (TopoDS_Shape + BinTools のバイナリ) だが、
 *   型名と 4CC を分ける。輪郭は Bezier / B-spline のまま保たれる。 */
class ocFace2D : public ocGeom {
public:
	ocFace2D(sPtr<pigInfo> i = thNULL) : ocGeom(i) {}

	TopoDS_Shape&       shape()       { return s_; }
	const TopoDS_Shape& shape() const { return s_; }
	void set_shape(const TopoDS_Shape &s) { s_ = s; }

	virtual sPtr<stdString> get_str();

	virtual const char* meta_tag()  { return OC2_TAG; }
	virtual const char* type_name() { return OC2_TYPE; }
	virtual uint16_t    repr_type() { return 129; }   /* B-rep 2D */
	virtual int         dim()       { return 2; }

	virtual void encode(ocChunkSink&   sink);
	virtual void decode(ocChunkSource& src);
	virtual bool write_to(const char *path, const char *unit);

	/* 面積 (area op 用)。GProp_GProps の SurfaceProperties。 */
	double area() const;
private:
	TopoDS_Shape s_;
};

/* ★ #3475: このモジュール専用のエラー生成子。文言は "[TAG] <name>/op: message" になる。
 *   素の oca_err(...) を使うとモジュール名が付かない。 */
PIG_DEFINE_MODULE_ERR(oca_err, OC_MODULE_NAME)

#endif
