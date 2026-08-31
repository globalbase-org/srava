/*
 * ocShape — OCCT B-rep 幾何の実装 (#3437 P5)。設計の背景はヘッダ冒頭を参照。
 */
#include	"oc/c++/ocShape.h"
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
#include	<STEPControl_Writer.hxx>
#include	<STEPControl_Reader.hxx>
#include	<Interface_Static.hxx>
#include	<IFSelect_ReturnStatus.hxx>
#include	<BRepGProp.hxx>
#include	<GProp_GProps.hxx>
#include	<BRepTools.hxx>
#include	<BRep_Builder.hxx>
#include	<TopExp_Explorer.hxx>
#include	<TopAbs.hxx>
#include	<Precision.hxx>
#include	<Message.hxx>
#include	<Message_Messenger.hxx>
#include	<Message_PrinterOStream.hxx>
#include	<stdlib.h>   /* getenv */

#include	<stdio.h>
#include	<string.h>
#include	<sstream>
#include	<string>

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
ocShape::nsolids() const
{
	int n = 0;
	for ( TopExp_Explorer e(s_, TopAbs_SOLID) ; e.More() ; e.Next() ) ++n;
	return n;
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

/* 拡張子の大文字小文字を無視して比べる (".STEP" も受ける)。 */
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

sPtr<ocShape>
ocShape::op_union(sPtr<ocShape> b, char *err, int errsz)
{
	if ( b == thNULL || s_.IsNull() || b->shape().IsNull() ) return sPtr<ocShape>();
	TopoDS_Shape r;
	if ( ! oc_guard([&]{ BRepAlgoAPI_Fuse op(s_, b->shape());
		if ( op.IsDone() ) r = op.Shape(); }, err, errsz) ) return sPtr<ocShape>();
	return oc_wrap(r);
}

sPtr<ocShape>
ocShape::op_intersection(sPtr<ocShape> b, char *err, int errsz)
{
	if ( b == thNULL || s_.IsNull() || b->shape().IsNull() ) return sPtr<ocShape>();
	TopoDS_Shape r;
	if ( ! oc_guard([&]{ BRepAlgoAPI_Common op(s_, b->shape());
		if ( op.IsDone() ) r = op.Shape(); }, err, errsz) ) return sPtr<ocShape>();
	return oc_wrap(r);
}

sPtr<ocShape>
ocShape::op_difference(sPtr<ocShape> b, char *err, int errsz)
{
	if ( b == thNULL || s_.IsNull() || b->shape().IsNull() ) return sPtr<ocShape>();
	TopoDS_Shape r;
	if ( ! oc_guard([&]{ BRepAlgoAPI_Cut op(s_, b->shape());
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
ocShape::op_bool_nary(sArray<sPtr<ocShape> >& ops, const char *kind, char *err, int errsz)
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
	if ( ! oc_guard([&]{
		op->SetArguments(aL);
		op->SetTools(tL);
		op->Build();
		if ( op->IsDone() ) out = op->Shape();
	}, err, errsz) )
		return sPtr<ocShape>();
	return oc_wrap(out);
}

sPtr<ocShape>
ocShape::bool_from_args(sArray<sPtr<pigData> > *args, const char *kind, const char **errmsg,
                        char *errbuf, int errbufsz)
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
		if      ( ::strcmp(kind, "union") == 0 )        r = ops[0]->op_union(ops[1], why, (int)sizeof why);
		else if ( ::strcmp(kind, "intersection") == 0 ) r = ops[0]->op_intersection(ops[1], why, (int)sizeof why);
		else                                            r = ops[0]->op_difference(ops[1], why, (int)sizeof why);
	} else
		r = op_bool_nary(ops, kind, why, (int)sizeof why);
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
ocShape::op_offset(double d, char *err, int errsz)
{
	if ( s_.IsNull() ) return sPtr<ocShape>();
	if ( d == 0.0 ) return oc_wrap(s_);
	TopoDS_Shape out;
	if ( ! oc_guard([&]{
		BRepOffsetAPI_MakeOffsetShape mk;
		mk.PerformByJoin(s_, d, Precision::Confusion(),
		                 BRepOffset_Skin, Standard_False, Standard_False, GeomAbs_Arc);
		if ( mk.IsDone() ) out = mk.Shape();
	}, err, errsz) )
		return sPtr<ocShape>();
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
ocShape::op_fillet(double r, char *err, int errsz)
{
	if ( s_.IsNull() ) return sPtr<ocShape>();
	if ( r == 0.0 ) return oc_wrap(s_);
	if ( r < 0.0 ) return sPtr<ocShape>();   /* 負の丸めは定義しない (収縮は offset の仕事) */
	TopoDS_Shape out;
	if ( ! oc_guard([&]{
		BRepFilletAPI_MakeFillet mk(s_);
		int n = 0;
		for ( TopExp_Explorer e(s_, TopAbs_EDGE) ; e.More() ; e.Next() ) {
			mk.Add(r, TopoDS::Edge(e.Current()));
			++n;
		}
		if ( n == 0 ) return;            /* 稜が無い = 丸める対象が無い */
		mk.Build();
		if ( mk.IsDone() ) out = mk.Shape();
	}, err, errsz) )
		return sPtr<ocShape>();
	return oc_wrap(out);
}

sPtr<ocShape>
ocShape::op_chamfer(double d, char *err, int errsz)
{
	if ( s_.IsNull() ) return sPtr<ocShape>();
	if ( d == 0.0 ) return oc_wrap(s_);
	if ( d < 0.0 ) return sPtr<ocShape>();
	TopoDS_Shape out;
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
		mk.Build();
		if ( mk.IsDone() ) out = mk.Shape();
	}, err, errsz) )
		return sPtr<ocShape>();
	return oc_wrap(out);
}

/* ---- wire 形式 (D_META 4CC "BREP") ----
 *   [u64 nbytes][BinTools::Write のバイナリ BREP]
 * 長さ接頭辞の理由は vd と同じ (chunk Source に「残り全部」が無い)。 */
static void put_u64(ocChunkSink &sink, uint64_t v)
{
	uint8_t b[8];
	for ( int i = 0 ; i < 8 ; ++i ) b[i] = (uint8_t)((v >> (8 * i)) & 0xff);
	sink.chunk(b, 8);
}
static uint64_t get_u64(ocChunkSource &src)
{
	uint8_t b[8];
	src.pull(b, 8);
	uint64_t v = 0;
	for ( int i = 7 ; i >= 0 ; --i ) v = (v << 8) | (uint64_t)b[i];
	return v;
}

void
ocShape::encode(ocChunkSink &sink)
{
	std::ostringstream os(std::ios_base::out | std::ios_base::binary);
	if ( ! s_.IsNull() )
		BinTools::Write(s_, os);
	const std::string &str = os.str();
	put_u64(sink, (uint64_t)str.size());
	if ( ! str.empty() )
		sink.chunk((const uint8_t*)str.data(), (int)str.size());
}

void
ocShape::decode(ocChunkSource &src)
{
	uint64_t n = get_u64(src);
	if ( n > (uint64_t)16 * 1024 * 1024 * 1024 ) { decodeErr_ = 1; return; }
	std::string buf;
	buf.resize((size_t)n);
	if ( n > 0 ) src.pull((uint8_t*)&buf[0], (int)n);
	if ( n == 0 ) { decodeErr_ = 1; return; }
	std::istringstream is(buf, std::ios_base::in | std::ios_base::binary);
	BinTools::Read(s_, is);
	if ( s_.IsNull() ) decodeErr_ = 1;
}

bool
ocShape::write_to(const char *path, const char *unit)
{
	(void)unit;
	if ( s_.IsNull() ) return false;
	const char *dot = ::strrchr(path, '.');
	if ( dot != 0 && ( oc_ieq(dot, ".step") || oc_ieq(dot, ".stp") ) ) {
		/* ★ STEP (ISO 10303) — **解析曲面のまま**書ける唯一の出口。
		 *   triangulate の出口 (mf-mesh3d) は三角形へ落とすが、こちらは B-rep のまま
		 *   他の CAD へ渡せる。「表現力を落とさずに外へ出せる」ことがこの型の価値なので、
		 *   ここは占有的に重要。AP203 (機械部品の形状) を既定にする。 */
		STEPControl_Writer w;
		Interface_Static::SetCVal("write.step.schema", "AP203");
		if ( w.Transfer(s_, STEPControl_AsIs) != IFSelect_RetDone ) return false;
		return w.Write(path) == IFSelect_RetDone;
	}
	/* .brep (OCCT 固有のバイナリ/ASCII 形式)。 */
	return BRepTools::Write(s_, path) == Standard_True;
}

/* ---- ★ 入口: STEP / BREP を読む (#3437) ----
 * ★★ **これは「mesh → B-rep」ではない**。STEP も BREP も **解析曲面をそのまま持っている**
 *   形式なので、読むだけで B-rep が手に入る (復元も推定もしない)。
 *   三角形群から解析曲面を復元する reverse engineering とは別物であり、そちらは
 *   「入口を作らない」という設計判断のまま変えない (ocShape.h 冒頭)。
 * 失敗は null。**部分的に読めた形を黙って返さない**。 */
sPtr<ocShape>
ocShape::read_file(const char *path)
{
	if ( path == 0 ) return sPtr<ocShape>();
	const char *dot = ::strrchr(path, '.');
	if ( dot != 0 && ( oc_ieq(dot, ".step") || oc_ieq(dot, ".stp") ) ) {
		STEPControl_Reader r;
		if ( r.ReadFile(path) != IFSelect_RetDone ) return sPtr<ocShape>();
		r.TransferRoots();
		if ( r.NbShapes() < 1 ) return sPtr<ocShape>();
		return oc_wrap(r.OneShape());
	}
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
	/* ★ MFM3 (メッシュ) の分岐は **撤去した** (akira-project #3452)。occt が作っていた
	 *   内部クラス ocMesh は「型名だけ mf-mesh3d を借りた別クラス」で、本家 manifold の
	 *   読み手と競合していた。B-rep → メッシュの出口は occt_mf.so が持ち、そこで作った
	 *   **本物の mfMesh** を読むのは manifold 本家の reader である。 */
	return sPtr<ocGeom>();
}
