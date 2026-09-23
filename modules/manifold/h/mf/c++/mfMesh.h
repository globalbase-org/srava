#ifndef MF_MESH_H
#define MF_MESH_H
/*
 * mfGeom / mfMesh / mfCross — Manifold(github.com/elalish/manifold・Apache-2.0)幾何を pigData で
 * ラップした多態な値ハンドル。cgMesh(CGAL 版)のミラーだが CGAL に非依存で、mf agent(pig + Manifold
 * のみリンク・GPL 非汚染)側でのみ compile する。
 *
 *  - 状態機械ではない plain クラス(cgMesh と同じ考え方)。sPtr/stdObject を使う。
 *  - mfGeom  … 抽象基底。meta_tag/repr_type/dim・codec(encode/decode)・write_to を virtual で持つ。
 *              reader は create_for_meta() でタグから具体型を作り、writer は基底ポインタで書ける
 *              (cgMesh 階層と同じ設計)。
 *  - mfMesh  … 3D Manifold(内容アドレスに乗る値・コピーは shared_ptr impl で安価)。
 *  - mfCross … 2D manifold::CrossSection(polygon/rect/circle/ngon → extrude/revolve の断面)。
 *  - CGAL corefinement が「厳密」なのに対し Manifold は「速い/非厳密(float 相当レンジ)」カーネル。
 *    妥当性は Status()==NoError で判断でき、破綻はサイレントでなく検出できる(op_valid)。
 */
#include	"pig/c++/pigData.h"
#include	"pig/c++/pigModuleError.h"   /* #3475: 自分の名前でエラーを作る */
#include	"pig/c++/pigOpEntry.h"   /* pigWireClass (配線先) */
#include	"pig/c++/pigBreak.h"     /* #3498: 中断 (mfGeom::force_eval の但し書きを読むこと) */
#include	"manifold/manifold.h"
#include	"manifold/cross_section.h"
#include	<stdint.h>
#include	<stdio.h>    /* mf_eval_err の snprintf */
#include	<string.h>   /* 同 strcmp */

/* codec の Sink/Source 抽象(cgChunkSink/Source と同シグネチャ・CGAL 非依存。将来 cg と共有可)。
 * writer/reader が adapter で実装し、encode/decode が chunk()/pull() 越しに D_CHUNK を直接読み書く。 */
struct mfChunkSink   { virtual void chunk(const uint8_t *data, int n) = 0; virtual ~mfChunkSink()   {} };
struct mfChunkSource { virtual void pull (uint8_t *dst, int n)        = 0;
                       virtual int  more()                            { return 1; }
                       virtual ~mfChunkSource() {} };

/* ---- 抽象基底: reader/writer が次元非依存に扱う多態ハンドル ---- */
class mfGeom : public pigDataWireTyped {   /* rev4 Phase A: 型軸 marker 基底 (旧 pigData 直継承) */
public:
	mfGeom(sPtr<pigInfo> i = thNULL) : pigDataWireTyped(i) {}
	virtual const char* meta_tag()  = 0;   /* D_META 4 バイトタグ "MFM3"(3D)/"MFC2"(2D) */
	virtual uint16_t    repr_type() = 0;   /* 64=manifold 3D / 96=manifold 2D(catalog §5) */
	virtual int         dim()       = 0;   /* 2 or 3 */
	virtual void encode(mfChunkSink&)   = 0;
	virtual void decode(mfChunkSource&) = 0;
	/* ★ #3433: decode が「この形式は mf の表現力では受け取れない」と判断したときに立てる。
	 *   reader がこれを見て errCode を立てる (空 mesh を黙って返さない)。 */
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
	/* ★★ #3498: **遅延 CSG 木をここで評価する**。成功なら 1・中断/破綻なら 0 (+ *why)。
	 *
	 * ★ なぜ要るのか — manifold は他の 2 つと **構造が違う**:
	 *   ① 中断の口が @c ExecutionContext::Cancel() という *こちらから撃つ* 関数しかない
	 *      (occt / openvdb は「向こうがこちらの旗を見に来る」)。押し出しは pigBreakHook が担う。
	 *   ② ★**中断を観測できるのは @c Status() だけ**。@c Volume / @c GetMeshGL / @c BoundingBox は
	 *      評価を強制するが **ctx を一切見ない** (manifold.h の WithContext のコメントに明記)。
	 *   ③ そして srava では、遅延 CSG 木を実際に評価していたのは **@c encode()**
	 *      (@c GetMeshGL64) だった — つまり compute() が返った *後*、キャッシュ書き出しの途中。
	 *      そこは calc が既に畳まれた後なので、中断のしようがない。
	 *
	 *   ⇒ **評価点を compute() の中へ引き出す**のがこの関数。#3417 の元 TODO はこれを指していた。
	 *
	 * ⚠⚠ これは **測定に影響する変更**。総仕事量は変わらない (評価は 1 回で、木のノードが
	 *   結果を cache_ に持つので encode 側は再利用するだけ) が、*どの段で計上されるか* が
	 *   compute へ移る。cold/warm の内訳を測っているベンチはここで数字の内訳が動く。
	 *   ⇒ bench へ持って行く前に前後を測ること (#3498 の「測定への影響」)。
	 *
	 * ⚠ 中断の粒度は **sub-boolean ごと** (common.h)。= *1 個の巨大なブールは止まらない*。
	 *   n 項の BatchBoolean や木は途中で止まる。
	 * ⚠ 中断は Manifold にとって **永続的** — 一度 Cancelled になった木は作り直すまで
	 *   Cancelled のまま。だから中断したら結果を捨ててエラーを返すのが唯一正しい扱いで、
	 *   「もう一度評価してみる」は無意味 (ctx の cancel も sticky)。 */
	virtual int force_eval(const pigBreak *brk, const char **why) = 0;

	virtual bool write_to(const char *path, const char *unit) = 0;
	/* アフィン変換(行優先 double[12]・3D 規約)。2D(mfCross)は XY 2x2 + XY 平行移動だけ使う。 */
	virtual sPtr<mfGeom> apply_affine(const double e[12]) = 0;
	/* reader 用ファクトリ: D_META タグから具体型を生成(未知タグは null)。 */
	static sPtr<mfGeom> create_for_meta(const uint8_t *meta, int len);

	/* ★ 2026-08-28 (ABI v12): **この階層への配線先**。op の OPS 行が OPWIRE(Calc, mfGeom) と
	 *   書くと、引数はこの WIRE 経由で実体化される。create_for_meta が 4CC を受理判定し、
	 *   mkReader がこの階層の stream reader を起こす。定義は mfCacheCodec.cpp。 */
	static const pigWireClass WIRE;
};

/* ---- 3D Manifold ---- */
class mfMesh : public mfGeom {
public:
	mfMesh(const manifold::Manifold &m, sPtr<pigInfo> i = thNULL) : mfGeom(i), m_(m) {}

	manifold::Manifold&       manifold()       { return m_; }
	const manifold::Manifold& manifold() const { return m_; }

	virtual sPtr<stdString> get_str();   /* 表示用 */

	virtual const char* meta_tag()  { return "MFM3"; }   /* Manifold 3D mesh */
	virtual const char* type_name() { return "mf-mesh3d"; }   /* rev4 実装型名 (MFM3 と 1:1) */
	virtual uint16_t    repr_type() { return 64; }        /* catalog §5: 64=manifold 3D */
	virtual int         dim()       { return 3; }

	/* codec(D_CHUNK)framing(little-endian): [u32 nv][u32 nt] 頂点×nv(double x,y,z)三角形×nt(u32 i,j,k)
	 * + **色 section** [u32 hasColor] (1 なら頂点×nv の u32 packed 0xRRGGBB)。
	 * 色 section は後付けなので、旧 cache は section 自体が無く decode は src.more() で判定する
	 * (cgaMeshCodec の面色 section と同じ後方互換の作法)。 */
	virtual void encode(mfChunkSink&   sink);
	virtual void decode(mfChunkSource& src);
	/* ★ exact→float(#3404 Phase D): CGAL の MESH(厳密有理数文字列)キャッシュを読んで double 化する
	 *   経路を有効にする。cast("manifold", exactMesh) で create_for_meta が MESH を検出→これを立て、
	 *   decode() が cgaMeshCodec 形式(有理数 "p/q" 文字列)をパースして Manifold を作る(CGAL 非依存)。 */
	void set_mesh_exact_input() { meshExactInput_ = 1; }
	/* ★ #3433: nef の "NEF3" を読む経路。NEF3 の payload は先頭 1 バイトが形式で、
	 *   1 = 厳密境界 (cg の "MESH" と同一フレーミング) なら **CGAL 無しで読める** (decode_mesh_exact)。
	 *   0 = SNC は CGAL が要るので manifold では読めない (CGAL 非依存 = GPL 非汚染を守る) →
	 *   decodeErr_ を立てて reader にエラーを出させる (空 mesh を黙って返さない)。 */
	void set_nef3_input() { nef3Input_ = 1; }

	/* ---- 着色 (#3415 続き): 全頂点プロパティ ch3..5 に RGB(0-255) を入れた新 mesh ----
	 * cgal の per-face "f:color" property map に対応する mf 側の持ち方は **頂点プロパティ**
	 * (Manifold::SetProperties)。numProp = 6 (x,y,z,r,g,b) になり、Manifold のブール/変換で
	 * そのまま運ばれる。色つき export (3MF/AMF) は「三角形の第 1 隅の色」を面色として出す
	 * (color() は全頂点を同色にするので、成分ごとに一様 = cgal の面色と同じ見え方になる)。 */
	sPtr<mfMesh>	op_color(int r, int g, int b);
	int		has_color() const { return (int)m_.NumProp() >= 3; }   /* 位置を除く追加 prop 3 本 = RGB */

	/* ---- ブーリアン(同型の新 mesh。b が null は null)---- */
	sPtr<mfMesh> op_union       (sPtr<mfMesh> b);
	sPtr<mfMesh> op_intersection(sPtr<mfMesh> b);
	sPtr<mfMesh> op_difference  (sPtr<mfMesh> b);

	/* ---- ★ Minkowski 和 A ⊕ B (#3511) ------------------------------------------
	 * ★★ **Manifold は凸分解を使わない**。A の三角形ごとに (3 頂点 ⊕ B) の凸包を取り、
	 *   1000 個ずつ @c BatchBoolean(Add) で畳む (上流 src/minkowski.cpp)。境界の三角形分割を
	 *   分解の代わりに使うので、**union の速さがそのまま効く**。
	 *   ⇒ 「ミンコフスキーは nef の専売」は *srava の配線の都合* であって、ライブラリの
	 *     都合ではなかった (#3510 の機能比較表で判明)。
	 * ⚠ nef (CGAL::minkowski_sum_3) とは **結果が bit 一致しない**。あちらは凸分解 + 厳密有理数、
	 *   こちらは三角形ごとの凸包 + double。凸どうしなら幾何は一致するが、頂点の並びは違う。
	 * ⚠ **空の被演算は空**を返す (A ⊕ ∅ = ∅)。Manifold 側も同じだが、ここで畳んでおくと
	 *   三角形ごとの凸包を 1 つも作らずに済む。
	 * ⚠ 2D は無い (CrossSection に Minkowski が無い)。 */
	sPtr<mfMesh> op_minkowski(sPtr<mfMesh> b);

	/* ---- アフィン変換(3D 型の行優先 double[12]。cgMesh3D::apply_affine と同じ規約)→ 新 mesh ---- */
	virtual sPtr<mfGeom> apply_affine(const double e[12]);

	/* ---- 計測 / 妥当性 ---- */
	double op_volume();   /* 囲む体積 */
	double op_area();     /* 表面積 */
	/* ★ #3443: 頂点数 / 面数 (planner の表示を op へ移した)。 */
	int    op_nverts();
	int    op_nfaces();
	int    op_valid();    /* 1 = Status()==NoError かつ非空 / 0 = 破綻・空(サイレント破綻の検出点)*/
	int    op_bbox(double mn[3], double mx[3]);      /* 軸平行 AABB。返り=3 */
	int    op_centroid(double out[3]);               /* 体積重心(発散定理・GetMeshGL64)。返り=3 */
	/* ★ #3514: 位相を **直接**数える。返り 1 = 閉じている (0 なら genus は意味を持たない)。
	 *   中身は common/meshprops.h の topology() — 定義 (シェル / 塊 / 種数) はそこに一本で書いてある。
	 *   ⚠ 上流の @Manifold::Genus()@ は使っていない。単一成分でしか意味を持たない値 (多成分では
	 *     1 - chi/2 = sum(g) - (C-1) になる) で、@Decompose()@ は成分ごとに Manifold を **丸ごと複製**
	 *     するため。3 つの数は 1 パスで同時に出る。 */
	int    op_topology(int *nshells, int *nparts, int *genus);

	virtual int force_eval(const pigBreak *brk, const char **why);   /* ★ #3498 (基底の但し書き参照) */

	virtual bool write_to(const char *path, const char *unit);   /* STL(binary)/OFF(ascii) */

	/* ---- primitive(mf agent の生成 op が使う。cga* の意味論に合わせる)---- */
	static sPtr<mfMesh> box(double x, double y, double z);   /* 原点隅の x*y*z 直方体(cgaBox 一致) */
	static sPtr<mfMesh> geodesic(int seed, int n, double r); /* sphere/icosphere 共通の測地球(cgal と一致・geodesic.h) */
	static sPtr<mfMesh> sphere(double r, int seg);           /* 半径 r・分割 seg の球 */
	static sPtr<mfMesh> prism(int n, double h, double r);    /* 正 n 角柱(高さ Z・底面 XY・cgaPrism 一致) */
	/* 外部メッシュ読み込み(STL binary/ascii・OFF)→ Manifold。失敗は null(CGAL 非依存の自前パーサ)。 */
	static sPtr<mfMesh> import_file(const char *path);

protected:
	void	decode_mesh_exact(mfChunkSource& src);   /* CGAL MESH(有理数文字列)→ double Manifold */
	void	decode_nef3(mfChunkSource& src);         /* NEF3: 境界形式なら読む / SNC は失敗 */
	manifold::Manifold	m_;
	int	meshExactInput_ = 0;   /* 1 = decode() が CGAL MESH 形式を読む(create_for_meta が MESH タグで立てる) */
	int	nef3Input_ = 0;        /* 1 = decode() が NEF3 を読む(create_for_meta が NEF3 タグで立てる) */
};

/* ---- 2D manifold::CrossSection(extrude/revolve の断面) ---- */
class mfCross : public mfGeom {
public:
	/* ★★ #3529: **真実は輪郭列 (p_)**。CrossSection は派生で、要求時に 1 回だけ作る。
	 *
	 * ---- なぜ (2026-09-13 実測) ----
	 * 旧 decode は @c_ = CrossSection(ps, NonZero)@ で作り直していた。CrossSection の構成子は
	 * **Clipper2 の Union を通る**ので座標が整数格子に載り、リングの巡回開始位置も Clipper2 の
	 * 出力順で決まる。⇒ *同じ式が「その場の実体」と「cache から decode した実体」で違う値*:
	 *     area(circle(1,32))                    3.1214451522580524   Union を通らない
	 *     area(translate(circle(1,32),[0,0,4])) 3.121445153570154    decode で Union ← 症状
	 * ⚠ これは性能でなく **正しさ**の問題 — content-addressed cache は「同じ入力なら同じ結果」を
	 *   前提に値を再利用する仕組みなので、hit と miss で答えが変わるのは約束そのものに触る。
	 *
	 * ---- なぜ「生成時に 1 回正規化する」案を採らなかったか (実測で決まった) ----
	 *     mf area(ngon(32,1))                   3.121445153570154   ★ 生成時に既に Union 済み
	 *     mf area(translate(ngon(32,1),…))      3.121445153570154   ★ もう一度通しても動かない
	 *     cg circle / ngon / translate           3.121445152258052   全部一致 (有理数)
	 *   ① 量子化は **冪等**だった ⇒ その案も成立はする。
	 *   ② しかし **mf の「その場の値」は cgal と一致している** ⇒ 両側を格子に載せて揃えると
	 *      *cgal から 1e-9 離れる*。⇒ 真実を輪郭列に持つ方 (これ) を採った。
	 *
	 * ---- 線引き: どこで Union を通すか ----
	 *   通す    import ・ polygon/ngon の輪郭 (利用者の任意入力) ・ 他カーネルからの cast
	 *           (★ 厳密→double の切り下げは丸めで **無かった自己交差を作る**) ・ warp
	 *   通さない **自分が encode した blob の decode** (本件) ・ Clipper2 op の出力
	 *           (既に正準形) ・ アフィン変換 (無損失) ・ project_flatten (#3534: 平面上で単射)
	 */
	/* 入口 A) op の出力から。★ 作り直さない (CrossSection をそのまま派生として抱える) */
	mfCross(const manifold::CrossSection &c, sPtr<pigInfo> i = thNULL)
		: mfGeom(i), p_(c.ToPolygons()), c_(c), built_(true) {}
	/* 入口 B) 輪郭列から。★ Union を通らない。decode / 素の生成はこちら。
	 * ⚠ 完全一致の方が優先されるので CrossSection 側の暗黙変換とは曖昧にならない。 */
	mfCross(const manifold::Polygons &p, sPtr<pigInfo> i = thNULL)
		: mfGeom(i), p_(p), built_(false) {}

	/* ★ 真実。量子化から解放されているのはこちら。 */
	const manifold::Polygons&     polys() const { return p_; }
	/* 派生。実際に Clipper2 が要る所 (boolean / offset / hull) だけが呼ぶ。
	 * ⚠ 非 const 版は **撤去した** — 書き換えられると p_ と食い違うため (#3529)。 */
	const manifold::CrossSection& cross() const;

	/* ★★ #3526: 2D が居る **平面 (枠)**。局所座標 (x,y) → O + xU + yV。
	 *   既定 O=(0,0,0) U=(1,0,0) V=(0,1,0) = z=0 平面 ⇒ *既存の式は一切変わらない*。
	 *
	 *   ★★ **多角形の座標は局所のまま**にする。ブール・面積・offset・repair は全部
	 *     局所座標で回っているので、そこに枠を持ち込むと 30 本以上を見直すことになる。
	 *     ⇒ 枠を足す意味は「**z 成分の行き先を作る**」ことだけ。
	 *
	 *   ⚠ 枠は **正規直交に保つ** (U ⊥ V ・どちらも単位)。歪みは局所座標が持つ。
	 *     こうしないと area だけでなく perimeter / offset (straight skeleton) が
	 *     局所座標で計算できなくなる (非等方な写像は長さを変える)。
	 *
	 *   ⇒ apply_affine の規約 (mfCross.cpp に実装):
	 *     ① 変換が **平面を動かさない** なら 枠はそのまま・局所座標に 2x2 を当てる
	 *        (= 今日の振る舞いそのもの。in-plane の回転は *図形* が回る)
	 *     ② 変換が **平面を動かす** なら 枠を動かし、残差の 2x2 を局所座標へ
	 *        (= 断面が空間に置かれる。#3511 の loft の前提) */
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
	 *   ⚠ 同一平面でも軸の取り方が違えば局所座標が食い違う。いまは *枠が一致すること* を求める
	 *     (同一平面での再表現は未実装 — 黙って間違えるよりよい)。 */
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
	 *   相手に合わせる (枠が違うだけで断らないために要る)。同じ平面でなければ null。
	 *   ⚠ 枠が既定どうしなど **完全に一致しているときは呼ばない** (呼び手が先に same_frame を
	 *     見る)。呼ぶと 2x2 が恒等でも CrossSection を作り直すので、値の下位桁が動きうる。 */
	sPtr<mfCross> reexpress(const double o[3], const double u[3], const double v[3]) const;
	/* ★★ #3534: **project_flatten** — world 座標の (x,y) をそのまま取り、z を捨てる。
	 *   ⇒ 結果は world 幾何だけで決まる = **経路非依存** (枠の取り方に依らない)。
	 *   ★ 局所 (x,y) → world → z を捨てる、は平面上の **2x2 アフィン**に畳める:
	 *       x' = Ox + x*Ux + y*Vx        y' = Oy + x*Uy + y*Vy
	 *     ⇒ 新しい枠は既定 (z=0)・型は cross2d へ落ちる (placed_=0)。
	 *   ⚠ det = Ux*Vy - Uy*Vx = (U x V)・ẑ = 法線の z 成分。**0 = 平面が world +Z を含む**
	 *     ⇒ 潰れるので null (呼び手が明示エラー)。★ extrude の既存検査と **同じ判定式**。
	 *   ★ 平面が XY と平行なら等長 (面積・周長は不変)・傾けば cosθ で縮む (影として正しい)。
	 *   ★ 平面上で階数 2 のアフィンは単射なので、単純多角形は単純のまま = **正規化不要**。
	 *   ⚠ cgMesh2D::project_flatten と対。**片方だけ直さないこと**。 */
	sPtr<mfCross> project_flatten() const;

	/* 局所座標 (x,y) → 世界座標。 */
	void to_world(double x, double y, double w[3]) const {
		for ( int i = 0 ; i < 3 ; ++i ) w[i] = fo_[i] + x*fu_[i] + y*fv_[i];
	}

	virtual sPtr<stdString> get_str();

	virtual const char* meta_tag()  { return "MFC2"; }   /* Manifold 2D cross-section */
	/* ★★ #3533: **2D 領域の型は 2 つ**。どちらも実体は mfCross・4CC も MFC2 のまま
	 *   (cgal の cgMesh2D と **同じ設計**にしてある — 片方だけ直すのを防ぐため)。
	 *     mf-cross2d  z=0 の **簡易表現**   … 生成したまま / cast で降ろしたもの
	 *     mf-face3d   空間に置かれた一般表現 … transform / section を通ったもの
	 *   ⚠⚠ 出し分けの根拠は @frame_is_default()@ では **なく** @placed_@ (規約①: transform は
	 *     結果が z=0 に留まっても face3d)。@frame_is_default()@ は *幾何* の述語で、
	 *     規約② の cast (降格) の可否だけがそちらを見る。 */
	virtual const char* type_name() { return placed_ ? "mf-face3d" : "mf-cross2d"; }   /* rev4 実装型名 (MFC2 は 2 型で共有) */
	int  is_placed() const { return placed_; }
	void set_placed(int p) { placed_ = p ? 1 : 0; }
	virtual uint16_t    repr_type() { return 96; }        /* catalog §5: 96=manifold 2D */
	virtual int         dim()       { return 2; }

	/* codec(D_CHUNK)framing(little-endian): [u32 nrings] リング×(([u32 npts] 点×(double x,y)))。
	 * ToPolygons() を直列化・decode は CrossSection(Polygons) で再構成。 */
	virtual void encode(mfChunkSink&   sink);
	virtual void decode(mfChunkSource& src);
	/* ★ exact→float 2D (cast の cg→mf downgrade): CGAL の PLY2(cgMesh2D 形式・厳密有理数リング)を
	 *   読んで double 化する経路を有効にする。cast("mf-cross2d", cgCross) で create_for_meta が PLY2 を
	 *   検出→これを立て、decode() が有理数 "p/q" リング列をパースして CrossSection を作る(CGAL 非依存)。 */
	void set_cross_exact_input() { crossExactInput_ = 1; }

	/* ---- 2D ブーリアン(CrossSection +/^/-)---- */
	sPtr<mfCross> op_union       (sPtr<mfCross> b);
	sPtr<mfCross> op_intersection(sPtr<mfCross> b);
	sPtr<mfCross> op_difference  (sPtr<mfCross> b);

	/* ---- 計測 ---- */
	double op_area();                            /* 囲み面積 */
	int    op_nverts();
	int    op_nfaces();   /* 2D は面を持たない = 0 */
	int    op_bbox(double mn[3], double mx[3]);  /* AABB。cross2d=局所 2 成分 / face3d=world 3 成分 */
	/* ★ #3533: 面積重心。**2D の centroid は manifold に無かった** (歯抜け) — cgal / occt は
	 *   持っていた。cgMesh2D::op_centroid と同じ靴紐モーメント (穴は負寄与) で、
	 *   cross2d=局所 2 成分 / face3d=world 3 成分。⚠ 片方だけ直さないこと。 */
	int    op_centroid(double out[3]);

	/* ---- アフィン変換(2D: e[12] の XY 2x2 + XY 平行移動を使う)→ 新 mfCross ---- */
	virtual sPtr<mfGeom> apply_affine(const double e[12]);

	/* ★ #3498: 2D。CrossSection には ExecutionContext を取る口が **無い**ので、
	 *   ここでできるのは「入る前に旗が立っていたら走らない」だけ。中断の粒度は op 単位になる。
	 *   ⚠ 「2D も中断できる」と読まないこと。 */
	virtual int force_eval(const pigBreak *brk, const char **why);

	virtual bool write_to(const char *path, const char *unit);   /* 2D 出力は当面未対応(false) */

protected:
	/* ★ #3533: 型が face3d か (上の type_name)。⚠ 枠とは独立したビット。
	 *   codec は「枠の節を書いたか」で持つ (節があれば face3d)。 */
	int placed_ = 0;
	/* ★ #3526: 枠。既定 = z=0 平面 (コンストラクタで入れる)。 */
	double fo_[3] = {0,0,0};
	double fu_[3] = {1,0,0};
	double fv_[3] = {0,1,0};
public:

	/* ---- primitive(cga* の 2D 意味論に合わせる)---- */
	static sPtr<mfCross> polygon(const double *xy, int npts);   /* 点列(x,y の flat 配列・npts 個)から */
	static sPtr<mfCross> rect(double w, double h);              /* 原点隅の w*h(cgaRect 一致) */
	static sPtr<mfCross> circle(double r, int segs);           /* 半径 r・segs 角近似 */
	static sPtr<mfCross> ngon(int n, double r);                /* 正 n 角形(外接円半径 r) */

protected:
	void	decode_cross_exact(mfChunkSource& src);   /* CGAL PLY2(有理数リング)→ double CrossSection */
	/* ★★ #3529: p_ が真実・c_ は派生 (遅延構築)。順序を入れ替えないこと
	 *   (入口 A の初期化子リストが p_ → c_ の順に依存している)。 */
	manifold::Polygons	p_;
	mutable manifold::CrossSection	c_;
	mutable bool		built_ = false;
	int	crossExactInput_ = 0;   /* 1 = decode() が CGAL PLY2 形式を読む(create_for_meta が PLY2 タグで立てる) */
};

/* ★ #3436 P4: n 項ブール (mfaUnion / mfaIntersection / mfaDifference 共通の入口)。
 *   manifold は Manifold::BatchBoolean / CrossSection::BatchBoolean を持ち、**上限なし**で
 *   n 個を 1 つの CSG ノードとして畳む (Compose は deprecated なのでこちらを使う)。
 *   difference は CsgOpNode が「先頭は正・残りは負」= a-(b|c|…) として扱うので左 fold と一致する。
 *   2 項は従来の二項 API のまま (既存キャッシュを byte 不変に保つ)。
 *   失敗時は null を返し *errmsg に理由を置く。3D/2D はここで振り分ける。 */
sPtr<mfGeom> mf_bool_from_args(sArray<sPtr<pigData> > *args, const char *kind, const char **errmsg);

/* ★ #3511: 凸包 hull(a[,b,…]) — **1 個以上**を受け、与えた形すべての和集合の凸包を返す。
 *   Manifold は @c Manifold::Hull / @c CrossSection::Hull を **既に持っている** (3D は QuickHull)。
 *   ★ hull(hull(a,b),c) = hull(a,b,c) が成り立つ (中間の hull が落とすのは凸包に寄与しない内点
 *     だけ) ので、sig は fold 形で書いてよい = 木に分解してよい。
 *   ⚠ **入力は「点の集合」としてしか見られない**。穴も凹みも消えるので、hull は情報を落とす
 *     op であって形を整える op ではない。
 *   3D と 2D はここで振り分ける (3D と 2D を混ぜたらエラー)。失敗時は null + *errmsg。 */
sPtr<mfGeom> mf_hull_from_args(sArray<sPtr<pigData> > *args, const char **errmsg);

/* ★★ #3511: 線織 loft — @loft_ruled(断面, 断面, …)@。断面の列を **直線で結ぶ** 立体。
 *
 * ★★ 断面の **置き場所は op が決めない** (利用者が transform で空間に置く)。これが書けるのは
 *   #3526 で mf-cross2d が **枠 (平面)** を持つようになったから。
 * ★ なめらかな方 (@loft@) は **置かない** — 解析曲面が要るのでメッシュ系には無い (#3510 の表に出る差)。
 *
 * 対応づけの規約 = 弧長で正規化 + 全断面の頂点パラメータの **和集合**で標本化 + 前の断面の始点に
 *   世界座標で最も近い頂点を始点にする + 巡回の向きを軸に揃える。
 *   ★ occt と **厳密に一致する**ことを実測で確認 (頂点数が違っても・傾けても)。
 *   ⚠⚠ ただし **ねじれ (面内回転) は一致しない** (双線形パッチを三角形 2 枚では表せない)。
 *     ひさ判断で *そのまま受ける* — 立場は @circle(r,segs)@ の segs と同じで、細かくしたい人は
 *     断面を足す。詳細は mfMesh.cpp の但し書き。
 *
 * 失敗時は null + *errmsg (数を含む文言は errbuf に組む・呼び出しのローカルを渡すこと)。 */
sPtr<mfGeom> mf_loft_ruled_from_args(sArray<sPtr<pigData> > *args, const char **errmsg,
                                     char *errbuf, int errbufsz);

/* ★★ #3526: **空間に置かれた 2D を world Y 軸まわりに回す** (mfMesh.cpp)。
 *   ★ 軸を world Y に固定するのは ひさ判断 (extrude の「world +Z のまま」と同じ論法)。
 *     occt (ocaRevolve) と同じ規約なので、3 カーネルで同じことを答える。
 *   ⚠ 枠が既定のときはこれを **通さない** (従来の Manifold::Revolve のまま = 既存の値が動かない)。
 *   ⚠ 断面が軸をまたぐ / 軸が断面を貫く場合は明示エラー (黙って体積 0 を返さない)。 */
sPtr<mfMesh> mf_revolve_placed(sPtr<mfCross> in, double angle, int nseg,
                               const char **errmsg, char *errbuf, int errbufsz);
/* ★ #3512: refine(m, len) — **形を厳密に変えずに**面密度だけ上げる。
 *   中身は Manifold::RefineToLength (長い辺を len くらいの断片へ割り、内部頂点も足して
 *   三角形分割を揃える)。頂点は既存頂点の線形補間なので **体積・面積は bit 単位で不変**。
 *   ⚠ **remesh ではない** — 頂点を動かさず辺も潰さないので、針状三角形は割られても
 *     針状のまま残る (1x1x0.02 の板で最小角 1.15° が不変・単位箱では 45° → 23.2° と悪化)。
 *     「形を保ったまま密度を上げる」道具であって、質を上げる道具ではない。
 *   ★ RefineToLength は manifold の **eager op** なので ExecutionContext で中断できる
 *     (WithContext(ctx).RefineToLength(len) が上流の作法)。⇒ #3498 の配線対象。
 *   失敗/中断は null + *errmsg。3D 専用 (CrossSection に対応物は無い)。 */
sPtr<mfGeom> mf_refine_to_length(sPtr<mfMesh> in, double len, const pigBreak &brk, const char **errmsg);

/* ★ #3512 続き: simplify_cleanup(m, tol) — **許容差以下の特徴を潰して掃除する**。
 *   中身は Manifold::Simplify(tolerance)。上流の約束が明快で、
 *   *「結果は元の頂点の部分集合で、どの面も tol 未満しか動かない」*。
 *   ⇒ **新しい頂点を作らない**有界誤差の間引き。ブールの後に残る極小辺・針状三角形を
 *     落とす用途に向く (`simplify(m, n)` の「面数を指定・形は保つ」とは *約束が違う* ので
 *     名前を分けてある — 命名は「元の op 名 + _修飾」)。
 *   ⚠ 効き始めると **形は動く** (球 r=1 で tol=0.03 のとき体積 −2%・0.1 で −10.8%)。
 *     測定軸 (面数だけ振る) には使えない。
 *   ⚠ tol=0 は「メッシュ自身が持つ許容差を使う」= 厳密に組んだ形では実質そのまま返る。
 *   ⚠ Simplify は上流の **deferred op** (ctx を見ない) なので、中断は呼び手が mf_eval_err で取る。
 *   失敗は null + *errmsg。3D 専用。 */
sPtr<mfGeom> mf_simplify_cleanup(sPtr<mfMesh> in, double tol, const char **errmsg);

/* ★ #3475: エラー文言に名乗るモジュール名 (記述子の .name と同じ)。 */
#define MF_MODULE_NAME	"manifold"

/* ★ #3475: このモジュール専用のエラー生成子。文言は "[TAG] <name>/op: message" になる。
 *   素の mfa_err(...) を使うとモジュール名が付かない。 */
PIG_DEFINE_MODULE_ERR(mfa_err, MF_MODULE_NAME)

/* ★★ #3498: 呼び出し側の定型 — compute() の最後で **遅延木をここで評価する**。
 *   成功なら thNULL・中断/破綻ならエラー値 (呼び手は result へ入れて return するだけ)。
 *
 * ★ **どの op がこれを呼ぶか** — manifold が結果を *遅延* させる op だけ:
 *     union / intersection / difference   (演算子 + ^ - と BatchBoolean)
 *     combine                             (Compose)
 *     translate / rotate / scale / mirror / transform   (Manifold::Transform)
 *   これ以外 (box / sphere / extrude / import / tube …) は **その場で評価済みの葉**を作るので、
 *   呼んでも Status() が即返るだけ = 呼ぶ意味が無い。⚠ ただし「呼んでも害は無い」ので、
 *   遅延する op が将来増えたらそこにも足すこと。判断の根拠は manifold.h の WithContext の
 *   コメント (Deferred ops の一覧) で、**上流の実装詳細**なので版が上がったら読み直す。
 *
 * ⚠ 中断されたら **結果を捨ててエラーを返す**。manifold の中断は木にとって永続的なので、
 *   途中まで評価された木を返すと以後ずっと Cancelled のまま引きずる。加えて、中断を
 *   成功として返すとキャッシュに焼き付いて次回以降 *正しい答えとして引かれる* (#3489 の形)。 */
static inline sPtr<pigData>
mf_eval_err(sPtr<mfGeom> g, const pigBreak &brk, const char *op)
{
	if ( ! g.is_notNull() ) return sPtr<pigData>();   /* 既に失敗している = 呼び手が扱う */
	const char *why = 0;
	if ( g->force_eval(&brk, &why) ) return sPtr<pigData>();
	char m[192];
	if ( why != 0 && ::strcmp(why, "cancelled") == 0 )
		::snprintf(m, sizeof m, "%s: aborted (interrupted)", op);
	else
		::snprintf(m, sizeof m, "%s: %s", op, why ? why : "manifold evaluation failed");
	return sPtr<pigData>(mfa_err(m));
}

#endif /* MF_MESH_H */
