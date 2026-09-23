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
#include	"pig/c++/pigBreak.h"         /* #3498: 走行中の中断 (下の ★中断 を参照) */
#include	"pig/c++/pigOpEntry.h"   /* pigWireClass (配線先) */
#include	<TopoDS_Shape.hxx>
#include	<stdint.h>
#include	<string.h>
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
/* ★★ #3533: **oc-cross2d から oc-face3d へ改名**した。occt には「z=0 の簡易表現」という
 *   状態が存在しない — TopoDS_Face は最初から空間に置かれた (しかも曲面上の) 面なので、
 *   cg/mf の 2 型のうち *一般表現* にしか対応しない。⇒ 型名も一般表現の名前にする。
 *   ⚠ 4CC (BRP2) は **据え置き**。改名はキャッシュの中身を変えない。
 *
 * ★★★ #3544 (2026-09-17・ひさ判断): **oc-cross2d を型名として戻した**。
 *   #3533 の読み「occt に z=0 の状態が存在しない」は *生成 op しか見ていなかった*。
 *   陰線処理 (HLR) の出力は **投影面の上の平らな図面**で、実測でも z[0,0] に乗る
 *   (#3544 段 0)。⇒ その状態は存在する。
 *   ⚠⚠ **4CC は BRP2 のまま 1 つ**。型は planner が載せるスタンプが持つので、blob を
 *     分ける必要がない (pigData.cpp「型は planner が載せたスタンプだけが持つ」)。
 *     ⇒ cgal が PLY2 を cg-cross2d / cg-face3d の 2 型で共有しているのと同じ形。
 *   ⇒ 規約は cg / mf と同じ 4 つ (docs/srava_language_reference.md#two-2d-types)。 */
#define OC2_TYPE	"oc-face3d"
#define OC2C_TYPE	"oc-cross2d"
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

	/* ★ #3518: アフィン変換 (行優先 3x4 = m00..m23)。**この階層に置く**のは、変換が
	 *   TopoDS_Shape を動かすだけで 3D / 2D の区別を要しないため (実体は両者で共有)。
	 *   ⚠ 返り値は **入力と同じ型** (oc-brep3d → oc-brep3d / oc-face3d → oc-face3d)。
	 *   ★★ 2D (ocFace2D) は TopoDS_Face なので **z=0 平面の外へも動かせる**。cgal / manifold の
	 *     2D は XY 2x2 + (tx,ty) しか使わず面外成分を**黙って捨てる**のに対し、occt は
	 *     3D の変換をそのまま受ける (断面を空間に置けるのが loft の前提・#3518)。
	 *   失敗は null (+ err に理由)。 */
	virtual sPtr<ocGeom> op_affine(const double e[12], char *err = 0, int errsz = 0) = 0;

	static sPtr<ocGeom> create_for_meta(const uint8_t *meta, int len);

	/* ★ 2026-08-28 (ABI v12): **この階層への配線先**。op の OPS 行が OPWIRE(Calc, ocGeom) と
	 *   書くと、引数はこの WIRE 経由で実体化される。create_for_meta が 4CC を受理判定し、
	 *   mkReader がこの階層の stream reader を起こす。定義は ocCacheCodec.cpp。 */
	static const pigWireClass WIRE;
};

/* ---- B-rep ソリッド ---- */
class ocFace2D;   /* ★ #3514: section の返り値 (定義は下) */

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
	/* ★ #3503 続き: STEP 書き出しは巨大な B-rep で長くなる。@c STEPControl_Writer の
	 *   Transfer / Write は Message_ProgressRange を取るので中断できる。
	 *   ⚠ BREP (BinTools) と 2D 側には進捗の口が無い。 */
	bool write_to(const char *path, const char *unit, const pigBreak *brk);

	/* ★★ #3498: **走行中に中断できる**。
	 *
	 * OCCT の中断機構は Message_ProgressIndicator — 算法へ Message_ProgressRange を渡すと、
	 * 算法が要所で indicator->UserBreak() を引く。真を返せば算法はそこで畳まれる。
	 * 器 (indicator の派生) を用意するのは *こちら側* で、その実装は ocShape.cpp の
	 * ocBreakIndicator。旗そのものは ptsCalcBody が持つ pigBreak (pigBreak.h)。
	 *
	 * ★ occt が 3 カーネルの中で最も素直: **ブール本体が止まる**。BRepAlgoAPI_* の
	 *   Build(range) がそのまま BOPAlgo へ降りるので、評価点を動かす改修は要らない。
	 * ⚠ 中断すると算法は「できなかった」状態で返る (IsDone()==false / BOPAlgo_AlertUserBreak)。
	 *   呼び手からは **失敗と区別がつかない**ので、失敗を報告する前に必ず brk を見ること
	 *   (下の oc_abort_err)。中断は「答えが出なかった」であって「答えは空」ではない。
	 * ⚠ UserBreak は **並行に呼ばれうる** (Message_ProgressIndicator.hxx に明記)。だから旗は
	 *   tinyState の is_destroyed() ではなく pigBreak の atomic を見る (理由は pigBreak.h)。
	 *
	 * brk = 0 なら中断機構を繋がない (既定・従来どおりの挙動)。 */

	/* ★ #3436 P4: n 項ブール。BRepAlgoAPI_* は SetArguments/SetTools で **リスト**を取れる
	 *   (BOPAlgo_Builder が n 個をまとめて 1 回の交差計算で処理する)。上限なし。
	 *   kind = "union" / "intersection" / "difference" (差は ops[0] から残り全部を引く = 左 fold)。 */
	static sPtr<ocShape> op_bool_nary(sArray<sPtr<ocShape> >& ops, const char *kind, char *err = 0, int errsz = 0,
	                                  const pigBreak *brk = 0);
	/* planner から届いた引数配列の入口 (2 項は従来の二項 API のまま)。失敗は null + *errmsg。 */
	/* ★★ #3511: **loft** — 断面の列を通る立体を作る (BRepOffsetAPI_ThruSections)。
	 *   ★ 断面の **置き場所は op が決めない** — 利用者が transform で空間に置いたものを
	 *     そのまま使う。これができるのは #3518 の 1 で oc-face3d が z=0 平面の外へ
	 *     出られるようになったから (メッシュ系の 2D は平面に縛られていてこれが書けない)。
	 *   ruled: false = 断面間をなめらかな曲面で通す / true = 直線で結ぶ (線織面)。
	 *   ⇒ **別の op** にしてある (loft / loft_ruled)。線織面は三角形で厳密に表せるので
	 *     メッシュ系でも実装できるが、なめらかな方は解析曲面が要る。表に出せるように分ける。
	 *   失敗は null + *errmsg (呼び手が明示エラーにする)。 */
	static sPtr<ocShape> loft_from_args(sArray<sPtr<pigData> > *args, bool ruled,
	                                    const char **errmsg, char *errbuf, int errbufsz);

	static sPtr<ocShape> bool_from_args(sArray<sPtr<pigData> > *args, const char *kind,
	                                    const char **errmsg, char *errbuf = 0, int errbufsz = 0,
	                                    const pigBreak *brk = 0);

	/* ---- ブール (BRepAlgoAPI)。失敗は null を返す (OCCT は「作れない」で失敗しうる) ---- */
	sPtr<ocShape> op_union(sPtr<ocShape> b, char *err = 0, int errsz = 0, const pigBreak *brk = 0);
	sPtr<ocShape> op_intersection(sPtr<ocShape> b, char *err = 0, int errsz = 0, const pigBreak *brk = 0);
	sPtr<ocShape> op_difference(sPtr<ocShape> b, char *err = 0, int errsz = 0, const pigBreak *brk = 0);

	/* ---- ★ 解析曲面を直接オフセット (BRepOffsetAPI_MakeOffsetShape) ---- */
	sPtr<ocShape> op_offset(double d, char *err = 0, int errsz = 0, const pigBreak *brk = 0);

	/* ---- ★ **B-rep でしか書けない加工** (#3437) ----
	 * fillet = 稜を半径 r の転がり球で丸める / chamfer = 稜を距離 d で 45 度に削ぐ。
	 * どちらも **全ての稜**に一律に適用する (部分適用は稜の選択語彙が要るので将来)。
	 * ★ メッシュ系にこれが無いのは偶然ではない — 転がり球の接触軌跡は解析曲面
	 *   (円筒・球・トーラス) であって、三角形分割では**定義そのものが近似になる**。
	 * 失敗は null (OCCT は自己交差する半径などで普通に失敗する)。 */
	sPtr<ocShape> op_fillet(double r, char *err = 0, int errsz = 0, const pigBreak *brk = 0);
	sPtr<ocShape> op_chamfer(double d, char *err = 0, int errsz = 0, const pigBreak *brk = 0);

	/* ★ #3461: 一般アフィン変換 (行優先 3x4 = m00..m23)。
	 *   translate / rotate / scale / mirror / transform の **共通の入口**。
	 *   manifold の apply_affine(double[12]) と同じ形にしてある (呼び出し側を揃えるため)。
	 *
	 *   ⚠ 線形部が「直交 × 一様スケール」なら gp_Trsf を使う。この場合 **解析曲面が
	 *   解析曲面のまま**運ばれる (球は球のまま)。そうでなければ gp_GTrsf へ落ちる。
	 *   後者では OCCT が曲面を BSpline へ変換するので、厳密な球面という性質は失われる
	 *   (形は正しい)。非等方 scale や剪断を指定した時点で避けられない。 */
	virtual sPtr<ocGeom> op_affine(const double e[12], char *err = 0, int errsz = 0);

	/* ---- ★★ #3545 段 5 (2026-09-18): **op から OCCT を追い出すための生成器** ----
	 * ⚠ 引数は素の型だけ。op TU が OCCT を触ると、投げうる inline を通っただけで
	 *   @opencascade::type_instance<Standard_Failure>::get()::anInstance@ 等の **RTTI の
	 *   型インスタンス**がその .o に emit される (2026-09-18 の実測: op TU 21/62 本)。
	 *   ★ OCCT は CGAL と違い **include だけでは出ない** — *使うと* 出る
	 *     (実測: TopoDS_Shape.hxx / gp_Pnt.hxx を include しただけの TU は 0 本)。
	 *   ⚠ 引き金は catch (Standard_Failure) だけではない — @ocaTorus@ / @ocaCylinder@ は
	 *     catch を持たないのに出ていた (OCCT の inline が投げうる経路を通るだけで実体化する)。
	 *   ⇒ **狭い直しは無い**。op から OCCT の呼び出しを丸ごと移すしかない。
	 * ★ 失敗は null + err (理由つき)。try/catch も **こちら側**に置く
	 *   (catch が op TU に在ると、それだけで RTTI が出る)。 */
	static sPtr<ocShape> make_cone(double r, double h, char *err, int errsz);
	static sPtr<ocShape> make_cylinder(double r, double h, char *err, int errsz);
	static sPtr<ocShape> make_torus(double R, double r, char *err, int errsz);
	/* ★ 多面体 (平面を縫って立体にする)。⇒ メッシュ系と **厳密に一致**する立体。
	 *   ⚠ 縫合の許容は 1e-7 (元の op と同じ)。座標は common/solids.h / geodesic.h と同じ規約。 */
	static sPtr<ocShape> make_tetrahedron(double r, char *err, int errsz);
	static sPtr<ocShape> make_pyramid(int n, double h, double r, char *err, int errsz);
	static sPtr<ocShape> make_icosphere(double r, int n, char *err, int errsz);

	/* ★ 入口: STEP / BREP を読む。**mesh → B-rep ではない** (どちらも解析曲面を
	 * そのまま持つ形式なので、読むだけで B-rep が手に入る)。失敗は null。 */
	static sPtr<ocShape> read_file(const char *path, const pigBreak *brk = 0);

	/* ★★ #3547 ④ (#3527 の規約③「片の名前を付けたら 3 つ組で名乗る」・2026-09-18):
	 *   **頂点を読む**。それまで occt は @nverts@ (数える) は在るのに *読む* 手段が 0 本で、
	 *   #3510 の表で @★頂点を読む / occt = lib@ と出ていた。
	 *   ⚠⚠ occt の「頂点」は **稜の端点**であって三角形の頂点ではない
	 *     (立方体 = 8 ・ 球 = 2 (極) ・ 円 = 1 (継ぎ目))。mesh 系の nverts とは桁が違う。
	 *   ★ 並びは @nverts@ と同じ @TopExp::MapShapes@ の順 ⇒ **verts(v) の i 番目 == vert(v,i)**
	 *     (#3527 の⚠。片方だけ順を変えると黙ってずれるので、検査がこの等式を見る)。 */
	int vert_at(int i, double p[3]) const;
	int verts_all(std::vector<double> &xyz) const;
	/* 面 fi (**@face(s,i)@ と同じ並び** = TopExp_Explorer の順) の頂点番号。
	 * ⚠ cgal の @face_verts@ は三角形だけなので常に 3 個だが、**B-rep の面は n 個**
	 *   (箱の面 = 4 ・ 円筒の側面 = 2)。⇒ 長さは面ごとに違う。返り: 個数 / -1 = 範囲外。 */
	int face_vert_indices(int fi, std::vector<int> &idx) const;

	double volume() const;      /* BRepGProp::VolumeProperties (厳密な曲面のまま積分) */
	int    nfaces() const;      /* Face の数。★三角形数ではない (円筒の側面は 1 面) */
	int    nverts() const;      /* Vertex の数。★稜の端点であって三角形の頂点ではない */
	int    nsolids() const;

	/* ---- 素性を訊く op (#3487) ----------------------------------------------
	 * ★ **B-rep のまま**測る (三角形に落とさない)。bbox は Bnd_Box・重心と面積は GProp。
	 *   球なら表面積 4πr² がそのまま出る (mesh 系の内接多面体とは構造的に違う値)。
	 * ★ valid の定義は 7 カーネル共通で ① 空でない ∧ ② 閉じている ∧ ③ 自己交差が無い
	 *   (src/h/common/meshprops.h の冒頭)。occt では BRepAlgoAPI_Check が ②③ をまとめて
	 *   答える (BRepCheck_Analyzer のトポロジ/幾何検査 + BOPAlgo_CheckerSI の自己交差検査)。
	 * ★★ #3543: ただし BRepAlgoAPI_Check は **立体を 1 つずつ**しか見ないので、*別々の立体が
	 *   稜だけで接している*形 (2 球の xor) を妥当と答える。② の「2-多様体」の側は
	 *   ocShape.cpp の oc_edges_manifold() が稜の使われ回数で別に数える。 */
	int    op_bbox(double mn[3], double mx[3]) const;
	int    op_centroid(double c[3]) const;
	double op_area() const;
	int    op_valid(const pigBreak *brk = 0) const;

	/* ★ #3514: **点との距離** — p から境界までの最短距離 (符号なし)。返り 1 = 出せた。
	 *   ★★ **B-rep のまま**測る (三角形に落とさない) ので、球なら |d - r| が閉形式のまま出る。
	 *     メッシュ系は内接多面体との距離になるので、ここは構造的に違う値 (体積・面積と同じ関係)。 */
	int    op_distance_at(const double p[3], double *out, const pigBreak *brk = 0) const;
	/* ★★ 判定は @BRepClass3d_SolidClassifier@ — **解析曲面のまま**解くので、球や円柱では
	 *   メッシュ近似の誤差が無い。⇒ 3 モジュールの中で **一番正確**。
	 *   ★ 境界の厚みは @Precision::Confusion@ (geomutils の相対 1e-12 / openvdb の
	 *     0.75 ボクセルに相当するもの。モジュールごとに違ってよい = #3575)。
	/* ---- #3581: 点群を **3 つに分ける** ------------------------------------------
	 * @cls[i]@ = **0 境界ちょうど / -1 内側 (開) / +1 外側**。@pts@ は @dim@ 成分 x @npt@
	 * (@dim@ = 2 なら **z=0 とみなす**)。返り 1 = 分けた / 0 = 断った (+ @why@)。
	 * ★★ 濾しの規約は geomutils / openvdb と **同一** (共通ヘッダ oc/c++/ocPtSplit.h)。
	 *   違うのは判定器だけ。⇒ s[1] / s[2] は 3 モジュールで一致するべき (#3581 の検定)。
	 * ⚠ OCCT は解けないときに投げるので **握り潰さず受ける** (ワーカー由来だと agent ごと死ぬ)。
	 * ⚠ 理由の文字列は **呼び手のバッファ**へ書く (@srava_affine::point3@ と同じ作法)。
	 *   ★ @static char buf[]@ にしてはいけない — 可変 static は op どうしで混線する
	 *     (test/srava_no_mutable_static が名指しする。2026-09-22 に一度そう書いた)。 */
	int    op_classify_points(const double *pts, int npt, int dim,
	                          signed char *cls, const char **why,
	                          char *errbuf, int errbufsz,
	                          const pigBreak *brk = 0) const;


	/* ★ #3514: **断面** — 点 P を通り法線 N の平面で切り、2D 領域 (ocFace2D) を返す。
	 *   cgal / manifold の section(m,P,N,mode) と **同じ約束**:
	 *     mode  0 … 平面ちょうど。共面の面が在れば **空集合** (一意に決まらないため)
	 *     mode -1 … 平面の直下の極限。共面が無ければ空集合
	 *     mode +1 … 平面の直上の極限。共面が無ければ空集合
	 *   coplanarOut != 0 なら「平面と共面の面が在ったか」を 1/0 で返す。
	 *   失敗 / 退化した N は null (呼び側が明示エラー)。 */
	sPtr<ocFace2D> op_section(const double P[3], const double N[3], int mode,
	                          int *coplanarOut = 0, char *err = 0, int errsz = 0,
	                          const pigBreak *brk = 0) const;

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
	/* ★★ #3544: 2 型は **同じ 4CC・同じクラス**で、名乗りだけが違う (cgMesh2D と同じ形)。
	 *
	 * ★★★ 名乗りは **幾何から導く**。ビット (placed_) では持たない
	 *   (ひさ判断 2026-09-17・案 i。4CC の blob にも足さない)。
	 *   理由: ビットを別に持つと、*幾何は z=0 の外に在るのに「z=0 の簡易表現」を名乗る* 値が
	 *   作れてしまう。その状態は矛盾しており、**存在できないようにする**のが目的。
	 *   ⇒ 名乗りと幾何が離れる経路が構造的に無くなる (取り違えを直すのではなく、起こさない)。
	 *   ★ cg / mf でこれが起きないのは、あちらの「枠」が *値そのものの座標系* だから —
	 *     枠が既定なら幾何は本当にその平面に居る (枠と幾何が離れない)。occt の TopoDS_Face は
	 *     空間の面を直に持つので、そこへ別のビットを添えると **離れられる**。
	 *
	 *   ⚠ 代償は 1 か所だけ: @rotate(r,"z",90)@ (面内回転) は規約①では face3d だが、幾何は
	 *     z=0 に留まるので **この値は cross2d を名乗る**。routing が見るのは sig のスタンプ
	 *     なので op の選択は規約①どおりで、食い違うのは *値に訊いたとき* だけ
	 *     (= bbox / centroid の成分数)。⇒ test/srava_face3d.sh に但し書きを置いた。
	 *   ★ キャッシュ往復でも失われない — 根拠が blob (幾何) の中に在るので、復元した値も
	 *     同じ答えを出す。「1 バイト書く」案が要らなくなったのはこのため。 */
	virtual const char* type_name() { return on_z0_plane() ? OC2C_TYPE : OC2_TYPE; }
	/* ★ 幾何の述語 — **本当に z=0 平面に居るか** (cgMesh2D::frame_is_default() に対応)。
	 *   名乗り (上) と cast (降格) の可否は、どちらも **これ 1 つ**から決まる。 */
	int  on_z0_plane(double tol = 1e-9) const;
	virtual uint16_t    repr_type() { return 129; }   /* B-rep 2D */
	virtual int         dim()       { return 2; }

	virtual void encode(ocChunkSink&   sink);
	virtual void decode(ocChunkSource& src);
	virtual bool write_to(const char *path, const char *unit);

	/* ★★ #3544 段 3: **理由つき**の書き出し。⚠ 素の write_to は bool しか返さないので、
	 *   「z=0 に居ないから断った」のか「知らない拡張子」なのかが呼び手に届かない
	 *   — 理由の無い拒否を作らない (ocGeom の decode_why と同じ作法)。 */
	bool write_to(const char *path, const char *unit, char *err, int errsz);
	/* ★★ #3545 段 5: 2D の生成器 (op から OCCT を追い出す・上の ocShape の注記を参照)。 */
	static sPtr<ocFace2D> make_circle(double r, char *err, int errsz);

	/* ★ #3544 段 3: **DXF を読む** (z=0 の図面)。⚠ 置かれた .dxf (OCS つき) は断る —
	 *   置き場所を黙って落とすくらいなら cgal の import (cg-face3d) へ回す。 */
	static sPtr<ocFace2D> read_drawing(const char *path, char *err = 0, int errsz = 0);

	/* 面積 (area op 用)。GProp_GProps の SurfaceProperties。 */
	double area() const;

	/* ★ #3518: アフィン変換。実体は ocShape と **同じ** (oc_affine_raw)。
	 *   ⚠ 面外へ出る変換 (rotate("x",45) / translate([0,0,5]) 等) も **そのまま通す**。
	 *     結果は「z=0 平面の外にある 2D 領域」で、これは型として正しい (ocFace2D は
	 *     TopoDS_Face = 任意の曲面上のトリム面)。平面を前提にしているのは
	 *     cast (cg/mf 2D へ) / polygonize / extrude・revolve の 3 箇所だけ。 */
	virtual sPtr<ocGeom> op_affine(const double e[12], char *err = 0, int errsz = 0);

	/* ★★ #3518 の 4: **面 ∩ 立体 / 面 − 立体**。返りは 2D (面を立体で切り取る)。
	 *   ⇒ 「面取り出し (face/face_at) + これ」だけで *曲面上に切り取られた 2D* が手に入る。
	 *   ★ 曲面種は保たれる (円筒面を箱で切っても円筒面のまま)。面積は加法的:
	 *     側面 18.8496 = (∩箱 3.1416) + (−箱 15.7080) を実測で確認 (#3518 の④)。
	 *   ⚠ 結果は **複数面の Compound になりうる** (継ぎ目で割れる)。ocFace2D は
	 *     TopoDS_Shape を持ち消費側は Face 単位に回すので、そのまま入る。
	 *   kind: 0 = 交差 (Common) / 1 = 差 (Cut)。失敗は null (呼び手が明示エラー)。 */
	sPtr<ocFace2D> op_bool_solid(const TopoDS_Shape &solid, int kind,
	                             char *err = 0, int errsz = 0, const pigBreak *brk = 0);

	/* ★ #3518 の 6: **面の素性を訊く**。face / face_at で平面でない 2D が普通に入るように
	 *   なったので、「それはどこに在るのか」も訊けないと困る (3D と同じ語彙で答える)。
	 *   ⚠ 2D の重心は **面積重心** (3D の体積重心に対応)。返りは 3 次元の座標
	 *     — 曲面上の面は z=0 平面に居ないので、2 成分では答えられない。 */
	int op_bbox(double mn[3], double mx[3]) const;
	int op_centroid(double c[3]) const;

	/* ★★ #3547 (2026-09-18): **測る op を 2D でも受ける**。それまで @nverts@ / @valid@ は
	 *   @ocShape@ にしか無く、@oc-face3d@ を渡すと *routing で断られて* いた
	 *   (@no module can execute op 'nverts' on (oc-face3d)@)。
	 *   ⚠ 計算の穴ではなく **型の穴** — @TopExp@ も @BRepAlgoAPI_Check@ も @TopoDS_Shape@ を
	 *     取るので、面でもそのまま動く。
	 *   ★ #3527 段 4〜6 で mf / gg / ch が geomutils 経由で 2D 型まで埋まった結果、
	 *     occt だけが取り残されていた。 */
	int nverts() const;

	/* ★★ #3547 ② (2026-09-18・ひさ裁定): **面内オフセット**。輪郭を *面の中で* d だけ動かす。
	 *   ⇒ 返りは同じ 2D (cg / mf の offset(2D) と同じ約束)。
	 *   ★ occt だけの値: 輪郭を **曲線のまま**太らせる (円は円のまま)。cg / mf は折れ線に落ちる。
	 *   ⚠⚠ **平面の面だけ**。曲面上の面 (円柱の側面など) では「距離 d」が *測地距離* になり、
	 *     ① 内側は cut locus で分裂・消滅する ② 外側は曲面の定義域から出る
	 *     ③ 測地オフセット曲線は一般に厳密表現できない (近似になる = occt を使う理由が消える)
	 *     ④ OCCT に道具が無い (BRepFill_OffsetWire / MakeOffset は **平面ワイヤ前提**)
	 *     ⇒ **明示エラー**にして offset_thicken を案内する (polygonize が #3536 で引いた線と同じ)。
	 *   失敗は null (+ err に理由)。 */
	sPtr<ocFace2D> op_offset(double d, char *err = 0, int errsz = 0) const;

	/* ★★ #3547 ② : **offset_thicken** — 面に厚み d を付けて **立体**にする (2D → 3D)。
	 *   ★ 平面でも曲面でも定義できるので、曲面の面はこちらで扱う。
	 *   ⚠ 片側だけ (面の法線側に d ・ 符号で裏返る)。3D の @offset@ (全方向に ⊕ 球(d)) とは
	 *     **約束が違う**ので別の op 名にしてある (ひさ裁定 2026-09-18・規約は「元の op 名 + _修飾」)。
	 *   ⚠⚠ **凹側の曲率半径を超えると OCCT は黙って別の立体を返す** — r=1 の円柱側面を内へ 1.5
	 *     ずらすと軸を越えるのに @IsDone()=true@ で *r=0.5〜1 の環 (体積 3π)* が返る (実測)。
	 *     @Geom_OffsetSurface@ のヘッダも「自己交差は消さない・**検査もしない**」と明記している。
	 *     ⇒ 標本点で @1 - d*κ > 0@ を数えて、破れていたら **こちらでエラーにする**。
	 *   失敗は null (+ err に理由)。 */
	sPtr<ocShape> op_thicken(double d, char *err = 0, int errsz = 0) const;

	/* ★ 面が **すべて平面**か。面内オフセットの前提 (上の op_offset の⚠)。
	 *   ⚠ @on_z0_plane()@ とは別物 — あちらは「z=0 に居るか」、こちらは「平面かどうか」。
	 *     傾いた平面は on_z0_plane=0 だが is_planar=1。 */
	int is_planar() const;

	/* ★ #3547 ④: 2D でも頂点を読む (3D と同じ約束・同じ並び)。
	 *   ⚠ 成分数は **名乗りと同じ規約** — oc-cross2d なら 2 ・ oc-face3d なら world の 3
	 *     (bbox / centroid と揃える。判定は on_z0_plane())。 */
	int vert_at(int i, double p[3]) const;
	int verts_all(std::vector<double> &xyz) const;

	/* ★★ #3553 (2026-09-18・ひさ裁定): **点との距離は 3D と同じ定義のまま 2D も受ける**。
	 *   ⚠ 新しい意味を作らない — @*-face3d@ / @*-cross2d@ はどちらも *3D に埋め込まれた 2 次元* で、
	 *     既存の定義「p から **面の集合** までの最短距離 (符号なし)」がそのまま当てはまる。
	 *   ★ 3D との関係: 立体は「境界の面」に測る (中身の詰まった立体を渡すと内側が 0 になるため・
	 *     ocShape::op_distance_at の⚠)。2D は値そのものが面なので **同じ経路**になる。
	 *   ⚠ 「平面へ射影してから 2D で測る」案は採らない — 面外の点の高さを黙って捨てることになり、
	 *     #3533 / #3534 で何度も直した *置き場所が落ちる* 事故と同じ形。
	 *   ★ 実測 (probes/face_distance.cpp・矩形 3x2 (z=0)):
	 *       面の中央 0 / 真上 h=0.7 は 0.7 / 同一平面で外に 2 は 2 / 角の外斜めは 2√2。
	 *     ⇒ 「点が面の上にあるか」が距離 0 でそのまま測れる (#3532 の当初の需要)。 */
	int op_distance_at(const double p[3], double *out, const pigBreak *brk = 0) const;
	/* ★★ 2D の判定は「**点がこの面の上に在るか**」。⚠ @oc-face3d@ は枠が任意なので、
	 *   **面の平面に載っていない点は外側 (+1)** とする (ひさ流儀に合わせて *黙って射影しない*)。
	 *   ⇒ 集合として正しい (面は 3D の中の 2 次元の集合なので、面外の点は含まれない) し、
	 *     分割の不変条件も保たれる。⚠ @part_at@ が面外を **断る**のと非対称に見えるが、
	 *     あちらは「どの片か」を答える op で射影すると *嘘になる*。こちらは「含むか」なので
	 *     面外は素直に「含まない」と言える。
	 *   ⚠ 帰結を文書に書くこと: 2D 点群 (z=0) x 枠が z=0 でない face3d は **全点が外**。 */
	/* ---- #3581: 点群を **3 つに分ける** ------------------------------------------
	 * @cls[i]@ = **0 境界ちょうど / -1 内側 (開) / +1 外側**。@pts@ は @dim@ 成分 x @npt@
	 * (@dim@ = 2 なら **z=0 とみなす**)。返り 1 = 分けた / 0 = 断った (+ @why@)。
	 * ★★ 濾しの規約は geomutils / openvdb と **同一** (共通ヘッダ oc/c++/ocPtSplit.h)。
	 *   違うのは判定器だけ。⇒ s[1] / s[2] は 3 モジュールで一致するべき (#3581 の検定)。
	 * ⚠ OCCT は解けないときに投げるので **握り潰さず受ける** (ワーカー由来だと agent ごと死ぬ)。
	 * ⚠ 理由の文字列は **呼び手のバッファ**へ書く (@srava_affine::point3@ と同じ作法)。
	 *   ★ @static char buf[]@ にしてはいけない — 可変 static は op どうしで混線する
	 *     (test/srava_no_mutable_static が名指しする。2026-09-22 に一度そう書いた)。 */
	int    op_classify_points(const double *pts, int npt, int dim,
	                          signed char *cls, const char **why,
	                          char *errbuf, int errbufsz,
	                          const pigBreak *brk = 0) const;


	/* ★ valid の定義は **共通** (#3487 の 3 条件を 2D へ写したもの・src/h/common/ringprops.h):
	 *     ① 空でない ∧ ② 各リングが面積を持ちうる ∧ ③ 自己交差が無い
	 *   ⚠ 3D の「② 閉じている (境界辺が無い)」に当たるのが 2D では ②。TopoDS_Face の
	 *     ワイヤは構造として閉じているので「閉じていない 2D」は作れず、代わりに
	 *     *面積を持てない退化* を弾く (ringprops.h が 3 点未満のリングを弾くのと同じ)。
	 *   ★ 答え方はカーネルごと — occt は BRepAlgoAPI_Check (曲線のまま) で ③ を見る。 */
	int op_valid(const pigBreak *brk = 0) const;
private:
	/* ★ #3544: 名乗りを持つビットは **無い** (上の type_name の注記)。ここに増やすと
	 *   幾何と離れられるようになる — それを塞ぐのが案 i の眼目。 */
	TopoDS_Shape s_;
};

/* ★ #3475: このモジュール専用のエラー生成子。文言は "[TAG] <name>/op: message" になる。
 *   素の oca_err(...) を使うとモジュール名が付かない。 */
PIG_DEFINE_MODULE_ERR(oca_err, OC_MODULE_NAME)

/* ★ #3498: 中断で終わったならそのエラーを、そうでなければ thNULL を返す。
 *
 * ⚠ **失敗を報告する前に必ずこれを見ること**。中断された算法は IsDone()==false で返るので
 *   「ブールが作れなかった」と見分けがつかず、そのまま報告すると *中断したのに幾何が悪いと
 *   言う* ことになる。利用者は Ctrl+C を押した本人なので、これは端的に嘘になる。
 * ⚠ 中断は必ず **エラー**で返す。空や途中の結果を返すとキャッシュに焼き付き、次回以降
 *   正しい答えとして引かれる (#3489 と同じ形の事故)。 */
static inline sPtr<pigData>
oc_abort_err(const pigBreak &b, const char *op)
{
	if ( ! b.cancelled() ) return sPtr<pigData>();
	char m[160];
	::snprintf(m, sizeof m, "%s: aborted (interrupted)", op ? op : "occt");
	return sPtr<pigData>(oca_err(m));
}

#endif
