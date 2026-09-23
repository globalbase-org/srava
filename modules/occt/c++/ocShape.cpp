/*
 * ocShape — OCCT B-rep 幾何の実装 (#3437 P5)。設計の背景はヘッダ冒頭を参照。
 */
#include	"oc/c++/ocShape.h"
#include	"oc/c++/ocDrawing.h"   /* ★ #3544 段 3: DXF / SVG */
#include	"common/blockframe.h"   /* ★ #3507: ブロック分割フレーミング */
#include	"ts2/c++/stdString.h"
#include	<Standard_Failure.hxx>
#include	<Standard_Type.hxx>
#include	<stdexcept>

#include	<BinTools.hxx>
#include	<BRepAlgoAPI_Fuse.hxx>
#include	<BRepAlgoAPI_Common.hxx>
#include	<BRepAlgoAPI_Cut.hxx>
#include	<BRepOffsetAPI_ThruSections.hxx>
#include	<BRepAlgoAPI_BooleanOperation.hxx>
#include	<TopTools_ListOfShape.hxx>
#include	<BRepOffsetAPI_MakeOffsetShape.hxx>
#include	<BRepOffsetAPI_MakeOffset.hxx>       /* ★ #3547: 面内オフセット (平面ワイヤ前提) */
#include	<BRepOffsetAPI_MakeThickSolid.hxx>   /* ★ #3547: offset_thicken (面 → 立体) */
#include	<BRepLProp_SLProps.hxx>              /* ★ #3547: 主曲率 (厚みの前提を検査する) */
#include	<Geom_Plane.hxx>                     /* ★ #3547: 面内オフセットの行き先の平面 */
#include	<BRepFilletAPI_MakeFillet.hxx>
#include	<BRepFilletAPI_MakeChamfer.hxx>
#include	<TopoDS.hxx>
#include	<TopoDS_Edge.hxx>
#include	<TopoDS_Face.hxx>
#ifdef SRAVA_OCCT_STEP
/* ★ 2026-09-07: STEP 入出力は OCCT の DataExchange (TKDESTEP/TKXSBase) を要求する。
 *   DataExchange 無しで建てた OCCT と組むときは -DSRAVA_OCCT_STEP=OFF で落とす
 *   (そのとき .brep だけになる・octs Agent の import_exts/export_exts も連動する)。 */
#include	<STEPControl_Writer.hxx>
#include	<STEPControl_Reader.hxx>
#include	<Interface_Static.hxx>
#include	<IFSelect_ReturnStatus.hxx>
#endif
#ifdef SRAVA_OCCT_IGES
/* ★ #3513: IGES (ANS US PRO/IPO-100・旧 IGES 5.3) 入出力。OCCT の DataExchange のうち
 *   **TKDEIGES** を要求する (STEP の TKDESTEP とは別ライブラリ)。⇒ フラグも別
 *   (-DSRAVA_OCCT_IGES=OFF で落とせる。そのとき import_exts/export_exts も連動する)。
 * ⚠ IGES は **STEP の前世代**で、曲面は運べるが位相 (殻・向き) が弱い。⇒ 既定の書き出しは
 *   *BRep モード* (theModecr=1) にする。Faces モード (既定の 0) は面をばらばらに並べるので
 *   読み戻しても立体にならない = 体積が出ない。 */
#include	<IGESControl_Writer.hxx>
#include	<IGESControl_Reader.hxx>
#include	<IGESControl_Controller.hxx>
#include	<IFSelect_ReturnStatus.hxx>
#endif
#include	<BRepGProp.hxx>
#include	<BRepBndLib.hxx>          /* bbox (#3487) */
#include	<Bnd_Box.hxx>
#include	<BRepPrimAPI_MakeCone.hxx>
#include	<BRepPrimAPI_MakeCylinder.hxx>
#include	<BRepPrimAPI_MakeTorus.hxx>
#include	<Geom_Circle.hxx>
#include	<BRepBuilderAPI_MakeEdge.hxx>
#include	<BRepBuilderAPI_MakeWire.hxx>
#include	<gp_Ax2.hxx>
#include	<BRepBuilderAPI_Sewing.hxx>
#include	<BRepBuilderAPI_MakePolygon.hxx>
#include	<BRepBuilderAPI_MakeSolid.hxx>
#include	"common/geodesic.h"
#include	"common/solids.h"
#include	<BRepAlgoAPI_Check.hxx>   /* valid = 妥当性 + 自己交差 (#3487) */
#include	<BRepExtrema_DistShapeShape.hxx>   /* ★ #3514: 点との距離 (解析曲面のまま) */
#include	<BRepAdaptor_Surface.hxx>          /* ★ #3514: 断面 — 共面の面を見分ける */
#include	<BRepBuilderAPI_MakeFace.hxx>
#include	<TopoDS_Iterator.hxx>
#include	<gp_Pln.hxx>
#include	<gp_Dir.hxx>
#include	<BRepBuilderAPI_MakeVertex.hxx>
#include	<TopoDS_Vertex.hxx>
#include	<gp_Pnt.hxx>
#include	<GProp_GProps.hxx>
#include	<BRepTools.hxx>
#include	<BRepAdaptor_Surface.hxx>
#include	<BRep_Builder.hxx>
#include	<TopoDS_Compound.hxx>
#include	<TopExp_Explorer.hxx>
#include	<TopExp.hxx>
#include	<TopTools_IndexedMapOfShape.hxx>
#include	<BRep_Tool.hxx>            /* ★ #3543: 退化稜を見分ける */
#include	<gp_Trsf.hxx>
#include	<gp_GTrsf.hxx>
#include	<gp_XYZ.hxx>
#include	<BRepBuilderAPI_Transform.hxx>
#include	<BRepBuilderAPI_GTransform.hxx>
#include	<cmath>
#include	<TopAbs.hxx>
#include	<Precision.hxx>
#include	<BRepClass3d_SolidClassifier.hxx>   /* ★ #3581: 点の内外 (解析曲面のまま) */
#include	<Message.hxx>
#include	<Message_Messenger.hxx>
#include	<Message_PrinterOStream.hxx>
#include	<Message_ProgressIndicator.hxx>   /* #3498: 中断の器 */
#include	<Message_ProgressRange.hxx>
#include	<Message_ProgressScope.hxx>
#include	<stdlib.h>   /* getenv */

#include	<stdio.h>
#include	<string.h>
#include	<sstream>
#include	<string>
#include	<vector>

/* ---- ★ #3498: 中断の器 (Message_ProgressIndicator) --------------------------
 * OCCT 側の作法は「算法へ Message_ProgressRange を渡す → 算法が要所で UserBreak() を引く」。
 * indicator の派生を用意するのは *こちら*。ここでは進捗表示は要らないので Show() は空。
 *
 * ⚠ Show() は **純粋仮想**なので空実装が要る (これが無いと抽象クラスのままで new できない)。
 * ⚠ UserBreak() は「並行に呼ばれうるのでスレッド安全に、かつ即座に返すこと」と
 *   Message_ProgressIndicator.hxx が明記している。⇒ atomic を 1 回読むだけにする。
 *   Show()/Position() をここから呼んではいけない (同ヘッダの指示)。
 * ⚠ UserBreak()/Show() は基底で **protected**。public に上げず override する。 */
class ocBreakIndicator : public Message_ProgressIndicator {
public:
	ocBreakIndicator(const pigBreak *b) : b_(b) {}
	DEFINE_STANDARD_RTTI_INLINE(ocBreakIndicator, Message_ProgressIndicator)
protected:
	virtual Standard_Boolean UserBreak() Standard_OVERRIDE
	{ return ( b_ != 0 && b_->cancelled() ) ? Standard_True : Standard_False; }
	virtual void Show(const Message_ProgressScope&, const Standard_Boolean) Standard_OVERRIDE {}
private:
	const pigBreak *b_;
};

/* 算法が走っている間ずっと indicator を生かすための入れ物。
 * ★ brk==0 なら indicator を作らない。Start(null handle) は「どの算法にも安全に渡せるが
 *   indicator には繋がっていないダミー range」を返すので、従来と同じ挙動になる。 */
class ocBreakScope {
public:
	ocBreakScope(const pigBreak *b) { if ( b != 0 ) ind_ = new ocBreakIndicator(b); }
	Message_ProgressRange range() { return Message_ProgressIndicator::Start(ind_); }
private:
	Handle(Message_ProgressIndicator) ind_;
};

sPtr<stdString>
ocShape::get_str()
{
	char buf[80];
	::snprintf(buf, sizeof buf, "<brep:occt faces=%d solids=%d>", nfaces(), nsolids());
	return thNEW(stdString,(buf));
}

int
ocShape::nfaces() const
{
	/* ★ **三角形数ではなく Face 数**。円筒の側面は 1 面なので、mesh 系の nfaces とは
	 *   桁が違う値になる。それがこの表現の要点なので、あえて同じ op 名で出す。 */
	int n = 0;
	for ( TopExp_Explorer e(s_, TopAbs_FACE) ; e.More() ; e.Next() ) ++n;
	return n;
}

int
ocShape::nverts() const
{
	/* ★ TopExp_Explorer は同じ頂点を稜の数だけ返すので、map で重複を潰す。
	 *   ⚠ **三角形の頂点ではなく稜の端点**。球は 2 (極) など、mesh 系とは桁が違う。 */
	TopTools_IndexedMapOfShape m;
	TopExp::MapShapes(s_, TopAbs_VERTEX, m);
	return m.Extent();
}

int
ocShape::nsolids() const
{
	int n = 0;
	for ( TopExp_Explorer e(s_, TopAbs_SOLID) ; e.More() ; e.Next() ) ++n;
	return n;
}

/* ---- ★★ #3546: bbox の経路を 3D / 2D で 1 本に揃える ------------------------
 * ★ 以前は 3D (ocShape::op_bbox) と 2D (ocFace2D::op_bbox) で **呼び方が違っていた**:
 *     3D  BRepBndLib::Add(s_, b, Standard_False);  + b.SetGap(0.0);
 *     2D  BRepBndLib::Add(s_, b);                  ← 引数なし・SetGap なし
 *   ⇒ 2D は useTriangulation が **既定 (true)** で、三角形が付いている面では 3D と *逆方向* に
 *     (曲面の内側へ) 誤り、さらに 1e-7 の余裕が面の bbox にだけ乗っていた。
 *
 * ⚠⚠ **三角形を見る経路にしてはいけない** — @c BinTools::Write の 2 引数版は三角形分割を
 *   書き込む (theWithTriangles=Standard_True) ので、三角形を見ると *キャッシュに焼き付いた
 *   分割の粗さ* で bbox が変わりうる。実測 (2026-09-17・同じ Bezier 面):
 *       三角形なし z 上端 1.0 / deflection 0.1 で 0.2672 / 0.001 で 0.2513
 *   ★ 2026-09-17 時点では三角形を付けたまま shape を返す op が無く実害は出ていないが、
 *     op が 1 本増えれば届く ⇒ いま塞ぐ。
 *
 * ⚠⚠⚠ **AddOptimal は使えない** (2026-09-17・macMINI / OCCT 7.9.3 で実測して撤回した)。
 *   Bezier / B-spline 面で @c Add が返すのは **poles の箱**で、真の箱の 4 倍になることがある
 *   (凸包性質)。そこで @c AddOptimal へ替えようとしたが、**B-spline 面で箱を過小に返す**:
 *
 *       面                  AddOptimal      Add          面を直に撮った真値
 *       Bezier   3x3        0.2500001       1.0000001    0.25      → +1e-7  保守側
 *       BSpline  fit        **0.9955557**   1.7777779    1.0       → **-4.4e-3**
 *       BSpline  interp     **0.9955557**   1.7777779    1.0       → **-4.4e-3**
 *
 *   ⇒ **箱が面を含まない**。bbox の基本契約を破るので、4 倍の過大評価より性質が悪い
 *     (過大は安全側・過小は危険側)。⚠ 許容差を 1e-3 から 1e-12 まで締めても 0.9955556 に
 *     *収束する* ので、精度ではなく **誤った極値へ収束している**。幾何層
 *     (@c BndLib_AddSurface::AddOptimal) に直接渡しても同じ。
 *   ⇒ **@c Add のまま**にする。過大評価は #3546 に記録し、docs で「Bezier / B-spline の面では
 *     制御点の箱 = 真の箱より広い」と明示する。 */
static void
oc_add_bbox(const TopoDS_Shape &s, Bnd_Box &b)
{
	BRepBndLib::Add(s, b, Standard_False);
}

/* ---- 素性を訊く op (#3487) ---------------------------------------------------
 * ★ B-rep のまま測る。三角形に落とさないので、解析曲面 (球・円柱・トーラス) では
 *   mesh 系の内接多面体と **構造的に違う値**が出る (体積と同じ事情)。 */
int
ocShape::op_bbox(double mn[3], double mx[3]) const
{
	mn[0] = mn[1] = mn[2] = mx[0] = mx[1] = mx[2] = 0.0;
	if ( s_.IsNull() ) return 3;
	Bnd_Box b;
	/* ★ useTriangulation=false: 三角形分割ではなく **曲面そのもの**から求める。
	 *   既定 (三角形) だと分割の粗さで箱が膨らむ。
	 * ★★ #3546: 2D と経路を 1 本に揃えた (oc_add_bbox の頭注。AddOptimal を撤回した理由もそこ)。 */
	oc_add_bbox(s_, b);
	if ( b.IsVoid() ) return 3;
	/* ⚠ Bnd_Box は既定で **余裕 (gap)** を持つ (実測 1e-7)。他カーネルと突き合わせると
	 *   その分だけ広い箱が返るので 0 にする。 */
	b.SetGap(0.0);
	b.Get(mn[0], mn[1], mn[2], mx[0], mx[1], mx[2]);
	return 3;
}

int
ocShape::op_centroid(double c[3]) const
{
	c[0] = c[1] = c[2] = 0.0;
	if ( s_.IsNull() ) return 3;
	GProp_GProps props;
	BRepGProp::VolumeProperties(s_, props);
	gp_Pnt g = props.CentreOfMass();
	c[0] = g.X(); c[1] = g.Y(); c[2] = g.Z();
	return 3;
}

double
ocShape::op_area() const
{
	if ( s_.IsNull() ) return 0.0;
	GProp_GProps props;
	BRepGProp::SurfaceProperties(s_, props);
	return props.Mass();
}

/* ---- ★★ #3543: ② の「2-多様体」の側を自分で数える -----------------------------
 * BRepAlgoAPI_Check は **立体を 1 つずつ**見るので、*別々の立体が稜だけで接している*形を
 * 妥当と答える (2 球の xor = 交線の円で接する三日月 2 つ)。共通定義 (#3487) の ② は
 * 「**境界辺の無い 2-多様体**」なので、それはこちらで数える。
 *
 * 数え方は「稜が面に **使われた回数**」。distinct な面の数ではない:
 *   継ぎ目 (球・円筒・トーラスの seam) は **同じ 1 面が 2 回**使うので、面で数えると 1 になり
 *   「境界がある」と誤検出する。
 * ⚠ 除くもの 2 つ (どちらも入れると **妥当な立体が赤くなる**・2026-09-15 に battery 12 形で較正):
 *   ・退化稜 (球やコーンの極) — 面に 1 回しか現れない
 *   ・INTERNAL / EXTERNAL の稜 — 面に埋め込まれた稜であって境界ではない
 * 返り: 1 = 閉じた 2-多様体・0 = 境界があるか非多様体。 */
static int
oc_edges_manifold(const TopoDS_Shape &s)
{
	TopTools_IndexedMapOfShape em;
	TopExp::MapShapes(s, TopAbs_EDGE, em);
	const int ne = em.Extent();
	if ( ne == 0 ) return 0;
	std::vector<int> use((size_t)ne + 1, 0);
	for ( TopExp_Explorer f(s, TopAbs_FACE) ; f.More() ; f.Next() )
		for ( TopExp_Explorer e(f.Current(), TopAbs_EDGE) ; e.More() ; e.Next() ) {
			const TopAbs_Orientation o = e.Current().Orientation();
			if ( o != TopAbs_FORWARD && o != TopAbs_REVERSED ) continue;
			const int i = em.FindIndex(e.Current());
			if ( i >= 1 && i <= ne ) ++use[(size_t)i];
		}
	for ( int i = 1 ; i <= ne ; ++i ) {
		if ( BRep_Tool::Degenerated(TopoDS::Edge(em(i))) ) continue;
		if ( use[(size_t)i] != 2 ) return 0;
	}
	return 1;
}

int
ocShape::op_valid(const pigBreak *brk) const
{
	if ( s_.IsNull() ) return 0;                 /* ① 空でない */
	if ( nsolids() == 0 ) return 0;
	/* ②③ = BRepAlgoAPI_Check。既定で BRepCheck_Analyzer (トポロジ/幾何の整合) と
	 * BOPAlgo_CheckerSI (自己交差) の両方を走らせる。★ OCCT が投げることがあるので
	 * ここで受ける (ワーカースレッド由来だと agent ごと死ぬ)。 */
	try {
		/* ★ ② の多様体性が先 — 稜を 1 度なめるだけで済むので、重い CheckerSI の前に置く。 */
		if ( ! oc_edges_manifold(s_) ) return 0;
		ocBreakScope br(brk);   /* ★ #3498: 自己交差検査 (BOPAlgo_CheckerSI) は重くなりうる */
		BRepAlgoAPI_Check chk(s_);
		chk.SetRunParallel(Standard_False);
		chk.Perform(br.range());
		return chk.IsValid() ? 1 : 0;
	} catch ( const Standard_Failure& ) {
		return 0;
	} catch ( ... ) {
		return 0;
	}
}


static int oc_distance_to_faces(const TopoDS_Shape &s, const double p[3], double *out,
                                const pigBreak *brk);
/* ★ #3581: 2D の「中か境界か」を分けるのに要る (外周までの距離)。定義は下。 */
static int oc_distance_to_edges(const TopoDS_Shape &s, double x, double y, double z,
                                double *out, const pigBreak *brk);

/* ---- 点との距離 (#3514) ----
 * ★ BRepExtrema_DistShapeShape は **解析曲面のまま**距離を解くので、球 (BRep の Sphere) では
 *   |d - r| が丸め誤差の範囲で出る。三角形化した近似ではない。
 * ⚠ 符号は付けない (内側でも正)。cgal / geogram / openvdb と約束を揃えてある。
 * ⚠ OCCT は解けないときに投げることがあるので受ける (ワーカースレッド由来だと agent ごと死ぬ)。 */
int
ocShape::op_distance_at(const double p[3], double *out, const pigBreak *brk) const
{
	return oc_distance_to_faces(s_, p, out, brk);
}

/* ★★ #3553: 2D も **同じ定義・同じ実装**で受ける (ocShape.h の注記)。
 *   ⇒ 実体は上の 3D と 1 本 (oc_distance_to_faces)。2 本書くと片方だけ直る。 */
int
ocFace2D::op_distance_at(const double p[3], double *out, const pigBreak *brk) const
{
	return oc_distance_to_faces(s_, p, out, brk);
}

/* ================= #3581: 点群を 3 つに分ける ===================================
 * ★★ 濾しの規約は geomutils / openvdb と **同一**。判定器だけが違う。
 *   3D … BRepClass3d_SolidClassifier (解析曲面のまま解く = 3 モジュールで一番正確)
 *   2D … 「点がこの面の上に在るか」。面の平面に載っていない点は **外側**
 * ★ 境界の厚みは Precision::Confusion。⚠ モジュールごとに違ってよい (#3575) が、
 *   s[1] / s[2] は 3 モジュールで一致するべき。
 * ⚠ OCCT は解けないときに投げるので **握り潰さず受ける**。 */
int
ocShape::op_classify_points(const double *pts, int npt, int dim,
                            signed char *cls, const char **why,
                            char *errbuf, int errbufsz, const pigBreak *brk) const
{
	*why = 0;
	if ( dim != 2 && dim != 3 ) { *why = "the point cloud must be 2D or 3D"; return 0; }
	if ( s_.IsNull() ) { *why = "this shape is empty, so it encloses nothing"; return 0; }
	try {
		ocBreakScope br(brk);
		const double tol = Precision::Confusion();
		BRepClass3d_SolidClassifier cl(s_);
		for ( int i = 0 ; i < npt ; ++i ) {
			const double x = pts[(size_t)i*dim + 0];
			const double y = pts[(size_t)i*dim + 1];
			const double z = ( dim == 3 ) ? pts[(size_t)i*dim + 2] : 0.0;   /* ★ 2D は z=0 */
			cl.Perform(gp_Pnt(x, y, z), tol);
			const TopAbs_State st = cl.State();
			/* ★ ON = 境界ちょうど。IN / OUT はそのまま。
			 * ⚠ UNKNOWN は **黙って内外へ倒さない** — 一意に決まらないものは境界へ寄せる
			 *   (規約「疑わしきは利用者が決める側 = s[0] へ」)。 */
			cls[i] = ( st == TopAbs_IN )  ? (signed char)-1
			       : ( st == TopAbs_OUT ) ? (signed char)1
			       :                        (signed char)0;
		}
	} catch ( const Standard_Failure &e ) {
		/* ⚠ **呼び手のバッファ**へ書く。static にすると op どうしで混線する
		 *   (test/srava_no_mutable_static)。 */
		if ( errbuf != 0 && errbufsz > 0 ) {
			::snprintf(errbuf, (size_t)errbufsz, "OCCT could not classify the points (%s)",
			    e.GetMessageString() ? e.GetMessageString() : "no message");
			*why = errbuf;
		} else
			*why = "OCCT could not classify the points";
		return 0;
	}
	return 1;
}

/* ★★ 2D: 「点が **この面の上**に在るか」。
 * ⚠ 面の平面に載っていない点は **外側** — 黙って射影しない (#3534 と同じ線引き)。
 *   面は 3D の中の 2 次元の集合なので、面外の点を「含まない」と言うのは集合として正しい。
 * ★ 実装は **距離 1 本**で足りる: 面 (材料) までの距離が 0 なら中・tol 以内なら境界・
 *   それより遠ければ外。⇒ 面外の高さも距離に入るので、上の規約が自動的に成り立つ。
 *   ⚠ oc_distance_to_faces は **境界 (Face の compound) まで**の距離なので中でも正になる。
 *     ここは *材料まで* が要るので BRepExtrema に面をそのまま渡す (下で別に測る)。 */
int
ocFace2D::op_classify_points(const double *pts, int npt, int dim,
                             signed char *cls, const char **why,
                             char *errbuf, int errbufsz, const pigBreak *brk) const
{
	*why = 0;
	if ( dim != 2 && dim != 3 ) { *why = "the point cloud must be 2D or 3D"; return 0; }
	if ( s_.IsNull() ) { *why = "this 2D region is empty, so it encloses nothing"; return 0; }
	try {
		ocBreakScope br(brk);
		const double tol = Precision::Confusion();
		for ( int i = 0 ; i < npt ; ++i ) {
			const double x = pts[(size_t)i*dim + 0];
			const double y = pts[(size_t)i*dim + 1];
			const double z = ( dim == 3 ) ? pts[(size_t)i*dim + 2] : 0.0;
			BRepBuilderAPI_MakeVertex mv(gp_Pnt(x, y, z));
			/* ★ 面 **そのもの** に対して測る (材料までの距離)。中なら 0 になる。 */
			BRepExtrema_DistShapeShape dss(mv.Vertex(), s_);
			if ( ! dss.IsDone() || dss.NbSolution() < 1 ) { cls[i] = (signed char)1; continue; }
			const double d = dss.Value();
			/* ⚠ 「中」と「境界」は距離だけでは分けられない (どちらも 0)。⇒ 境界 (Edge の
			 *   compound) までの距離を別に測り、tol 以内なら境界と決める。 */
			if ( d > tol ) { cls[i] = (signed char)1; continue; }        /* 外 */
			double db = 0.0;
			if ( ! oc_distance_to_edges(s_, x, y, z, &db, brk) ) { cls[i] = (signed char)0; continue; }
			cls[i] = ( db <= tol ) ? (signed char)0 : (signed char)-1;
		}
	} catch ( const Standard_Failure &e ) {
		/* ⚠ **呼び手のバッファ**へ書く。static にすると op どうしで混線する
		 *   (test/srava_no_mutable_static)。 */
		if ( errbuf != 0 && errbufsz > 0 ) {
			::snprintf(errbuf, (size_t)errbufsz, "OCCT could not classify the points (%s)",
			    e.GetMessageString() ? e.GetMessageString() : "no message");
			*why = errbuf;
		} else
			*why = "OCCT could not classify the points";
		return 0;
	}
	return 1;
}

/* p から **外周 (Edge の集合)** までの距離。2D の「中か境界か」を分けるのに要る。 */
static int
oc_distance_to_edges(const TopoDS_Shape &s_, double x, double y, double z,
                     double *out, const pigBreak *brk)
{
	if ( s_.IsNull() ) return 0;
	ocBreakScope br(brk);
	TopoDS_Compound wireCmp;
	BRep_Builder bb;
	bb.MakeCompound(wireCmp);
	int ne = 0;
	for ( TopExp_Explorer ex(s_, TopAbs_EDGE) ; ex.More() ; ex.Next() ) {
		bb.Add(wireCmp, ex.Current());
		++ne;
	}
	if ( ne == 0 ) return 0;
	BRepBuilderAPI_MakeVertex mv(gp_Pnt(x, y, z));
	BRepExtrema_DistShapeShape dss(mv.Vertex(), wireCmp);
	if ( ! dss.IsDone() || dss.NbSolution() < 1 ) return 0;
	if ( out ) *out = dss.Value();
	return 1;
}

/* p から **面の集合**までの最短距離 (符号なし)。3D / 2D 共用。 */
static int
oc_distance_to_faces(const TopoDS_Shape &s_, const double p[3], double *out, const pigBreak *brk)
{
	if ( s_.IsNull() ) return 0;
	try {
		ocBreakScope br(brk);
		/* ⚠⚠ **立体をそのまま渡してはいけない**。BRepExtrema_DistShapeShape は
		 *   TopoDS_Solid を **中身の詰まった領域**として扱うので、内側の点との距離が
		 *   **0** になる (2026-09-13 実測: 半径 2 の球の中心で 0・|0-2|=2 ではない)。
		 *   ⇒ Face を集めた compound = **境界だけ**に対して測る。
		 *   ★ 他の 3 カーネルは境界しか持っていないので、揃えるのはこちら側の仕事。 */
		TopoDS_Compound shell;
		BRep_Builder bb;
		bb.MakeCompound(shell);
		int nf = 0;
		for ( TopExp_Explorer ex(s_, TopAbs_FACE) ; ex.More() ; ex.Next() ) {
			bb.Add(shell, ex.Current());
			++nf;
		}
		if ( nf == 0 ) return 0;
		TopoDS_Vertex v = BRepBuilderAPI_MakeVertex(gp_Pnt(p[0], p[1], p[2]));
		BRepExtrema_DistShapeShape ext(shell, v);
		ext.Perform();
		if ( ! ext.IsDone() || ext.NbSolution() < 1 ) return 0;
		if ( out ) *out = ext.Value();
		return 1;
	} catch ( const Standard_Failure& ) {
		return 0;
	} catch ( ... ) {
		return 0;
	}
}

double
ocShape::volume() const
{
	if ( s_.IsNull() ) return 0.0;
	/* ★ **厳密な曲面のまま積分する**。三角形に落としてから積むのではないので、球なら
	 *   4/3·π·r³ がそのまま出る (内接多面体の体積ではない)。ここが mesh 系との質的な差。 */
	GProp_GProps props;
	BRepGProp::VolumeProperties(s_, props);
	return props.Mass();
}

/* ★ OCCT の診断出力を stdout から stderr へ移す (ヘッダの ensure_init のコメント参照)。
 * プロセスに 1 回。**幾何とは無関係だが、これをやらないと process 実行の agent が必ず死ぬ。** */
void
ocShape::ensure_init()
{
	/* ⚠ 「初期化したか」の static は置かない (ひさ指示 2026-08-26)。この関数は **冪等**:
	 * RemovePrinters が毎回先に走るので、AddPrinter を繰り返しても printer は 1 つに保たれる。 */
	Handle(Message_Messenger) m = Message::DefaultMessenger();
	if ( m.IsNull() ) return;
	/* ★ 既定の printer は std::cout へ書く。**stdout は pigwire なので黙らせる**。
	 * ⚠ OCCT 7.8 の Message_PrinterOStream には「任意の ostream を渡す」ctor が無い
	 *   (ファイル名を渡す形か既定の cout のみ) ので、stderr へ差し替えることはできない。
	 *   よって既定では**全部落とす**。診断が要るときだけ SRAVA_OCCT_LOG=<path> で
	 *   ファイルへ出す (ファイル名 ctor は 3 OS 共通で使える)。 */
	m->RemovePrinters(STANDARD_TYPE(Message_PrinterOStream));
	const char *lg = ::getenv("SRAVA_OCCT_LOG");
	if ( lg != 0 && lg[0] != 0 )
		m->AddPrinter(new Message_PrinterOStream(lg, Standard_True, Message_Info));
}

/* 拡張子の大文字小文字を無視して比べる (".STEP" も受ける)。
 * ★ 呼び手は STEP / IGES の分岐しか無いので、**両方 OFF** のビルドでは丸ごと未使用になる
 *   (clang が -Wunused-function を出す)。⇒ 呼び手と同じ条件で囲う。 */
#if defined(SRAVA_OCCT_STEP) || defined(SRAVA_OCCT_IGES)
static bool
oc_ieq(const char *a, const char *b)
{
	for ( ; *a != 0 && *b != 0 ; ++a, ++b ) {
		int ca = (*a >= 'A' && *a <= 'Z') ? (*a - 'A' + 'a') : *a;
		int cb = (*b >= 'A' && *b <= 'Z') ? (*b - 'A' + 'a') : *b;
		if ( ca != cb ) return false;
	}
	return *a == 0 && *b == 0;
}
#endif	/* SRAVA_OCCT_STEP || SRAVA_OCCT_IGES */

/* ---- OCCT の例外を srava のエラーへ落とす ----------------------------------
 * ★ OCCT のアルゴリズムは **例外で失敗を知らせる**ことがある。IsDone() で見える失敗
 *   (半径が大きすぎる等) は呼び手が既に扱っているが、それ以前に投げてくる経路がある
 *   (実例: fillet(sphere(...)) — 球にも seam 稜はあるので n>0 を通り、MakeFillet の中で
 *   Standard_Failure)。捕まえないと agent が terminate() → SIGABRT で死に、原因が読めない。
 *
 * ⚠⚠ **Standard_Failure は std::exception 派生ではない** (Standard_Transient 派生)。
 *   geogram と同じ `catch (const std::exception&)` を書いても **OCCT では素通りする**。
 *   専用の catch を先に置き、型名とメッセージを取る。
 * ⚠⚠ **理由をモジュール大域 (static) に置かない** (ひさ指示 2026-08-26)。in-proc 実行では
 *   1 プロセスに複数 op が同居しうるので混線する。**呼び手のバッファへ書く**ことで
 *   リエントラントに保つ。err==0 なら理由は捨てる。
 * ⚠ 捕まえるのは例外だけ。SIGSEGV 等はここでは受けない。
 * ★ OCCT の並列 (OSD_Parallel) は TBB でも自前スレッドプールでも **worker の例外を
 *   呼び出しスレッドへ投げ直す**ので、ここで捕まえられる (geogram はそうなっていない)。 */
static void
oc_note_error(char *err, int errsz, const char *type, const char *what)
{
	if ( err == 0 || errsz <= 0 )
		return;
	const char *t = ( type != 0 && *type != '\0' ) ? type : "exception";
	if ( what != 0 && *what != '\0' )
		::snprintf(err, (size_t)errsz, "%s: %s", t, what);
	else
		::snprintf(err, (size_t)errsz, "%s", t);
	for ( char *p = err ; *p ; ++p ) if ( *p == '\n' || *p == '\r' ) *p = ' ';
}

/* ★ #3501: OCCT が上げた警告を呼び手のバッファへ写す (理由を「言えるのに言わない」を作らない)。
 *   ⚠ 理由の受け皿は **呼び手のバッファ**。モジュール大域の static を置かない
 *     (in-proc では 1 プロセスに複数 op が同居するので混線する。ひさ指示 2026-08-26)。 */
static void
oc_note_warnings(const BRepAlgoAPI_BooleanOperation &op, char *err, int errsz)
{
	if ( err == 0 || errsz <= 0 ) return;
	std::ostringstream w;
	op.DumpWarnings(w);
	std::string t = w.str();
	for ( size_t i = 0 ; i < t.size() ; ++i ) if ( t[i] == '\n' || t[i] == '\r' ) t[i] = ' ';
	::snprintf(err, (size_t)errsz,
	    "the fused shape lost material (its bounding box is smaller than the inputs); "
	    "OCCT reported: %s", t.empty() ? "(no detail)" : t.c_str());
}

/* 呼ぶ側の定型。f() が例外を投げたら 0 を返し、理由を err へ書く。 */
template <class F>
static int
oc_guard(F f, char *err, int errsz)
{
	try {
		f();
		return 1;
	} catch ( const Standard_Failure &e ) {        /* ★ std::exception 派生ではない */
		oc_note_error(err, errsz, e.DynamicType()->Name(), e.GetMessageString());
		return 0;
	} catch ( const std::exception &e ) {
		oc_note_error(err, errsz, "std::exception", e.what());
		return 0;
	} catch ( ... ) {
		oc_note_error(err, errsz, 0, 0);
		return 0;
	}
}

/* ---- ブール ----
 * ★ OCCT のブールは**失敗しうる** (トレランスが噛み合わないと「作れませんでした」になる)。
 *   誤った形を黙って返すより良い性質なので、失敗は null にして呼び手が明示エラーにする。 */
static sPtr<ocShape>
oc_wrap(const TopoDS_Shape &s)
{
	if ( s.IsNull() ) return sPtr<ocShape>();
	sPtr<ocShape> out = thNEW(ocShape,());
	out->set_shape(s);
	return out;
}

/* ---- ★★ #3501: union が **黙って材料を落とす**のを捕まえる ----------------------
 * OCCT の BOPAlgo は、融合できなかった配置で @c IsDone() を真・@c HasErrors() を偽に
 * したまま @c BOPAlgo_AlertUnableToOrientTheShape を *警告*として上げ、**材料を大量に
 * 落とした形をそのまま返す**ことがある。実測 (球 320 個の union・OCCT 7.9.3):
 *
 *     fuse a=146.773935 b=190.113890 ->  4.188658   IsDone=1 err=0 warn=1
 *     fuse a=107.388403 b= 59.848040 -> -0.000132   IsDone=1 err=0 warn=1
 *
 *   体積 190 が 4.19 (球 1 個ぶん) に落ち、負になった回すらある。利用者には普通の数値として
 *   返り、キャッシュに焼き付いて次回以降も「正しい答え」として引かれていた。
 *
 * ★ 検出は **bbox の包含**で行う。a∪b は a も b も含むので、結果の bbox は両者の bbox の和を
 *   含まなければならない — これは *トレランスに依存しない* union の定義そのもの。
 *
 * ⚠ **警告そのものを失敗の指標にしてはいけない**。実測では警告 37 件のうち破綻は 6 件で、
 *   31 件は正しい結果だった。警告で弾くと正しい結果を大量に捨てる。
 * ⚠ 体積の不変条件 (V(a∪b) >= max) より **bbox のほうが厳しい**。実測で体積検査が見逃した
 *   損傷 (57.66 + 58.56 -> 59.85) を bbox は捕まえており、その出力は次の fuse で -0.000132 に
 *   化けていた。
 * ★ **警告が出たときだけ**呼ぶ (呼び手を参照)。警告は 6〜12% でしか出ないので、測定対象
 *   カーネルにほぼ負荷を足さない。実測でも「警告なしで壊れた例」は 0 件だった。
 * ⚠ intersection / difference には入れない — 結果が入力に *含まれる*側なので「空に潰れた」を
 *   不変条件で正常と区別できない (空集合は正当な答え)。下界を持つのは union だけ。
 *
 * 戻り 0 = 材料を落としている (呼び手はエラーにする)。 */
static int
oc_union_bbox_ok(const TopoDS_Shape &a, const TopoDS_Shape &b, const TopoDS_Shape &r)
{
	if ( r.IsNull() ) return 0;
	Bnd_Box ba, bb, brr;
	/* ★ useTriangulation=false は op_bbox と同じ理由 (曲面そのものから求める)。
	 * ⚠ #3546: ここは検算 (「結果の箱が入力の箱を超えていないか」で材料落ちを見る) なので、
	 *   箱が過大なぶんには **見逃す方向** = 安全側に倒れる。⇒ 値を返す op (op_bbox) と
	 *   要求が違うため、oc_add_bbox に寄せずそのまま置く。 */
	BRepBndLib::Add(a, ba, Standard_False);
	BRepBndLib::Add(b, bb, Standard_False);
	BRepBndLib::Add(r, brr, Standard_False);
	if ( ba.IsVoid() || bb.IsVoid() ) return 1;   /* 入力が空なら比べる下界が無い */
	if ( brr.IsVoid() ) return 0;                 /* 入力があるのに結果が空 = 落ちている */
	ba.SetGap(0.0); bb.SetGap(0.0); brr.SetGap(0.0);
	double ax1,ay1,az1,ax2,ay2,az2, bx1,by1,bz1,bx2,by2,bz2, rx1,ry1,rz1,rx2,ry2,rz2;
	ba.Get(ax1,ay1,az1,ax2,ay2,az2);
	bb.Get(bx1,by1,bz1,bx2,by2,bz2);
	brr.Get(rx1,ry1,rz1,rx2,ry2,rz2);
	const double lo[3] = { (ax1<bx1?ax1:bx1), (ay1<by1?ay1:by1), (az1<bz1?az1:bz1) };
	const double hi[3] = { (ax2>bx2?ax2:bx2), (ay2>by2?ay2:by2), (az2>bz2?az2:bz2) };
	const double r1[3] = { rx1, ry1, rz1 }, r2[3] = { rx2, ry2, rz2 };
	/* ★ 許容は箱の大きさに対する相対 — 絶対値だと寸法の桁で意味が変わる。狙いは
	 *   「材料が丸ごと落ちた」の検出であって、トレランス程度のずれを咎めることではない。 */
	double span = 0.0;
	for ( int k = 0 ; k < 3 ; ++k ) { const double d = hi[k] - lo[k]; if ( d > span ) span = d; }
	const double tol = ( span > 0.0 ) ? span * 1e-6 : 1e-9;
	for ( int k = 0 ; k < 3 ; ++k )
		if ( r1[k] > lo[k] + tol || r2[k] < hi[k] - tol ) return 0;
	return 1;
}

sPtr<ocShape>
ocShape::op_union(sPtr<ocShape> b, char *err, int errsz, const pigBreak *brk)
{
	if ( b == thNULL || s_.IsNull() || b->shape().IsNull() ) return sPtr<ocShape>();
	TopoDS_Shape r;
	int lost = 0;
	ocBreakScope br(brk);   /* ★ #3498: 中断機構 (brk==0 ならダミー range) */
	if ( ! oc_guard([&]{ BRepAlgoAPI_Fuse op(s_, b->shape(), br.range());
		if ( ! op.IsDone() ) return;
		/* ★ #3501: **警告が出たときだけ** bbox を見る (常時だと測定対象カーネルに
		 *   無用な負荷が乗る。実測で警告なしの破綻は 0 件)。 */
		if ( op.HasWarnings() && ! oc_union_bbox_ok(s_, b->shape(), op.Shape()) ) {
			lost = 1;
			oc_note_warnings(op, err, errsz);
			return;
		}
		r = op.Shape(); }, err, errsz) ) return sPtr<ocShape>();
	if ( lost ) return sPtr<ocShape>();
	return oc_wrap(r);
}

sPtr<ocShape>
ocShape::op_intersection(sPtr<ocShape> b, char *err, int errsz, const pigBreak *brk)
{
	if ( b == thNULL || s_.IsNull() || b->shape().IsNull() ) return sPtr<ocShape>();
	TopoDS_Shape r;
	ocBreakScope br(brk);   /* ★ #3498: 中断機構 (brk==0 ならダミー range) */
	if ( ! oc_guard([&]{ BRepAlgoAPI_Common op(s_, b->shape(), br.range());
		if ( op.IsDone() ) r = op.Shape(); }, err, errsz) ) return sPtr<ocShape>();
	return oc_wrap(r);
}

sPtr<ocShape>
ocShape::op_difference(sPtr<ocShape> b, char *err, int errsz, const pigBreak *brk)
{
	if ( b == thNULL || s_.IsNull() || b->shape().IsNull() ) return sPtr<ocShape>();
	TopoDS_Shape r;
	ocBreakScope br(brk);   /* ★ #3498: 中断機構 (brk==0 ならダミー range) */
	if ( ! oc_guard([&]{ BRepAlgoAPI_Cut op(s_, b->shape(), br.range());
		if ( op.IsDone() ) r = op.Shape(); }, err, errsz) ) return sPtr<ocShape>();
	return oc_wrap(r);
}

/* ---- n 項ブール (#3436 P4) --------------------------------------------------
 * BRepAlgoAPI_* は引数を **リスト**で取れる (内部の BOPAlgo_Builder が n 個をまとめて
 * 1 回の交差計算で扱う)。二項の ctor はその 2 個版にすぎない。
 *   union        arguments={ops[0]} / tools={残り}  → 全部の Fuse
 *   intersection 同上                                → 全部の Common
 *   difference   同上                                → ops[0] から残り全部を引く (= 左 fold) */
sPtr<ocShape>
ocShape::op_bool_nary(sArray<sPtr<ocShape> >& ops, const char *kind, char *err, int errsz,
                      const pigBreak *brk)
{
	int n = ops.length();
	if ( n < 2 || kind == 0 ) return sPtr<ocShape>();
	TopTools_ListOfShape aL, tL;
	for ( int i = 0 ; i < n ; ++i ) {
		if ( ! ops[i].is_notNull() || ops[i]->shape().IsNull() ) return sPtr<ocShape>();
		if ( i == 0 ) aL.Append(ops[i]->shape());
		else          tL.Append(ops[i]->shape());
	}
	BRepAlgoAPI_Fuse   fu;
	BRepAlgoAPI_Common co;
	BRepAlgoAPI_Cut    cu;
	BRepAlgoAPI_BooleanOperation *op;
	if      ( ::strcmp(kind, "union") == 0 )        op = &fu;
	else if ( ::strcmp(kind, "intersection") == 0 ) op = &co;
	else if ( ::strcmp(kind, "difference") == 0 )   op = &cu;
	else return sPtr<ocShape>();
	TopoDS_Shape out;
	int lost = 0;
	ocBreakScope br(brk);   /* ★ #3498: Build(range) が BOPAlgo までそのまま降りる */
	if ( ! oc_guard([&]{
		op->SetArguments(aL);
		op->SetTools(tL);
		op->Build(br.range());
		if ( ! op->IsDone() ) return;
		/* ★ #3501: 二項と同じ検査を n 項でも。**union のときだけ**
		 *   (intersection / difference は結果が入力に含まれる側なので下界を持たない)。
		 *   ⚠ 既定では occt は .arity 未申告 = 2 なのでここは通らないが、
		 *     module("occt.so",{arity:k}) で上書きされれば通る。実測でも arity=32 で
		 *     別の誤値 (0.1402) が出た — つまりこの経路も同じ壊れ方をする。 */
		if ( ::strcmp(kind, "union") == 0 && op->HasWarnings() ) {
			/* n 項の下界は「全オペランドの bbox の和」。左端 1 個との比較では足りない。
			 * ops[0] から順に畳んだ箱を作って包含を見る。 */
			TopoDS_Compound all;
			BRep_Builder bld;
			bld.MakeCompound(all);
			for ( int i = 0 ; i < n ; ++i ) bld.Add(all, ops[i]->shape());
			if ( ! oc_union_bbox_ok(all, all, op->Shape()) ) {
				lost = 1;
				oc_note_warnings(*op, err, errsz);
				return;
			}
		}
		out = op->Shape();
	}, err, errsz) )
		return sPtr<ocShape>();
	if ( lost ) return sPtr<ocShape>();
	return oc_wrap(out);
}

/* ---- ★★ #3511: loft — 断面の列を通る立体 -----------------------------------
 * 断面 (oc-face3d) の **外周ワイヤ**を順に BRepOffsetAPI_ThruSections へ渡す。
 * ★ 置き場所は op が決めない。利用者が transform で空間に置いた断面をそのまま使う
 *   (#3518 の 1 で oc-face3d が平面の外へ出られるようになったのでこれが書ける)。
 * ⚠ 断面が複数の面を持つ (穴つき / ブールで割れた) 場合は **明示エラー**。
 *   どの輪をどの輪につなぐかが決まらないため (黙って外周だけ使うと穴が消える)。
 * ⚠ CheckCompatibility は既定で有効 — 断面ごとに始点と向きを合わせ直して辺数を揃える。
 *   切ると面がねじれる。 */
sPtr<ocShape>
ocShape::loft_from_args(sArray<sPtr<pigData> > *args, bool ruled,
                        const char **errmsg, char *errbuf, int errbufsz)
{
	int na = ( args != 0 ) ? args->length() : 0;
	if ( na < 2 ) { *errmsg = "needs at least two cross sections"; return sPtr<ocShape>(); }
	TopoDS_Shape r;
	int bad = 0, nf = 0;
	if ( ! oc_guard([&]{
		BRepOffsetAPI_ThruSections mk(Standard_True /* 立体にする */,
		                              ruled ? Standard_True : Standard_False,
		                              Precision::Confusion());
		for ( int i = 0 ; i < na ; ++i ) {
			sPtr<ocFace2D> f = sPtr<ocFace2D>::d_cast((*args)[i]);
			if ( ! f.is_notNull() || f->shape().IsNull() ) { bad = 1; return; }
			int k = 0;
			TopoDS_Face face;
			for ( TopExp_Explorer e(f->shape(), TopAbs_FACE) ; e.More() ; e.Next() ) {
				face = TopoDS::Face(e.Current());
				++k;
			}
			if ( k == 0 ) { bad = 2; return; }
			if ( k > 1 )  { bad = 3; nf = k; return; }
			/* ★★ 断面は **平面**でなければならない (2026-09-13 に実測で判明)。
			 *   oc-face3d は #3518 から *曲面の上* にも居られるので、円柱側面を切り取った
			 *   patch をそのまま断面に渡せてしまう。ところが ThruSections は isSolid=true で
			 *   *断面そのものを蓋にする* ので、蓋が平面でないと内外が決まらず、
			 *   **符号つき体積が打ち消して 0 になる** (valid=0 だが体積だけ黙って返っていた)。
			 *   ⇒ 面の曲面種を見て先に断る (原因を名指しできる)。 */
			if ( BRepAdaptor_Surface(face).GetType() != GeomAbs_Plane ) { bad = 4; nf = i; return; }
			mk.AddWire(BRepTools::OuterWire(face));
		}
		mk.Build();
		if ( mk.IsDone() ) r = mk.Shape();
	}, errbuf, errbufsz) ) {
		*errmsg = ( errbuf != 0 && errbuf[0] != '\0' ) ? errbuf : "OCCT failed";
		return sPtr<ocShape>();
	}
	if ( bad == 1 ) { *errmsg = "every section must be a 2D region (oc-face3d)"; return sPtr<ocShape>(); }
	if ( bad == 2 ) { *errmsg = "a section has no face"; return sPtr<ocShape>(); }
	if ( bad == 3 ) {
		if ( errbuf != 0 && errbufsz > 0 ) {
			::snprintf(errbuf, (size_t)errbufsz,
			    "a section is made of %d faces; loft needs one closed outline per section "
			    "(use face(section, i) to pick one)", nf);
			*errmsg = errbuf;
		} else
			*errmsg = "a section is made of several faces";
		return sPtr<ocShape>();
	}
	if ( bad == 4 ) {
		if ( errbuf != 0 && errbufsz > 0 ) {
			::snprintf(errbuf, (size_t)errbufsz,
			    "section %d is not flat (it lies on a curved surface); loft caps the solid with "
			    "the sections themselves, so a curved section leaves no well-defined inside",
			    nf);
			*errmsg = errbuf;
		} else
			*errmsg = "a section is not flat";
		return sPtr<ocShape>();
	}
	if ( r.IsNull() ) { *errmsg = "OCCT could not build a solid through these sections"; return sPtr<ocShape>(); }
	/* ★ #3518 の 5 と同じ網: **符号つき体積が 0 なら明示エラー**。上の平面性検査で主な原因は
	 *   潰れるが、平面の断面でも退化した配置 (同じ位置に 2 枚など) は残るので出口でも見る。
	 *   ★ 閾値は **bbox の体積に対する相対** (絶対値だと寸法の単位で意味が変わる)。 */
	{
		GProp_GProps gp;
		BRepGProp::VolumeProperties(r, gp);
		double vol = gp.Mass();
		Bnd_Box bb;
		BRepBndLib::Add(r, bb);
		double scale = 1.0;
		if ( ! bb.IsVoid() ) {
			double x1, y1, z1, x2, y2, z2;
			bb.Get(x1, y1, z1, x2, y2, z2);
			double bv = (x2-x1) * (y2-y1) * (z2-z1);
			if ( bv > 0 ) scale = bv;
		}
		if ( ( ( vol < 0 ) ? -vol : vol ) <= 1e-9 * scale ) {
			*errmsg = "the sections do not bound a solid (the signed volume cancels to 0); "
			          "check that they are not coincident and that they are ordered along the path";
			return sPtr<ocShape>();
		}
	}
	return oc_wrap(r);
}

sPtr<ocShape>
ocShape::bool_from_args(sArray<sPtr<pigData> > *args, const char *kind, const char **errmsg,
                        char *errbuf, int errbufsz, const pigBreak *brk)
{
	int na = ( args != 0 ) ? args->length() : 0;
	if ( na < 2 ) { *errmsg = "needs at least two OCCT shapes"; return sPtr<ocShape>(); }
	sArray<sPtr<ocShape> > ops;
	ops.length(na);
	for ( int i = 0 ; i < na ; ++i ) {
		ops[i] = sPtr<ocShape>::d_cast((*args)[i]);
		if ( ! ops[i].is_notNull() ) { *errmsg = "needs OCCT shapes"; return sPtr<ocShape>(); }
	}
	sPtr<ocShape> r;
	/* ★ 理由の受け皿は **この呼び出しのローカル** (static を置かない)。 */
	char why[512];
	why[0] = '\0';
	if ( na == 2 ) {   /* 既存キャッシュを byte 不変に保つため 2 項は従来どおり */
		if      ( ::strcmp(kind, "union") == 0 )        r = ops[0]->op_union(ops[1], why, (int)sizeof why, brk);
		else if ( ::strcmp(kind, "intersection") == 0 ) r = ops[0]->op_intersection(ops[1], why, (int)sizeof why, brk);
		else                                            r = ops[0]->op_difference(ops[1], why, (int)sizeof why, brk);
	} else
		r = op_bool_nary(ops, kind, why, (int)sizeof why, brk);
	/* ★ OCCT はトレランスが噛み合わないと **演算そのものが失敗する**。黙って壊れた形を返す
	 *   よりよい性質なので、そのまま明示エラーにする。 */
	if ( ! r.is_notNull() ) {
		if ( errbuf != 0 && errbufsz > 0 && why[0] != '\0' ) {
			::snprintf(errbuf, (size_t)errbufsz, "%s", why);
			*errmsg = errbuf;
		} else
			*errmsg = "OCCT boolean failed (tolerances did not resolve)";
	}
	return r;
}

/* ---- ★ offset: 解析曲面を直接オフセットする ----
 * 面は厳密にオフセットされ (平面→平面・半径 r の円柱→r+d の円柱)、稜には円筒パッチ、
 * 頂点には球パッチが生成される = Steiner の公式を構成的にやっているのと同じ。
 * Join=GeomAbs_Arc が「丸める」= 球との Minkowski 和に対応する接続方式。 */
sPtr<ocShape>
ocShape::op_offset(double d, char *err, int errsz, const pigBreak *brk)
{
	if ( s_.IsNull() ) return sPtr<ocShape>();
	if ( d == 0.0 ) return oc_wrap(s_);
	TopoDS_Shape out;
	ocBreakScope br(brk);   /* ★ #3498: PerformByJoin の末尾引数が進捗/中断の口 */
	if ( ! oc_guard([&]{
		BRepOffsetAPI_MakeOffsetShape mk;
		mk.PerformByJoin(s_, d, Precision::Confusion(),
		                 BRepOffset_Skin, Standard_False, Standard_False, GeomAbs_Arc,
		                 Standard_False, br.range());
		if ( mk.IsDone() ) out = mk.Shape();
	}, err, errsz) )
		return sPtr<ocShape>();
	return oc_wrap(out);
}

/* ---- ★ #3461 affine: 変換 op の共通の入口 ----
 * translate / rotate / scale / mirror / transform を **1 本に集約**する (manifold と同じ形)。
 *
 * ★ 線形部 M が「直交 × 一様スケール」(M·Mᵀ = s²·I) なら gp_Trsf が使える。gp_Trsf は
 *   剛体 + 一様スケールしか表現できないが、そのぶん **解析曲面を解析曲面のまま**運ぶ
 *   (球は球のまま・体積は 4/3·π·r³ のまま厳密)。
 * ⚠ そうでない (非等方 scale・剪断) 場合は gp_GTrsf へ落ちる。OCCT は曲面を BSpline へ
 *   変換するので **厳密な球面という性質は失われる** (形自体は正しい)。これは表現の帰結で
 *   避けられない。黙って落とすのではなく、この差はドキュメントに書く。 */
static int
oc_is_similarity(const double e[12], double *scale_out)
{
	/* M·Mᵀ が s²·I か。s² は対角の平均で取り、非対角と対角のばらつきを相対で見る。 */
	double g[3][3];
	for ( int i = 0 ; i < 3 ; ++i )
		for ( int j = 0 ; j < 3 ; ++j ) {
			double v = 0;
			for ( int k = 0 ; k < 3 ; ++k ) v += e[i*4+k] * e[j*4+k];
			g[i][j] = v;
		}
	double s2 = (g[0][0] + g[1][1] + g[2][2]) / 3.0;
	if ( !(s2 > 0) ) return 0;
	const double tol = 1e-12;
	for ( int i = 0 ; i < 3 ; ++i )
		for ( int j = 0 ; j < 3 ; ++j ) {
			double want = ( i == j ) ? s2 : 0.0;
			if ( ::fabs(g[i][j] - want) > tol * s2 ) return 0;
		}
	if ( scale_out != 0 ) *scale_out = ::sqrt(s2);
	return 1;
}

/* ★ #3518: 変換の本体は **型に依らない** — TopoDS_Shape を動かすだけで、それが Solid か
 *   Face かを見ていない。ocShape (3D) と ocFace2D (2D) で同じものを使う。
 *   失敗は 0 を返す (out は空のまま)。 */
static int
oc_affine_raw(const TopoDS_Shape &in, const double e[12], TopoDS_Shape &out, char *err, int errsz)
{
	if ( in.IsNull() ) return 0;
	double s = 1.0;
	if ( oc_is_similarity(e, &s) ) {
		if ( ! oc_guard([&]{
			gp_Trsf t;
			t.SetValues(e[0], e[1], e[2],  e[3],
			            e[4], e[5], e[6],  e[7],
			            e[8], e[9], e[10], e[11]);
			BRepBuilderAPI_Transform mk(in, t, Standard_True);
			if ( mk.IsDone() ) out = mk.Shape();
		}, err, errsz) )
			return 0;
	} else {
		if ( ! oc_guard([&]{
			gp_GTrsf t;
			for ( int i = 0 ; i < 3 ; ++i )
				for ( int j = 0 ; j < 3 ; ++j )
					t.SetValue(i+1, j+1, e[i*4+j]);
			t.SetTranslationPart(gp_XYZ(e[3], e[7], e[11]));
			BRepBuilderAPI_GTransform mk(in, t, Standard_True);
			if ( mk.IsDone() ) out = mk.Shape();
		}, err, errsz) )
			return 0;
	}
	return out.IsNull() ? 0 : 1;
}

sPtr<ocGeom>
ocShape::op_affine(const double e[12], char *err, int errsz)
{
	TopoDS_Shape out;
	if ( ! oc_affine_raw(s_, e, out, err, errsz) ) return sPtr<ocGeom>();
	return sPtr<ocGeom>::d_cast(oc_wrap(out));
}

/* ★★ #3518: 2D も **同じ実体**で動かす。3D の変換をそのまま受けるので、面は z=0 平面の
 *   外へも出る (傾いた断面・持ち上げた断面)。これは型として正しい — ocFace2D は
 *   TopoDS_Face = 「任意の曲面 + (u,v) を切り取るワイヤ」であって平面に縛られていない。
 * ⚠ cgal / manifold の 2D は XY の 2x2 + (tx,ty) しか使わず、面外成分を **黙って捨てる**
 *   (rotate("x",45) で面積が cos45 倍になり、translate([0,0,5]) は何も起きない)。
 *   occt だけが「断面を空間に置く」を表現できる = loft の前提 (#3518)。
 * ⚠ 平面を前提にしているのは cast (cg/mf 2D へ) / polygonize / extrude・revolve の 3 箇所で、
 *   面外へ出た 2D をそこへ渡したときの扱いは #3518 の 3./5. で決める。 */
sPtr<ocGeom>
ocFace2D::op_affine(const double e[12], char *err, int errsz)
{
	TopoDS_Shape out;
	if ( ! oc_affine_raw(s_, e, out, err, errsz) ) return sPtr<ocGeom>();
	sPtr<ocFace2D> f = thNEW(ocFace2D,());
	f->set_shape(out);
	return sPtr<ocGeom>::d_cast(f);
}

/* ---- 断面 (#3514) ------------------------------------------------------------
 * ★★ 約束は cgal / manifold の section(m,P,N,mode) に **完全に合わせる** (実測して確かめた
 *   2026-09-13・箱 [0,2]^3 と球):
 *
 *     共面の面が無い     mode 0 = 断面 / mode -1 = 空 / mode +1 = 空
 *     共面の面が在る     mode 0 = 空   / mode -1 = 直下の極限 / mode +1 = 直上の極限
 *
 *   ★ 「平面ちょうど」が共面のとき空になるのは、面そのものが答えになって **一意に決まらない**
 *     から。極限を別の要素で返すのが 3 要素配列の意味 (docs の section の節)。
 *
 * ★ 断面そのものは **大きな平面の面との Common** で採る。⚠ 辺を拾って自分でワイヤに綴じる方式は
 *   採らない — 穴 (入れ子ループ) の内外判定を自前で書くことになり、ブールエンジンが
 *   既に持っている能力を writing し直すことになる。Common なら穴はそのまま穴で出る。
 * ★★ **解析曲面のまま切れる**ので、球の断面は真円 (Geom_Circle) で出る。面積は π(r²-h²) が
 *   丸め誤差の範囲で出る (メッシュ系は内接多角形なので構造的に小さい)。
 *
 * ★ 極限側 (mode ±1) は **立体自身の共面の面**がそのまま答え。どちら側の極限かは
 *   その面の **外向き法線**で決まる: 法線が +N なら立体は平面の下にある = 直下の極限。
 *   ⚠ 面の向きは TopAbs_REVERSED で反転する (これを見落とすと上下が入れ替わる)。
 * ⚠ 限界: 平面の **両側に材料がある**共面 (接している 2 立体の継ぎ目など) では、そこに
 *   境界面が無いので両方とも空になる。真の極限は両側とも断面なので、そこは答えられない。
 *
 * ★★ #3536: **断面は切った場所にそのまま返る** (運ばない)。oc-face3d の実体は TopoDS_Face
 *   なので置き場所を自分で持てる。⚠ 2026-09-14 まではここで cgal と同じ基底 (oc_section_basis)
 *   を当てて XY 平面へ運んでいた — #3526 より前の cgal (2D が z=0 に縛られていた) に座標まで
 *   合わせるためで、*当時は正しかった*。#3526 で cg/mf が切った場所に返すようになった結果、
 *   occt だけが取り残されたので外した。
 *   ★ その基底表 (軸に平行な法線は入れ替え + 符号反転 / 一般の法線は正規直交基底) は捨てて
 *     いない — **@src/h/common/affine.h@ の @plane_frame_canonical()@** に移り、occt_mf の
 *     polygonize が「面の平面を mf-cross2d の枠にする」ときに使っている。 */

/* 面が平面 pln と共面か (向きは問わない)。 */
static int
oc_face_on_plane(const TopoDS_Face &f, const gp_Pln &pln, double tol)
{
	BRepAdaptor_Surface sa(f, Standard_False);
	if ( sa.GetType() != GeomAbs_Plane ) return 0;
	const gp_Pln fp = sa.Plane();
	const gp_Dir a = fp.Axis().Direction(), b = pln.Axis().Direction();
	if ( ::fabs(::fabs(a.Dot(b)) - 1.0) > 1e-9 ) return 0;
	return ( ::fabs(pln.Distance(fp.Location())) <= tol ) ? 1 : 0;
}

sPtr<ocFace2D>
ocShape::op_section(const double P[3], const double N[3], int mode,
                    int *coplanarOut, char *err, int errsz, const pigBreak *brk) const
{
	if ( coplanarOut ) *coplanarOut = 0;
	if ( s_.IsNull() ) return sPtr<ocFace2D>();
	const double nl2 = N[0]*N[0] + N[1]*N[1] + N[2]*N[2];
	if ( !(nl2 > 0) ) return sPtr<ocFace2D>();   /* 退化した法線 */

	gp_Pln pln(gp_Pnt(P[0], P[1], P[2]), gp_Dir(N[0], N[1], N[2]));

	/* ---- 立体の大きさ (大きな切断面のサイズと共面判定の許容に使う) ---- */
	Bnd_Box bb;
	BRepBndLib::Add(s_, bb);
	if ( bb.IsVoid() ) return sPtr<ocFace2D>();
	double x0,y0,z0,x1,y1,z1;
	bb.Get(x0,y0,z0,x1,y1,z1);
	const double diag = std::sqrt((x1-x0)*(x1-x0) + (y1-y0)*(y1-y0) + (z1-z0)*(z1-z0));
	const double tol  = ( diag > 0 ) ? diag * 1e-9 : 1e-12;

	/* ---- 共面の面を集める (在るかどうかと、極限側の答えの両方に要る) ---- */
	std::vector<TopoDS_Face> below, above;   /* below = 外向き法線が +N (= 立体は平面の下) */
	for ( TopExp_Explorer ex(s_, TopAbs_FACE) ; ex.More() ; ex.Next() ) {
		const TopoDS_Face f = TopoDS::Face(ex.Current());
		if ( ! oc_face_on_plane(f, pln, tol) ) continue;
		if ( coplanarOut ) *coplanarOut = 1;
		BRepAdaptor_Surface sa(f, Standard_False);
		gp_Dir fn = sa.Plane().Axis().Direction();
		if ( f.Orientation() == TopAbs_REVERSED ) fn.Reverse();   /* ⚠ 面の向きで法線は反転する */
		const double d = fn.X()*N[0] + fn.Y()*N[1] + fn.Z()*N[2];
		( d > 0 ? below : above ).push_back(f);
	}
	const int coplanar = ( coplanarOut ) ? *coplanarOut : ( below.size() + above.size() > 0 );

	/* ---- どの面を答えにするか (約束の表そのもの) ---- */
	BRep_Builder bld;
	TopoDS_Compound acc;
	bld.MakeCompound(acc);
	if ( ! coplanar ) {
		if ( mode == 0 ) {
			/* 平面いっぱいの面と Common。穴はブールエンジンがそのまま穴で返す。
			 * ⚠⚠ 面は **立体を覆う位置と大きさ**で作ること。gp_Pln の局所座標は
			 *   その Location が原点なので、P のまま -R..R を張ると *P から遠い側が
			 *   切り落とされる*。2026-09-13 に実測: translate(box(2,4,6),[1,2,3]) を
			 *   x=2 で切ると断面積が 24 ではなく 17.93 になった (z=9 側が面の外)。
			 *   ⇒ bbox の中心を平面へ落とした点を原点にし、半径は対角長そのもの
			 *     (= 直径 2·diag) を張る。 */
			double R = ( diag > 0 ) ? diag : 1.0;
			double cx = (x0+x1)/2, cy = (y0+y1)/2, cz = (z0+z1)/2;
			const gp_Dir nd = pln.Axis().Direction();
			const double off = (cx-P[0])*nd.X() + (cy-P[1])*nd.Y() + (cz-P[2])*nd.Z();
			gp_Pln big(gp_Pnt(cx - off*nd.X(), cy - off*nd.Y(), cz - off*nd.Z()), nd);
			TopoDS_Shape cut;
			if ( ! oc_guard([&]{
				ocBreakScope br(brk);
				BRepBuilderAPI_MakeFace mf(big, -R, R, -R, R);
				if ( ! mf.IsDone() ) return;
				BRepAlgoAPI_Common op(mf.Face(), s_, br.range());
				if ( op.IsDone() && ! op.HasErrors() ) cut = op.Shape();
			}, err, errsz) )
				return sPtr<ocFace2D>();
			if ( ! cut.IsNull() )
				for ( TopExp_Explorer ex(cut, TopAbs_FACE) ; ex.More() ; ex.Next() )
					bld.Add(acc, ex.Current());
		}
		/* mode ±1 は空 (共面が無ければ極限は [0] と同じなので、重ねて返さない) */
	} else {
		/* ⚠ mode 0 は空 — 面そのものが答えになって一意に決まらないため。 */
		const std::vector<TopoDS_Face>& pick = ( mode < 0 ) ? below : ( mode > 0 ? above : below );
		if ( mode != 0 )
			for ( size_t i = 0 ; i < pick.size() ; ++i ) {
				TopoDS_Face f = pick[i];
				f.Orientation(TopAbs_FORWARD);   /* 2D 領域として持つので向きは正に揃える */
				bld.Add(acc, f);
			}
	}

	/* ---- ★★ #3536: **切った場所にそのまま返す** (運搬しない) ----
	 *   ⚠ 2026-09-14 まで、ここで oc_section_basis の基底を当てて断面を XY 平面へ
	 *     **運んでいた**。#3526 より前の cgal (2D が z=0 に縛られていた) に座標まで
	 *     合わせるための実装で、*当時は正しかった*。
	 *   ⇒ #3526 で cgal / manifold の 2D が枠 (平面) を持ち、断面を切った場所に返すように
	 *     なった ⇒ occt だけが取り残され、**同じ式が 2 通りの答えを返す**状態になっていた。
	 *   ★ oc-face3d の実体は TopoDS_Face = 任意曲面上のトリム面なので、**元から置き場所を
	 *     持てる** (cg/mf のように枠のフィールドを足す必要が無い)。運ばなければ在るべき所に在る。
	 *   ★ 運搬を外した副産物として、gp_Trsf / gp_GTrsf の場合分け (曲面が BSpline へ
	 *     近似変換されるのを避けるための分岐) も要らなくなった — **変換そのものを通さない**ので、
	 *     球の断面は Geom_Circle のまま、面積は π(r²-h²) がそのまま出る。
	 *   ⇒ メッシュ系へ渡すときの座標系の付け替えは occt_mf の polygonize が受け持つ
	 *     (面の平面を mf-cross2d の枠として引き継ぐ)。 */
	sPtr<ocFace2D> out = thNEW(ocFace2D,());
	out->set_shape(acc);
	return out;
}

/* ---- ★ fillet / chamfer — B-rep でしか厳密に書けない加工 (#3437) ----
 * どちらも **全ての稜**に一律で適用する。稜を選ぶ語彙 (「この面とこの面の間だけ」) は
 * srava 側に無いので、部分適用は将来の課題。
 *
 * ★ chamfer は稜だけでなく **隣接する面のどちらか**を渡す必要がある (削ぐ向きの基準)。
 *   TopExp::MapShapesAndAncestors で稜 → 面の対応を作って、最初の面を渡す。
 *   45 度 (対称) なので、どちらの面を渡しても結果は同じ。
 *
 * ★ OCCT は「作れない」で普通に失敗する (半径が大きすぎて自己交差する等)。失敗は null
 *   にして呼び元が明示エラーにする。**黙って入力を返さない**。 */
sPtr<ocShape>
ocShape::op_fillet(double r, char *err, int errsz, const pigBreak *brk)
{
	if ( s_.IsNull() ) return sPtr<ocShape>();
	if ( r == 0.0 ) return oc_wrap(s_);
	if ( r < 0.0 ) return sPtr<ocShape>();   /* 負の丸めは定義しない (収縮は offset の仕事) */
	TopoDS_Shape out;
	ocBreakScope br(brk);   /* ★ #3498 */
	if ( ! oc_guard([&]{
		BRepFilletAPI_MakeFillet mk(s_);
		int n = 0;
		for ( TopExp_Explorer e(s_, TopAbs_EDGE) ; e.More() ; e.Next() ) {
			mk.Add(r, TopoDS::Edge(e.Current()));
			++n;
		}
		if ( n == 0 ) return;            /* 稜が無い = 丸める対象が無い */
		mk.Build(br.range());
		if ( mk.IsDone() ) out = mk.Shape();
	}, err, errsz) )
		return sPtr<ocShape>();
	return oc_wrap(out);
}

sPtr<ocShape>
ocShape::op_chamfer(double d, char *err, int errsz, const pigBreak *brk)
{
	if ( s_.IsNull() ) return sPtr<ocShape>();
	if ( d == 0.0 ) return oc_wrap(s_);
	if ( d < 0.0 ) return sPtr<ocShape>();
	TopoDS_Shape out;
	ocBreakScope br(brk);   /* ★ #3498 */
	if ( ! oc_guard([&]{
		BRepFilletAPI_MakeChamfer mk(s_);
		int n = 0;
		/* ★ Add(距離, 稜) は **両側に同じ距離** = 45 度の削ぎになる。
		 *   非対称にしたいときだけ Add(d1, d2, 稜, 基準面) で面を指定する必要がある。 */
		for ( TopExp_Explorer e(s_, TopAbs_EDGE) ; e.More() ; e.Next() ) {
			mk.Add(d, TopoDS::Edge(e.Current()));
			++n;
		}
		if ( n == 0 ) return;
		mk.Build(br.range());
		if ( mk.IsDone() ) out = mk.Shape();
	}, err, errsz) )
		return sPtr<ocShape>();
	return oc_wrap(out);
}

/* ---- wire 形式 (D_META 4CC "BREP") ----
 *   [u32 blocklen][block] … [u32 0]   ブロックを繋ぐと BinTools::Write のバイナリ BREP
 * ★ #3507: 旧形式は [u64 nbytes][bytes]。全長の前置は書き手に全文を作らせるので、
 *   ブロック分割へ変えた。⚠ **読みだけは全文バッファ 1 本が下限** —
 *   BinTools_IStream::GoTo() が位置を戻すため (書きは前向きにしか進まない)。 */
void
ocShape::encode(ocChunkSink &sink)
{
	/* ★★ #3507 (2026-09-10): BinTools を **ブロック分割**で直接流す (全文バッファを作らない)。
	 *   BinTools_OStream は位置を自前で数えて参照を前向きに書くので、書きは seek しない。 */
	blockframe::obuf<ocChunkSink> ob(sink);
	std::ostream                  os(&ob);
	if ( ! s_.IsNull() )
		BinTools::Write(s_, os);
	os.flush();
	ob.finish();
}

void
ocShape::decode(ocChunkSource &src)
{
	/* ★ #3507: 読みは **全文バッファ 1 本が下限** — BinTools_IStream::GoTo() が位置を戻すため。
	 *   ブロック列を 1 本に集め、その上に streambuf を被せる (@istringstream@ は中身を
	 *   もう 1 本複製するので使わない = 2 本 → 1 本)。 */
	std::string buf;
	if ( ! blockframe::read_all(src, buf, (uint64_t)16 * 1024 * 1024 * 1024) ) {
		set_decode_err("the stored shape is corrupt or implausibly large (over 16GiB)");
		return;
	}
	if ( buf.empty() ) { set_decode_err("the stored shape is empty"); return; }
	blockframe::membuf mb(buf.data(), buf.size());
	std::istream       is(&mb);
	BinTools::Read(s_, is);
	if ( s_.IsNull() ) set_decode_err("OCCT BinTools could not read the stored shape (unsupported or corrupt B-rep)");
}

/* ★ #3503 続き: 中断なしの入口 (codec / write_to の既存の呼び手はこちら)。 */
bool
ocShape::write_to(const char *path, const char *unit)
{
	return write_to(path, unit, 0);
}

bool
ocShape::write_to(const char *path, const char *unit, const pigBreak *wbrk)
{
	(void)unit;   /* ★ 使うのは IGES だけ (STEP は Interface_Static 側・.brep は単位を持たない) */
	(void)wbrk;   /* ★ STEP/IGES を両方 OFF にしたビルドでは使い道が無い (.brep に進捗の口が無い) */
	if ( s_.IsNull() ) return false;
#ifdef SRAVA_OCCT_STEP
	const char *dot = ::strrchr(path, '.');
	if ( dot != 0 && ( oc_ieq(dot, ".step") || oc_ieq(dot, ".stp") ) ) {
		/* ★ STEP (ISO 10303) — **解析曲面のまま**書ける唯一の出口。
		 *   triangulate の出口 (mf-mesh3d) は三角形へ落とすが、こちらは B-rep のまま
		 *   他の CAD へ渡せる。「表現力を落とさずに外へ出せる」ことがこの型の価値なので、
		 *   ここは占有的に重要。AP203 (機械部品の形状) を既定にする。 */
		STEPControl_Writer w;
		Interface_Static::SetCVal("write.step.schema", "AP203");
		/* ★ #3503 続き: **Transfer だけ**が Message_ProgressRange を取る。
		 *   ⚠ Write(filename) には口が無い (多重定義も無い)。⇒ 変換は中断できるが、
		 *     ファイルへの書き出しは始まったら最後まで走る。長さの主は Transfer 側なので
		 *     実用上はこれで足りるが、「export は中断できる」と読まないこと。 */
		ocBreakScope br(wbrk);
		if ( w.Transfer(s_, STEPControl_AsIs, Standard_True, br.range()) != IFSelect_RetDone )
			return false;
		if ( wbrk != 0 && wbrk->cancelled() ) return false;   /* 書き出す前に降りる */
		return w.Write(path) == IFSelect_RetDone;
	}
#endif
#ifdef SRAVA_OCCT_IGES
	{
		const char *dot2 = ::strrchr(path, '.');
		if ( dot2 != 0 && ( oc_ieq(dot2, ".iges") || oc_ieq(dot2, ".igs") ) ) {
			/* ★ #3513: IGES — **STEP の前世代**。曲面は運べるが位相が弱い。レガシー資産の
			 *   受け渡し用で、新規に選ぶ形式ではない (新しく作るなら STEP)。
			 * ★★ モードは **BRep (1)** を使う。既定の Faces (0) は面をばらばらの
			 *   IGES エンティティとして並べるので、読み戻しても殻にならず **体積が出ない**。
			 * ★ 単位は IGES ファイル自身が持てる (STEP と違い writer の引数)。⇒ srava の
			 *   export(path, m, unit) の unit をそのまま渡す。空なら MM。
			 *   ⚠ OCCT が受けるのは IGES の単位名 (MM/CM/M/IN/FT...) で、mm/cm/in の
			 *     小文字は通らないので大文字へ寄せる。知らない綴りは MM に落とす
			 *     (黙って別の単位で書くより、既定に寄せて docs に書く方が安全)。 */
			char un[8];
			int  ui = 0;
			for ( const char *u = ( unit != 0 ? unit : "" ) ; *u != 0 && ui < 4 ; ++u )
				un[ui++] = (char)( ( *u >= 'a' && *u <= 'z' ) ? ( *u - 'a' + 'A' ) : *u );
			un[ui] = '\0';
			const char *iu = "MM";
			if ( ::strcmp(un, "MM") == 0 || ::strcmp(un, "CM") == 0 || ::strcmp(un, "M") == 0 ||
			     ::strcmp(un, "IN") == 0 || ::strcmp(un, "FT") == 0 || ::strcmp(un, "MI") == 0 ||
			     ::strcmp(un, "KM") == 0 || ::strcmp(un, "MIL") == 0 || ::strcmp(un, "UM") == 0 )
				iu = un;
			IGESControl_Controller::Init();   /* ⚠ これを呼ばないと writer が空のモデルを書く */
			IGESControl_Writer w(iu, 1);
			ocBreakScope br2(wbrk);
			if ( ! w.AddShape(s_, br2.range()) ) return false;
			if ( wbrk != 0 && wbrk->cancelled() ) return false;
			w.ComputeModel();
			return w.Write(path) == Standard_True;
		}
	}
#endif
	/* .brep (OCCT 固有のバイナリ/ASCII 形式)。⚠ BinTools / BRepTools には進捗の口が無い。 */
	return BRepTools::Write(s_, path) == Standard_True;
}

/* ---- ★ 入口: STEP / BREP を読む (#3437) ----
 * ★★ **これは「mesh → B-rep」ではない**。STEP も BREP も **解析曲面をそのまま持っている**
 *   形式なので、読むだけで B-rep が手に入る (復元も推定もしない)。
 *   三角形群から解析曲面を復元する reverse engineering とは別物であり、そちらは
 *   「入口を作らない」という設計判断のまま変えない (ocShape.h 冒頭)。
 * 失敗は null。**部分的に読めた形を黙って返さない**。 */
/* ---- ★★ #3545 段 5: op から移してきた生成器 (2026-09-18) --------------------------
 * ⚠ try/catch も **こちら側**に置く — @catch (const Standard_Failure&)@ が op TU に在ると、
 *   それだけで RTTI の型インスタンスがその .o に emit される。 */
static void
oc_fail(char *err, int errsz, const char *op, const Standard_Failure &e)
{
	if ( ! err ) return;
	const char *msg = ( e.GetMessageString() && e.GetMessageString()[0] ) ? e.GetMessageString()
	                                                                      : "no message";
	::snprintf(err, errsz, "%s: OCCT failed [%s] (%s)", op, e.DynamicType()->Name(), msg);
}

sPtr<ocShape>
ocShape::make_cone(double r, double h, char *err, int errsz)
{
	try {
		/* ★ 円錐は **解析曲面** (円錐面 1 + 平面 1)。原点中心・軸 +Z で、
		 *   BRepPrimAPI_MakeCone の基準点が底面中心なので h/2 下げる。 */
		gp_Ax2 ax(gp_Pnt(0, 0, -h/2), gp_Dir(0, 0, 1));
		BRepPrimAPI_MakeCone mk(ax, r, 0.0, h);
		TopoDS_Shape sh = mk.Shape();
		if ( sh.IsNull() ) {
			if ( err ) ::snprintf(err, errsz, "cone: OCCT produced a null shape");
			return sPtr<ocShape>();
		}
		sPtr<ocShape> out = thNEW(ocShape,());
		out->set_shape(sh);
		return out;
	} catch ( const Standard_Failure& e ) { oc_fail(err, errsz, "cone", e); return sPtr<ocShape>(); }
}

sPtr<ocShape>
ocShape::make_cylinder(double r, double h, char *err, int errsz)
{
	try {
		/* 他カーネルの box / sphere と同じく **原点中心**・軸は +Z。
		 * BRepPrimAPI_MakeCylinder は基準点が底面中心なので h/2 下げる。 */
		gp_Ax2 ax(gp_Pnt(0, 0, -h/2), gp_Dir(0, 0, 1));
		BRepPrimAPI_MakeCylinder mk(ax, r, h);
		/* ★ プリミティブは遅延構築。IsDone() は立たないので IsNull() で見る (ocaBox と同じ罠)。 */
		TopoDS_Shape sh = mk.Shape();
		if ( sh.IsNull() ) {
			if ( err ) ::snprintf(err, errsz, "cylinder: OCCT produced a null shape");
			return sPtr<ocShape>();
		}
		sPtr<ocShape> out = thNEW(ocShape,());
		out->set_shape(sh);
		return out;
	} catch ( const Standard_Failure& e ) { oc_fail(err, errsz, "cylinder", e); return sPtr<ocShape>(); }
}

sPtr<ocShape>
ocShape::make_torus(double R, double r, char *err, int errsz)
{
	try {
		/* 原点中心・軸は +Z (穴が Z 方向に空く)。 */
		gp_Ax2 ax(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1));
		BRepPrimAPI_MakeTorus mk(ax, R, r);
		TopoDS_Shape sh = mk.Shape();
		if ( sh.IsNull() ) {
			if ( err ) ::snprintf(err, errsz, "torus: OCCT produced a null shape");
			return sPtr<ocShape>();
		}
		sPtr<ocShape> out = thNEW(ocShape,());
		out->set_shape(sh);
		return out;
	} catch ( const Standard_Failure& e ) { oc_fail(err, errsz, "torus", e); return sPtr<ocShape>(); }
}

/* ★ 三角形の列を縫って閉じた立体にする (tetrahedron / pyramid / icosphere の共通部)。
 *   ⚠ 縫合の許容 1e-7 は元の op と同じ。失敗の文言も op 名で作る。 */
static sPtr<ocShape>
oc_sew_solid(const std::vector<gp_Pnt> &v, const std::vector<int> &t,
             const char *op, char *err, int errsz)
{
	BRepBuilderAPI_Sewing sew(1.0e-7);
	for ( size_t i = 0 ; i + 2 < t.size() + 1 && i < t.size() ; i += 3 ) {
		BRepBuilderAPI_MakePolygon tri(v[(size_t)t[i]], v[(size_t)t[i+1]], v[(size_t)t[i+2]],
		                               Standard_True);
		BRepBuilderAPI_MakeFace f(tri.Wire());
		if ( ! f.IsDone() ) {
			if ( err ) ::snprintf(err, errsz, "%s: could not build a face", op);
			return sPtr<ocShape>();
		}
		sew.Add(f.Face());
	}
	sew.Perform();
	TopoDS_Shape sh = sew.SewedShape();
	if ( sh.IsNull() || sh.ShapeType() != TopAbs_SHELL ) {
		if ( err ) ::snprintf(err, errsz, "%s: the faces did not sew into a closed shell", op);
		return sPtr<ocShape>();
	}
	BRepBuilderAPI_MakeSolid ms(TopoDS::Shell(sh));
	if ( ! ms.IsDone() ) {
		if ( err ) ::snprintf(err, errsz, "%s: the shell could not be closed into a solid", op);
		return sPtr<ocShape>();
	}
	sPtr<ocShape> out = thNEW(ocShape,());
	out->set_shape(ms.Solid());
	return out;
}

sPtr<ocShape>
ocShape::make_tetrahedron(double r, char *err, int errsz)
{
	try {
		/* ★ 正四面体も **平面 4 枚**なのでメッシュ系と厳密に一致する。
		 *   common/solids.h の make_tetrahedron と同じ座標 (立方体の対角 4 頂点 × r/√3)。 */
		const double s = r / std::sqrt(3.0);
		std::vector<gp_Pnt> v;
		v.push_back(gp_Pnt( s,  s,  s)); v.push_back(gp_Pnt( s, -s, -s));
		v.push_back(gp_Pnt(-s,  s, -s)); v.push_back(gp_Pnt(-s, -s,  s));
		static const int F[12] = { 0,1,2, 0,2,3, 0,3,1, 1,3,2 };
		std::vector<int> t(F, F + 12);
		return oc_sew_solid(v, t, "tetrahedron", err, errsz);
	} catch ( const Standard_Failure& e ) { oc_fail(err, errsz, "tetrahedron", e); return sPtr<ocShape>(); }
}

sPtr<ocShape>
ocShape::make_pyramid(int n, double h, double r, char *err, int errsz)
{
	try {
		/* ★ 平面 n+1 枚でできる **多面体**なのでメッシュ系と厳密に一致する (prism と同じ理由)。
		 *   底面は z=0 の正 n 角形・頂点は (0,0,h)。common/solids.h の make_pyramid と同じ座標。
		 *   ⚠ 底面は多角形 1 枚なので、三角形しか受けない oc_sew_solid には載せられない。
		 *     ⇒ ここだけ自前で縫う。 */
		std::vector<gp_Pnt> ring;
		for ( int k = 0 ; k < n ; ++k ) {
			double a = 2.0 * srava_geo::SOLID_PI * (double)k / (double)n;
			ring.push_back(gp_Pnt(r * std::cos(a), r * std::sin(a), 0.0));
		}
		gp_Pnt apex(0, 0, h);
		BRepBuilderAPI_Sewing sew(1.0e-7);
		for ( int k = 0 ; k < n ; ++k ) {          /* 側面 */
			BRepBuilderAPI_MakePolygon tri(ring[(size_t)k], ring[(size_t)((k + 1) % n)], apex,
			                               Standard_True);
			BRepBuilderAPI_MakeFace f(tri.Wire());
			if ( ! f.IsDone() ) {
				if ( err ) ::snprintf(err, errsz, "pyramid: could not build a side face");
				return sPtr<ocShape>();
			}
			sew.Add(f.Face());
		}
		BRepBuilderAPI_MakePolygon base;           /* 底面 */
		for ( int k = 0 ; k < n ; ++k ) base.Add(ring[(size_t)k]);
		base.Close();
		BRepBuilderAPI_MakeFace bf(base.Wire());
		if ( ! bf.IsDone() ) {
			if ( err ) ::snprintf(err, errsz, "pyramid: could not build the base face");
			return sPtr<ocShape>();
		}
		sew.Add(bf.Face());
		sew.Perform();
		TopoDS_Shape sh = sew.SewedShape();
		if ( sh.IsNull() || sh.ShapeType() != TopAbs_SHELL ) {
			if ( err ) ::snprintf(err, errsz, "pyramid: the faces did not sew into a closed shell");
			return sPtr<ocShape>();
		}
		BRepBuilderAPI_MakeSolid ms(TopoDS::Shell(sh));
		if ( ! ms.IsDone() ) {
			if ( err ) ::snprintf(err, errsz, "pyramid: the shell could not be closed into a solid");
			return sPtr<ocShape>();
		}
		sPtr<ocShape> out = thNEW(ocShape,());
		out->set_shape(ms.Solid());
		return out;
	} catch ( const Standard_Failure& e ) { oc_fail(err, errsz, "pyramid", e); return sPtr<ocShape>(); }
}

sPtr<ocShape>
ocShape::make_icosphere(double r, int n, char *err, int errsz)
{
	try {
		/* ★ icosphere は **測地多面体** (平面の三角形の集まり) であって近似球ではない。
		 *   occt でも平面 Face を縫えば **厳密に**同じ立体になる (sphere と違って
		 *   一致検査の表に入れられる)。 */
		struct OcSink {
			std::vector<gp_Pnt> v;
			std::vector<int>    t;
			int  add_vertex(double x, double y, double z) { v.push_back(gp_Pnt(x,y,z)); return (int)v.size()-1; }
			void add_triangle(int a, int b, int c) { t.push_back(a); t.push_back(b); t.push_back(c); }
		} sink;
		srava_geo::make_geodesic(srava_geo::SEED_ICOSAHEDRON, n, r, sink);
		return oc_sew_solid(sink.v, sink.t, "icosphere", err, errsz);
	} catch ( const Standard_Failure& e ) { oc_fail(err, errsz, "icosphere", e); return sPtr<ocShape>(); }
}

sPtr<ocFace2D>
ocFace2D::make_circle(double r, char *err, int errsz)
{
	try {
		/* XY 平面・原点中心。 */
		gp_Ax2 ax(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1));
		Handle(Geom_Circle) c = new Geom_Circle(ax, r);
		BRepBuilderAPI_MakeEdge me(c);
		if ( ! me.IsDone() ) { if ( err ) ::snprintf(err, errsz, "circle: could not build the edge");
		                       return sPtr<ocFace2D>(); }
		BRepBuilderAPI_MakeWire mw(me.Edge());
		if ( ! mw.IsDone() ) { if ( err ) ::snprintf(err, errsz, "circle: could not build the wire");
		                       return sPtr<ocFace2D>(); }
		BRepBuilderAPI_MakeFace mf(mw.Wire());
		if ( ! mf.IsDone() ) { if ( err ) ::snprintf(err, errsz, "circle: could not build the face");
		                       return sPtr<ocFace2D>(); }
		sPtr<ocFace2D> out = thNEW(ocFace2D,());
		out->set_shape(mf.Face());
		return out;
	} catch ( const Standard_Failure& e ) { oc_fail(err, errsz, "circle", e); return sPtr<ocFace2D>(); }
}

sPtr<ocShape>
ocShape::read_file(const char *path, const pigBreak *brk)
{
	(void)brk;   /* ★ STEP/IGES を両方 OFF にしたビルドでは使い道が無い (BRepTools::Read に口が無い) */
	if ( path == 0 ) return sPtr<ocShape>();
#ifdef SRAVA_OCCT_STEP
	const char *dot = ::strrchr(path, '.');
	if ( dot != 0 && ( oc_ieq(dot, ".step") || oc_ieq(dot, ".stp") ) ) {
		STEPControl_Reader r;
		if ( r.ReadFile(path) != IFSelect_RetDone ) return sPtr<ocShape>();
		/* ★ #3503 続き: 巨大な STEP の取り込みは長い。TransferRoots は
		 *   Message_ProgressRange を取るので中断できる (ReadFile 自体には口が無い)。
		 *   ⚠ 中断されると NbShapes()==0 になり「読めなかった」と区別がつかないので、
		 *     呼び手が旗を見て弾く (ocaImport)。 */
		ocBreakScope br(brk);
		r.TransferRoots(br.range());
		if ( r.NbShapes() < 1 ) return sPtr<ocShape>();
		return oc_wrap(r.OneShape());
	}
#endif
#ifdef SRAVA_OCCT_IGES
	{
		const char *dot2 = ::strrchr(path, '.');
		if ( dot2 != 0 && ( oc_ieq(dot2, ".iges") || oc_ieq(dot2, ".igs") ) ) {
			IGESControl_Controller::Init();
			IGESControl_Reader r;
			if ( r.ReadFile(path) != IFSelect_RetDone ) return sPtr<ocShape>();
			/* ★ STEP と同じ作法: TransferRoots だけが進捗 (= 中断) の口を持つ。 */
			ocBreakScope br2(brk);
			r.TransferRoots(br2.range());
			if ( r.NbShapes() < 1 ) return sPtr<ocShape>();
			return oc_wrap(r.OneShape());
		}
	}
#endif
	TopoDS_Shape sh;
	BRep_Builder b;
	if ( ! BRepTools::Read(sh, path, b) ) return sPtr<ocShape>();
	return oc_wrap(sh);
}

sPtr<ocGeom>
ocGeom::create_for_meta(const uint8_t *meta, int len)
{
	if ( meta == 0 || len < 4 )
		return sPtr<ocGeom>();
	if ( ::memcmp(meta, OC_TAG, 4) == 0 )
		return sPtr<ocGeom>::d_cast(thNEW(ocShape,()));
	/* ★ #3471: 2D 領域 (平面上の Face)。中身は同じ BinTools のバイナリだが、
	 *   **型が違うので 4CC も分ける** (同じタグだと reader が 2D/3D を見分けられない)。 */
	if ( ::memcmp(meta, OC2_TAG, 4) == 0 )
		return sPtr<ocGeom>::d_cast(thNEW(ocFace2D,()));
	/* ★ MFM3 (メッシュ) の分岐は **撤去した** (akira-project #3452)。occt が作っていた
	 *   内部クラス ocMesh は「型名だけ mf-mesh3d を借りた別クラス」で、本家 manifold の
	 *   読み手と競合していた。B-rep → メッシュの出口は occt_mf.so が持ち、そこで作った
	 *   **本物の mfMesh** を読むのは manifold 本家の reader である。 */
	return sPtr<ocGeom>();
}

/* ═══════════════════════════════════════════════════════════════════════
 * ★ #3471: ocFace2D — 平面上の TopoDS_Face で表す 2D 領域。
 *   encode/decode は ocShape と同じ BinTools。型名と 4CC だけが違う。
 * ═══════════════════════════════════════════════════════════════════════ */

sPtr<stdString>
ocFace2D::get_str()
{
	int nf = 0;
	for ( TopExp_Explorer e(s_, TopAbs_FACE) ; e.More() ; e.Next() ) ++nf;
	char buf[80];
	::snprintf(buf, sizeof buf, "<cross2d:occt faces=%d area=%.6g>", nf, area());
	return thNEW(stdString,(buf));
}

/* ---- ★★ #3518 の 4: 面 ∩ 立体 / 面 − 立体 ----------------------------------
 * OCCT のブールは **次元が違う相手でも動く**。面を立体で切ると「曲面上に切り取られた面」に
 * なり、曲面種はそのまま保たれる (円筒面は円筒面のまま)。
 * ⚠ 3D どうしの op_union / op_intersection と **同じ作法**: 失敗は null で返し、
 *   呼び手が明示エラーにする (黙って壊れた形を返さない)。中断 (#3498) も同じ。 */
sPtr<ocFace2D>
ocFace2D::op_bool_solid(const TopoDS_Shape &solid, int kind, char *err, int errsz,
                        const pigBreak *brk)
{
	if ( s_.IsNull() || solid.IsNull() ) return sPtr<ocFace2D>();
	TopoDS_Shape r;
	ocBreakScope br(brk);   /* ★ #3498: 中断機構 (brk==0 ならダミー range) */
	if ( kind == 0 ) {
		if ( ! oc_guard([&]{ BRepAlgoAPI_Common op(s_, solid, br.range());
			if ( op.IsDone() ) r = op.Shape(); }, err, errsz) ) return sPtr<ocFace2D>();
	} else {
		if ( ! oc_guard([&]{ BRepAlgoAPI_Cut op(s_, solid, br.range());
			if ( op.IsDone() ) r = op.Shape(); }, err, errsz) ) return sPtr<ocFace2D>();
	}
	if ( r.IsNull() ) return sPtr<ocFace2D>();
	/* ⚠ 面が 1 枚も残らない (立体と交わらない / 完全に含まれる) ことはある。
	 *   ★ それは **空の 2D** であってエラーではない (空集合は正当な答え)。
	 *     ⇒ 呼び手が area()==0 で判断できる。ここで断ると「交わらない」が書けなくなる。 */
	sPtr<ocFace2D> out = thNEW(ocFace2D,());
	out->set_shape(r);
	return out;
}

/* ---- ★★ #3544: 幾何の述語 — **本当に z=0 平面に居るか** --------------------
 * cgMesh2D::frame_is_default() に対応する。★ この 1 つから **名乗り (type_name) と
 * cast (降格) の可否の両方**が決まる — 名乗りを別のビットで持たないことで、
 * 「幾何は z=0 の外なのに cross2d を名乗る」矛盾した値を作れなくしている
 * (ひさ判断 2026-09-17・案 i。⇒ ocShape.h の type_name の注記)。
 *
 * ★ 判定は bbox の z 幅で行う。Bezier / B-spline 面は **制御点が平面に在るときに限り**
 *   その平面に収まる (凸包性質) ので、poles の箱の z 幅が 0 ⇔ 面が z=0 に在る、で一致する。
 *   ⇒ #3546 で据え置いた「poles の箱」がここでは **ちょうど正しい道具**になる。
 * ⚠ 稜だけの値 (HLR の出力など・面 0 枚) でも箱は取れるので、そのまま効く。 */
int
ocFace2D::on_z0_plane(double tol) const
{
	if ( s_.IsNull() ) return 1;            /* 空は z=0 に居るとみなす (降格を断らない) */
	Bnd_Box b;
	oc_add_bbox(s_, b);
	if ( b.IsVoid() ) return 1;             /* 中身が無い = 同上 */
	b.SetGap(0.0);
	double mn[3], mx[3];
	b.Get(mn[0], mn[1], mn[2], mx[0], mx[1], mx[2]);
	return ( ::fabs(mn[2]) <= tol && ::fabs(mx[2]) <= tol ) ? 1 : 0;
}

/* ---- ★ #3518 の 6: 2D の bbox / 重心 (3D と同じ語彙で答える) ---- */
int
ocFace2D::op_bbox(double mn[3], double mx[3]) const
{
	mn[0] = mn[1] = mn[2] = mx[0] = mx[1] = mx[2] = 0.0;
	if ( s_.IsNull() ) return 2;
	Bnd_Box b;
	/* ★★ #3546: ここは 3D (ocShape::op_bbox) と **食い違っていた** — 引数なしの Add は
	 *   useTriangulation が **既定 (true)** で、三角形が付いている面では 3D と *逆方向* に
	 *   (曲面の内側へ) 誤る。⚠ さらに SetGap(0.0) も無く、面の bbox にだけ 1e-7 の余裕が
	 *   乗っていた。⇒ 3D と同じ経路・同じ後始末に揃える。 */
	oc_add_bbox(s_, b);
	if ( b.IsVoid() ) return 2;
	b.SetGap(0.0);
	b.Get(mn[0], mn[1], mn[2], mx[0], mx[1], mx[2]);
	return 2;
}

int
ocFace2D::op_centroid(double c[3]) const
{
	c[0] = c[1] = c[2] = 0.0;
	if ( s_.IsNull() ) return 2;
	GProp_GProps g;
	BRepGProp::SurfaceProperties(s_, g);   /* ★ 面積重心 */
	gp_Pnt p = g.CentreOfMass();
	c[0] = p.X(); c[1] = p.Y(); c[2] = p.Z();
	return 2;
}

/* ---- ★★ #3547: 2D でも「測る」 (2026-09-18) --------------------------------
 * ⚠ どちらも **3D と同じ道具**で書ける (TopExp / BRepAlgoAPI_Check は TopoDS_Shape を取る)。
 *   穴だったのは型のほうで、計算ではない。 */
int
ocFace2D::nverts() const
{
	/* ★ 3D (ocShape::nverts) と **同じ数え方** — TopExp_Explorer は同じ頂点を稜の数だけ
	 *   返すので map で潰す。⇒ 矩形は 4・円は 1 (閉じた稜の継ぎ目の頂点 1 つ)。
	 *   ⚠ *三角形の頂点ではなく稜の端点*。輪郭が曲線のまま保たれているので、
	 *     mesh 系の nverts(2D) (= 折れ線の点数) とは構造的に違う値になる。 */
	TopTools_IndexedMapOfShape m;
	TopExp::MapShapes(s_, TopAbs_VERTEX, m);
	return m.Extent();
}

int
ocFace2D::op_valid(const pigBreak *brk) const
{
	if ( s_.IsNull() ) return 0;                 /* ① 空でない */
	int nf = 0;
	for ( TopExp_Explorer e(s_, TopAbs_FACE) ; e.More() ; e.Next() ) ++nf;
	if ( nf == 0 ) return 0;
	/* ② 面積を持ちうるか。⚠ 3D の「閉じている」に当たる条件 (ヘッダの注記)。
	 *   ★ 面積は GProp なので **曲線のまま**測る (多角形近似を経ない)。 */
	if ( ! (area() > 0.0) ) return 0;
	/* ③ 自己交差。★ 3D と同じ BRepAlgoAPI_Check (BRepCheck_Analyzer + BOPAlgo_CheckerSI)。
	 *   ⚠ oc_edges_manifold は **呼ばない** — あれは「境界辺が無い」を見るもので、
	 *     2D 領域は縁を持つのが正常。3D の条件をそのまま持ち込むと妥当な面が全部赤くなる。
	 *   ⚠ OCCT は投げることがあるのでここで受ける (ワーカースレッド由来だと agent ごと死ぬ)。 */
	try {
		ocBreakScope br(brk);
		BRepAlgoAPI_Check chk(s_);
		chk.SetRunParallel(Standard_False);
		chk.Perform(br.range());
		return chk.IsValid() ? 1 : 0;
	} catch ( const Standard_Failure& ) {
		return 0;
	} catch ( ... ) {
		return 0;
	}
}

/* ---- ★★ #3547 ②: 面内オフセット と offset_thicken (2026-09-18) --------------------
 * ⚠ **2 つは別の操作**。名前を分けたのはそのため (ひさ裁定):
 *     offset(2D,d)          面の *中で* 輪郭を動かす  → 2D のまま (cg / mf と同じ約束)
 *     offset_thicken(2D,d)  面に厚みを付ける          → 3D (片側 d)
 *   ⇒ 同じ名前で型が上がると、cg / mf は 2D を返し occt だけ 3D を返すことになる
 *     (*-face3d は「曲面」ではなく「空間に置かれた」の意味で、平面も大量に含むため)。 */

/* ---- ★★ #3547 ④: 頂点を読む 3 つ組 (2026-09-18) ---------------------------------
 * ★ 数える側 (nverts) と **同じ地図**を使う。⇒ verts(v) の i 番目 == vert(v,i) が構造的に成り立つ
 *   (別々に列を作ると、どちらかを直したときに黙ってずれる — #3527 の⚠)。 */
static int
oc_vert_at(const TopoDS_Shape &s, int i, double p[3])
{
	p[0] = p[1] = p[2] = 0.0;
	if ( s.IsNull() ) return 0;
	TopTools_IndexedMapOfShape m;
	TopExp::MapShapes(s, TopAbs_VERTEX, m);
	if ( i < 0 || i >= m.Extent() ) return 0;
	gp_Pnt g = BRep_Tool::Pnt(TopoDS::Vertex(m(i + 1)));   /* ⚠ OCCT の地図は 1 始まり */
	p[0] = g.X(); p[1] = g.Y(); p[2] = g.Z();
	return 1;
}
static int
oc_verts_all(const TopoDS_Shape &s, std::vector<double> &xyz)
{
	xyz.clear();
	if ( s.IsNull() ) return 0;
	TopTools_IndexedMapOfShape m;
	TopExp::MapShapes(s, TopAbs_VERTEX, m);
	for ( int i = 1 ; i <= m.Extent() ; ++i ) {
		gp_Pnt g = BRep_Tool::Pnt(TopoDS::Vertex(m(i)));
		xyz.push_back(g.X()); xyz.push_back(g.Y()); xyz.push_back(g.Z());
	}
	return m.Extent();
}

int ocShape::vert_at(int i, double p[3]) const        { return oc_vert_at(s_, i, p); }
int ocShape::verts_all(std::vector<double> &x) const  { return oc_verts_all(s_, x); }
int ocFace2D::vert_at(int i, double p[3]) const       { return oc_vert_at(s_, i, p); }
int ocFace2D::verts_all(std::vector<double> &x) const { return oc_verts_all(s_, x); }

int
ocShape::face_vert_indices(int fi, std::vector<int> &idx) const
{
	idx.clear();
	if ( s_.IsNull() || fi < 0 ) return -1;
	TopTools_IndexedMapOfShape vm;
	TopExp::MapShapes(s_, TopAbs_VERTEX, vm);
	int k = 0;
	for ( TopExp_Explorer f(s_, TopAbs_FACE) ; f.More() ; f.Next(), ++k ) {
		if ( k != fi ) continue;
		/* ⚠ Explorer は頂点を **稜の数だけ**返すので、出た順を保ったまま重複を落とす
		 *   (地図の番号で見る = vert(v,i) の i と同じ番号)。 */
		for ( TopExp_Explorer v(f.Current(), TopAbs_VERTEX) ; v.More() ; v.Next() ) {
			const int gi = vm.FindIndex(v.Current());
			if ( gi < 1 ) continue;
			int dup = 0;
			for ( size_t t = 0 ; t < idx.size() ; ++t ) if ( idx[t] == gi - 1 ) { dup = 1; break; }
			if ( ! dup ) idx.push_back(gi - 1);
		}
		return (int)idx.size();
	}
	return -1;   /* 範囲外 */
}

int
ocFace2D::is_planar() const
{
	if ( s_.IsNull() ) return 0;
	int nf = 0;
	for ( TopExp_Explorer e(s_, TopAbs_FACE) ; e.More() ; e.Next() ) {
		BRepAdaptor_Surface ad(TopoDS::Face(e.Current()));
		if ( ad.GetType() != GeomAbs_Plane ) return 0;
		++nf;
	}
	return ( nf > 0 ) ? 1 : 0;
}

/* ★ 厚みの前提。offset 写像のヤコビアンが正であること = 標本点のすべてで 1 - d*κ > 0。
 *   返り: 最小値 (>0 なら安全・<=0 なら焦線が立って自己交差する)。
 * ⚠ **標本で見ている**ので厳密ではない (解析曲面では曲率が一定か素直なので十分だが、
 *   B-spline では標本の間で破れうる)。それでも OCCT は *何も検査しない* ので、
 *   「黙って別の立体を返す」よりは大きく良い。⇒ 但し書きはエラー文には出さない。 */
static double
oc_thicken_margin(const TopoDS_Shape &s, double d)
{
	double worst = 1e300;
	for ( TopExp_Explorer e(s, TopAbs_FACE) ; e.More() ; e.Next() ) {
		BRepAdaptor_Surface ad(TopoDS::Face(e.Current()));
		BRepLProp_SLProps pr(ad, 2, 1e-7);
		const double u0 = ad.FirstUParameter(), u1 = ad.LastUParameter();
		const double v0 = ad.FirstVParameter(), v1 = ad.LastVParameter();
		for ( int i = 0 ; i <= 8 ; ++i )
			for ( int j = 0 ; j <= 8 ; ++j ) {
				pr.SetParameters(u0 + (u1 - u0) * i / 8.0, v0 + (v1 - v0) * j / 8.0);
				if ( ! pr.IsCurvatureDefined() ) continue;
				const double a = 1.0 - d * pr.MaxCurvature();
				const double b = 1.0 - d * pr.MinCurvature();
				if ( a < worst ) worst = a;
				if ( b < worst ) worst = b;
			}
	}
	return ( worst > 1e299 ) ? 1.0 : worst;   /* 曲率が取れない (平面ばかり) なら安全 */
}

sPtr<ocFace2D>
ocFace2D::op_offset(double d, char *err, int errsz) const
{
	if ( s_.IsNull() ) {
		if ( err ) ::snprintf(err, errsz, "offset: empty 2D region");
		return sPtr<ocFace2D>();
	}
	if ( ! is_planar() ) {
		/* ⚠ 理由の無い拒否を作らない — 代わりの道を必ず案内する (#3544 段 3 と同じ作法)。 */
		if ( err ) ::snprintf(err, errsz,
		    "offset: a 2D region on a curved surface cannot be offset in-surface "
		    "(the distance would be geodesic); use offset_thicken(face, d) to give it thickness");
		return sPtr<ocFace2D>();
	}
	/* ⚠ 面が 2 枚以上なら断る。面ごとに独立にオフセットすると *重なりを解けない*
	 *   (occt は 2D どうしのブールを持たないので足し直せない)。黙って誤った面積を返さない。 */
	int nf = 0;
	for ( TopExp_Explorer e(s_, TopAbs_FACE) ; e.More() ; e.Next() ) ++nf;
	if ( nf != 1 ) {
		if ( err ) ::snprintf(err, errsz,
		    "offset: this 2D value has %d faces; in-surface offset needs exactly one "
		    "(overlaps between offset faces cannot be resolved without a 2D boolean)", nf);
		return sPtr<ocFace2D>();
	}
	try {
		TopoDS_Face f0;
		for ( TopExp_Explorer e(s_, TopAbs_FACE) ; e.More() ; e.Next() ) { f0 = TopoDS::Face(e.Current()); break; }
		Handle(Geom_Plane) pl = Handle(Geom_Plane)::DownCast(BRep_Tool::Surface(f0));
		if ( pl.IsNull() ) {
			if ( err ) ::snprintf(err, errsz, "offset: could not read the plane of the face");
			return sPtr<ocFace2D>();
		}
		/* ★ GeomAbs_Arc = 外側の角を **円弧で丸める** (Steiner の公式どおりの面積になる:
		 *   矩形 3x2 を外へ d で 6 + 10d + πd²)。⇒ cg / mf の 2D offset と同じ約束。 */
		BRepOffsetAPI_MakeOffset mk(f0, GeomAbs_Arc);
		mk.Perform(d);
		if ( ! mk.IsDone() ) {
			if ( err ) ::snprintf(err, errsz, "offset: OCCT MakeOffset failed (d=%g)", d);
			return sPtr<ocFace2D>();
		}
		TopoDS_Shape w = mk.Shape();
		int nw = 0;
		for ( TopExp_Explorer e(w, TopAbs_WIRE) ; e.More() ; e.Next() ) ++nw;
		if ( nw == 0 ) {
			/* ★ 内側へ削りすぎて消えた。**空の 2D** は表現できるので、そう返すのが正しい
			 *   (エラーにしない — cg / mf も空集合を返す)。 */
			sPtr<ocFace2D> outEmpty = thNEW(ocFace2D,());
			outEmpty->set_shape(TopoDS_Shape());
			return outEmpty;
		}
		TopoDS_Face out;
		for ( TopExp_Explorer e(w, TopAbs_WIRE) ; e.More() ; e.Next() ) {
			TopoDS_Wire wi = TopoDS::Wire(e.Current());
			if ( out.IsNull() ) out = BRepBuilderAPI_MakeFace(pl->Pln(), wi);
			else                out = BRepBuilderAPI_MakeFace(out, wi);   /* 穴として足す */
		}
		sPtr<ocFace2D> r = thNEW(ocFace2D,());
		r->set_shape(out);
		return r;
	} catch ( const Standard_Failure &e ) {
		if ( err ) ::snprintf(err, errsz, "offset: OCCT threw (%s)", e.GetMessageString());
		return sPtr<ocFace2D>();
	}
}

sPtr<ocShape>
ocFace2D::op_thicken(double d, char *err, int errsz) const
{
	if ( s_.IsNull() ) {
		if ( err ) ::snprintf(err, errsz, "offset_thicken: empty 2D region");
		return sPtr<ocShape>();
	}
	if ( d == 0.0 ) {
		if ( err ) ::snprintf(err, errsz, "offset_thicken: thickness must not be 0");
		return sPtr<ocShape>();
	}
	/* ★★ 前提の検査。これを省くと **黙って別の立体**が返る (ヘッダの⚠の実測)。 */
	const double m = oc_thicken_margin(s_, d);
	if ( m <= 0.0 ) {
		if ( err ) ::snprintf(err, errsz,
		    "offset_thicken: thickness %g exceeds the radius of curvature on the concave side "
		    "(1-d*k = %.3g <= 0); the offset surface would fold through itself", d, m);
		return sPtr<ocShape>();
	}
	try {
		BRepOffsetAPI_MakeThickSolid mk;
		mk.MakeThickSolidBySimple(s_, d);
		if ( ! mk.IsDone() ) {
			if ( err ) ::snprintf(err, errsz, "offset_thicken: OCCT MakeThickSolid failed (d=%g)", d);
			return sPtr<ocShape>();
		}
		TopoDS_Shape r = mk.Shape();
		/* ⚠ 実測: 法線側 (d>0) に作ると **体積が負の向き**で返る。そのまま渡すと
		 *   volume() が負を返すので、ここで裏返す (向きは値の性質であって呼び手の仕事ではない)。 */
		GProp_GProps g;
		BRepGProp::VolumeProperties(r, g);
		if ( g.Mass() < 0.0 ) r.Reverse();
		sPtr<ocShape> out = thNEW(ocShape,());
		out->set_shape(r);
		return out;
	} catch ( const Standard_Failure &e ) {
		if ( err ) ::snprintf(err, errsz, "offset_thicken: OCCT threw (%s)", e.GetMessageString());
		return sPtr<ocShape>();
	}
}

double
ocFace2D::area() const
{
	if ( s_.IsNull() ) return 0.0;
	GProp_GProps g;
	BRepGProp::SurfaceProperties(s_, g);
	return g.Mass();
}

void
ocFace2D::encode(ocChunkSink &sink)
{
	/* ★★ #3507 (2026-09-10): BinTools を **ブロック分割**で直接流す (全文バッファを作らない)。
	 *   BinTools_OStream は位置を自前で数えて参照を前向きに書くので、書きは seek しない。
	 *
	 * ★ #3544: 先頭に名乗りの 1 バイトを置く案は **取り下げた** (ひさ指示 2026-09-17)。
	 *   ⇒ blob は **不変**。2D の名乗り (oc-cross2d / oc-face3d) をどう持ち回るかは
	 *     pig 層にも触れずに決める必要があり、その判断は #3544 の journal に残す。 */
	blockframe::obuf<ocChunkSink> ob(sink);
	std::ostream                  os(&ob);
	if ( ! s_.IsNull() )
		BinTools::Write(s_, os);
	os.flush();
	ob.finish();
}

void
ocFace2D::decode(ocChunkSource &src)
{
	/* ★ #3507: 読みは **全文バッファ 1 本が下限** — BinTools_IStream::GoTo() が位置を戻すため。
	 *   ブロック列を 1 本に集め、その上に streambuf を被せる (@istringstream@ は中身を
	 *   もう 1 本複製するので使わない = 2 本 → 1 本)。 */
	std::string buf;
	if ( ! blockframe::read_all(src, buf, (uint64_t)16 * 1024 * 1024 * 1024) ) {
		set_decode_err("the stored 2D face is corrupt or implausibly large (over 16GiB)");
		return;
	}
	if ( buf.empty() ) { set_decode_err("the stored 2D face is empty"); return; }
	blockframe::membuf mb(buf.data(), buf.size());
	std::istream       is(&mb);
	BinTools::Read(s_, is);
	if ( s_.IsNull() ) set_decode_err("OCCT BinTools could not read the stored 2D face (unsupported or corrupt B-rep)");
}

bool
ocFace2D::write_to(const char *path, const char *unit)
{
	return write_to(path, unit, 0, 0);
}

bool
ocFace2D::write_to(const char *path, const char *unit, char *err, int errsz)
{
	(void)unit;
	if ( s_.IsNull() ) {
		if ( err != 0 && errsz > 0 ) ::snprintf(err, (size_t)errsz, "the 2D value is empty");
		return false;
	}
#ifdef SRAVA_OCCT_STEP
	const char *dot = ::strrchr(path, '.');
	if ( dot != 0 && ( oc_ieq(dot, ".step") || oc_ieq(dot, ".stp") ) ) {
		STEPControl_Writer w;
		Interface_Static::SetCVal("write.step.schema", "AP203");
		if ( w.Transfer(s_, STEPControl_AsIs) != IFSelect_RetDone ) return false;
		return w.Write(path) == IFSelect_RetDone;
	}
#endif
	/* ★★ #3544 段 3: **DXF / SVG** — 2D 図面の出口。
	 *   ⚠ 「曲線を保つのが型の価値なので折れ線へ落とす出口は置かない」と書いていたが、
	 *     それは *折れ線しか書けない書き手に乗せる場合* の話だった。⇒ occt が自前で書けば
	 *     円は CIRCLE・楕円は ELLIPSE・B-spline は SPLINE として **曲線のまま**出せる。
	 *     ⇒ 出口を置かない理由が消えたので置く (ocDrawing.cpp)。
	 *   ⚠ SVG だけは形式に B-spline が無いので、そこだけ折れ線になる (ocDrawing.cpp の注記)。 */
	{
		const char *d2 = ::strrchr(path, '.');
		if ( d2 != 0 && oc_ieq(d2, ".dxf") ) return oc_write_dxf(s_, path, unit, err, errsz);
		if ( d2 != 0 && oc_ieq(d2, ".svg") ) return oc_write_svg(s_, path, unit, err, errsz);
	}
	/* .brep — ⚠ 2D も **書けなかった** (ここが false を返していた)。3D と同じ出口を開ける。 */
	return BRepTools::Write(s_, path) == Standard_True;
}

/* ★★ #3544 段 3: DXF を読んで 2D 図面にする。⚠ 語彙は書き手と 1:1 (ocDrawing.cpp)。
 *   読めなければ null + *err (呼び手が明示エラーにする)。 */
sPtr<ocFace2D>
ocFace2D::read_drawing(const char *path, char *err, int errsz)
{
	const char *dot = ( path != 0 ) ? ::strrchr(path, '.') : 0;
	if ( dot == 0 || ! oc_ieq(dot, ".dxf") ) return sPtr<ocFace2D>();
	TopoDS_Shape sh;
	if ( ! oc_read_dxf(path, sh, err, errsz) ) return sPtr<ocFace2D>();
	sPtr<ocFace2D> out = thNEW(ocFace2D,());
	out->set_shape(sh);
	return out;
}
