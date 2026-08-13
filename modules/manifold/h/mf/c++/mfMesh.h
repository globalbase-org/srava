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
#include	"manifold/manifold.h"
#include	"manifold/cross_section.h"
#include	<stdint.h>

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
	virtual bool write_to(const char *path, const char *unit) = 0;
	/* アフィン変換(行優先 double[12]・3D 規約)。2D(mfCross)は XY 2x2 + XY 平行移動だけ使う。 */
	virtual sPtr<mfGeom> apply_affine(const double e[12]) = 0;
	/* reader 用ファクトリ: D_META タグから具体型を生成(未知タグは null)。 */
	static sPtr<mfGeom> create_for_meta(const uint8_t *meta, int len);
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

	/* ---- アフィン変換(3D 型の行優先 double[12]。cgMesh3D::apply_affine と同じ規約)→ 新 mesh ---- */
	virtual sPtr<mfGeom> apply_affine(const double e[12]);

	/* ---- 計測 / 妥当性 ---- */
	double op_volume();   /* 囲む体積 */
	double op_area();     /* 表面積 */
	int    op_valid();    /* 1 = Status()==NoError かつ非空 / 0 = 破綻・空(サイレント破綻の検出点)*/
	int    op_bbox(double mn[3], double mx[3]);      /* 軸平行 AABB。返り=3 */
	int    op_centroid(double out[3]);               /* 体積重心(発散定理・GetMeshGL64)。返り=3 */

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
	manifold::Manifold	m_;
	int	meshExactInput_ = 0;   /* 1 = decode() が CGAL MESH 形式を読む(create_for_meta が MESH タグで立てる) */
};

/* ---- 2D manifold::CrossSection(extrude/revolve の断面) ---- */
class mfCross : public mfGeom {
public:
	mfCross(const manifold::CrossSection &c, sPtr<pigInfo> i = thNULL) : mfGeom(i), c_(c) {}

	manifold::CrossSection&       cross()       { return c_; }
	const manifold::CrossSection& cross() const { return c_; }

	virtual sPtr<stdString> get_str();

	virtual const char* meta_tag()  { return "MFC2"; }   /* Manifold 2D cross-section */
	virtual const char* type_name() { return "mf-cross2d"; }   /* rev4 実装型名 (MFC2 と 1:1) */
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
	int    op_bbox(double mn[3], double mx[3]);  /* 2D AABB(mn/mx の [0],[1] のみ)。返り=2 */

	/* ---- アフィン変換(2D: e[12] の XY 2x2 + XY 平行移動を使う)→ 新 mfCross ---- */
	virtual sPtr<mfGeom> apply_affine(const double e[12]);

	virtual bool write_to(const char *path, const char *unit);   /* 2D 出力は当面未対応(false) */

	/* ---- primitive(cga* の 2D 意味論に合わせる)---- */
	static sPtr<mfCross> polygon(const double *xy, int npts);   /* 点列(x,y の flat 配列・npts 個)から */
	static sPtr<mfCross> rect(double w, double h);              /* 原点隅の w*h(cgaRect 一致) */
	static sPtr<mfCross> circle(double r, int segs);           /* 半径 r・segs 角近似 */
	static sPtr<mfCross> ngon(int n, double r);                /* 正 n 角形(外接円半径 r) */

protected:
	void	decode_cross_exact(mfChunkSource& src);   /* CGAL PLY2(有理数リング)→ double CrossSection */
	manifold::CrossSection	c_;
	int	crossExactInput_ = 0;   /* 1 = decode() が CGAL PLY2 形式を読む(create_for_meta が PLY2 タグで立てる) */
};

#endif /* MF_MESH_H */
