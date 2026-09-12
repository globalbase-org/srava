/*
 * ocShape — OCCT B-rep 幾何の実装 (#3437 P5)。設計の背景はヘッダ冒頭を参照。
 */
#include	"oc/c++/ocShape.h"
#include	"common/blockframe.h"   /* ★ #3507: ブロック分割フレーミング */
#include	"ts2/c++/stdString.h"
#include	<Standard_Failure.hxx>
#include	<Standard_Type.hxx>
#include	<stdexcept>

#include	<BinTools.hxx>
#include	<BRepAlgoAPI_Fuse.hxx>
#include	<BRepAlgoAPI_Common.hxx>
#include	<BRepAlgoAPI_Cut.hxx>
#include	<BRepAlgoAPI_BooleanOperation.hxx>
#include	<TopTools_ListOfShape.hxx>
#include	<BRepOffsetAPI_MakeOffsetShape.hxx>
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
#include	<BRepGProp.hxx>
#include	<BRepBndLib.hxx>          /* bbox (#3487) */
#include	<Bnd_Box.hxx>
#include	<BRepAlgoAPI_Check.hxx>   /* valid = 妥当性 + 自己交差 (#3487) */
#include	<gp_Pnt.hxx>
#include	<GProp_GProps.hxx>
#include	<BRepTools.hxx>
#include	<BRep_Builder.hxx>
#include	<TopoDS_Compound.hxx>
#include	<TopExp_Explorer.hxx>
#include	<TopExp.hxx>
#include	<TopTools_IndexedMapOfShape.hxx>
#include	<gp_Trsf.hxx>
#include	<gp_GTrsf.hxx>
#include	<gp_XYZ.hxx>
#include	<BRepBuilderAPI_Transform.hxx>
#include	<BRepBuilderAPI_GTransform.hxx>
#include	<cmath>
#include	<TopAbs.hxx>
#include	<Precision.hxx>
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
	 *   既定 (三角形) だと分割の粗さで箱が膨らむ。 */
	BRepBndLib::Add(s_, b, Standard_False);
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

int
ocShape::op_valid(const pigBreak *brk) const
{
	if ( s_.IsNull() ) return 0;                 /* ① 空でない */
	if ( nsolids() == 0 ) return 0;
	/* ②③ = BRepAlgoAPI_Check。既定で BRepCheck_Analyzer (トポロジ/幾何の整合) と
	 * BOPAlgo_CheckerSI (自己交差) の両方を走らせる。★ OCCT が投げることがあるので
	 * ここで受ける (ワーカースレッド由来だと agent ごと死ぬ)。 */
	try {
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
 * ★ 呼び手は STEP の分岐しか無いので、SRAVA_OCCT_STEP=OFF のビルドでは丸ごと未使用になる
 *   (clang が -Wunused-function を出す)。⇒ 呼び手と同じガードで囲う。 */
#ifdef SRAVA_OCCT_STEP
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
#endif	/* SRAVA_OCCT_STEP */

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
	/* ★ useTriangulation=false は op_bbox と同じ理由 (曲面そのものから求める)。 */
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

sPtr<ocShape>
ocShape::op_affine(const double e[12], char *err, int errsz)
{
	if ( s_.IsNull() ) return sPtr<ocShape>();
	TopoDS_Shape out;
	double s = 1.0;
	if ( oc_is_similarity(e, &s) ) {
		if ( ! oc_guard([&]{
			gp_Trsf t;
			t.SetValues(e[0], e[1], e[2],  e[3],
			            e[4], e[5], e[6],  e[7],
			            e[8], e[9], e[10], e[11]);
			BRepBuilderAPI_Transform mk(s_, t, Standard_True);
			if ( mk.IsDone() ) out = mk.Shape();
		}, err, errsz) )
			return sPtr<ocShape>();
	} else {
		if ( ! oc_guard([&]{
			gp_GTrsf t;
			for ( int i = 0 ; i < 3 ; ++i )
				for ( int j = 0 ; j < 3 ; ++j )
					t.SetValue(i+1, j+1, e[i*4+j]);
			t.SetTranslationPart(gp_XYZ(e[3], e[7], e[11]));
			BRepBuilderAPI_GTransform mk(s_, t, Standard_True);
			if ( mk.IsDone() ) out = mk.Shape();
		}, err, errsz) )
			return sPtr<ocShape>();
	}
	if ( out.IsNull() ) return sPtr<ocShape>();
	return oc_wrap(out);
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
	(void)unit;
	(void)wbrk;   /* ★ SRAVA_OCCT_STEP=OFF のビルドでは使い道が無い (.brep には進捗の口が無い) */
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
	/* .brep (OCCT 固有のバイナリ/ASCII 形式)。⚠ BinTools / BRepTools には進捗の口が無い。 */
	return BRepTools::Write(s_, path) == Standard_True;
}

/* ---- ★ 入口: STEP / BREP を読む (#3437) ----
 * ★★ **これは「mesh → B-rep」ではない**。STEP も BREP も **解析曲面をそのまま持っている**
 *   形式なので、読むだけで B-rep が手に入る (復元も推定もしない)。
 *   三角形群から解析曲面を復元する reverse engineering とは別物であり、そちらは
 *   「入口を作らない」という設計判断のまま変えない (ocShape.h 冒頭)。
 * 失敗は null。**部分的に読めた形を黙って返さない**。 */
sPtr<ocShape>
ocShape::read_file(const char *path, const pigBreak *brk)
{
	(void)brk;   /* ★ SRAVA_OCCT_STEP=OFF のビルドでは使い道が無い (BRepTools::Read に口が無い) */
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
	 *   BinTools_OStream は位置を自前で数えて参照を前向きに書くので、書きは seek しない。 */
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
	(void)unit;
	if ( s_.IsNull() ) return false;
#ifdef SRAVA_OCCT_STEP
	const char *dot = ::strrchr(path, '.');
	if ( dot != 0 && ( oc_ieq(dot, ".step") || oc_ieq(dot, ".stp") ) ) {
		STEPControl_Writer w;
		Interface_Static::SetCVal("write.step.schema", "AP203");
		if ( w.Transfer(s_, STEPControl_AsIs) != IFSelect_RetDone ) return false;
		return w.Write(path) == IFSelect_RetDone;
	}
#endif
	/* ★ 2D 領域は **B-rep のまま**しか出さない。曲線を保つのが型の価値なので、
	 *   三角形や折れ線へ落とす出口はここに置かない (要るなら cast で落とす)。 */
	return false;
}
