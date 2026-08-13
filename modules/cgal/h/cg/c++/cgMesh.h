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
 * CGAL を要するので srava_agent 側でのみ compile。
 */
#include	"pig/c++/pigData.h"
#include	<CGAL/Exact_predicates_exact_constructions_kernel.h>
#include	<CGAL/Surface_mesh.h>
#include	<CGAL/IO/Color.h>   /* 面の色(per-face property map "f:color")。color() op / 色つき export 用 */
#include	<CGAL/Polygon_2.h>
#include	<CGAL/Polygon_with_holes_2.h>
#include	<vector>
#include	<stdint.h>

/* codec の Sink/Source 抽象(encode/decode を virtual 化するため。writer/reader が adapter で実装)。 */
struct cgChunkSink   { virtual void chunk(const uint8_t *data, int n) = 0; virtual ~cgChunkSink()   {} };
struct cgChunkSource { virtual void pull (uint8_t *dst, int n)        = 0;
                       /* まだ読めるデータが残っているか(後方互換: 旧 blob で末尾の任意セクションを
                        * 読むかの判定。W_END に達していれば 0)。既定 1(完全バッファ前提)。 */
                       virtual int  more()                            { return 1; }
                       virtual ~cgChunkSource() {} };

class cgMesh : public pigDataWireTyped {   /* rev4 Phase A: 型軸 marker 基底 (旧 pigData 直継承) */
public:
	/* カーネルは 2D/3D 共通(EPECK)。Point_3/Mesh は 3D 用だが当面基底に置く(参照箇所の churn 削減。
	 * Step C 以降で cgMesh3D へ降ろしてよい)。 */
	typedef CGAL::Exact_predicates_exact_constructions_kernel	K;
	typedef K::Point_3						Point_3;
	typedef CGAL::Surface_mesh<Point_3>				Mesh;

	cgMesh(sPtr<pigInfo> i = thNULL) : pigDataWireTyped(i) {}

	virtual sPtr<stdString> get_str();   /* 表示用(out-of-line = vtable/typeinfo anchor) */

	/* ---- キャッシュ認識(D_META)---- */
	virtual const char* meta_tag()  = 0;   /* D_META 4 バイトタグ "MESH"/"PLY2" */
	virtual uint16_t    repr_type() = 0;   /* 1=3D mesh, 32=2D poly(catalog §5) */
	virtual int         dim()       = 0;   /* 2 or 3 */
	/* ---- codec(D_CHUNK ストリーム)---- */
	virtual void encode(cgChunkSink&)   = 0;
	virtual void decode(cgChunkSource&) = 0;
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
	virtual double op_volume()    = 0;   /* 3D: 囲む体積(閉メッシュ)/ 2D: 0(呼び元が dim ガード) */
	virtual double op_perimeter() = 0;   /* 2D: 境界長(外周+穴)/ 3D: 0(呼び元が dim ガード) */
	virtual int    op_centroid(double out[3]) = 0;   /* 面積/体積重心。out に座標、返り=次元(2 or 3) */
	virtual int    op_bbox(double mn[3], double mx[3]) = 0;   /* 軸平行 AABB。mn/mx に min/max 隅、返り=次元(2 or 3) */

	/* ---- 検査/修復 ---- */
	virtual int          op_valid()  = 0;   /* 1=正常 / 0=問題(3D: 閉∧¬自己交差 / 2D: 全リング単純)。値返し */
	virtual sPtr<cgMesh> op_repair() = 0;   /* 修復した同次元の新 mesh(3D: autorefine / 2D: even-odd repair)。失敗は null */

	/* ---- 着色: 全面に色(r,g,b: 0-255)を付けた同次元の新 mesh。per-face property map "f:color"。
	 *      combine(+++)で各成分の色が保持され、色対応の export(3MF/AMF/OFF/PLY)で出る。3D 専用(2D は null)。 ---- */
	virtual sPtr<cgMesh> op_color(int r, int g, int b) = 0;

	/* ---- 断面: 点 P を通り法線 N の平面でメッシュを切り、2D 断面(cgMesh2D)を返す。
	 *      3D 専用(2D は null=エラー)。面内の正規直交基底で 2D に射影 → even-odd 充填。失敗/退化 N は null。 ---- */
	virtual sPtr<cgMesh> op_section(const double P[3], const double N[3]) = 0;

	/* ---- ファイル書き出し(拡張子で形式判定。3D=OFF/STL/.. / 2D=SVG/DXF)。成否を返す。
	 *      unit = 単位文字列("mm"/"cm"/"in"/...)。SVG=width/height に付与、DXF=$INSUNITS、
	 *      単位概念のない形式(OFF/STL/..)や空文字は無視。 ---- */
	virtual bool write_to(const char *path, const char *unit) = 0;

	/* reader 用ファクトリ: D_META タグから具体型を生成(未知タグは null)。 */
	static sPtr<cgMesh> create_for_meta(const uint8_t *meta, int len);
};

/* 3D メッシュ(EPECK Surface_mesh)。 */
class cgMesh3D : public cgMesh {
public:
	cgMesh3D(sPtr<pigInfo> i = thNULL) : cgMesh(i) {}

	Mesh& mesh() { return m_; }   /* 3D 固有(プリミティブ生成/export が直接触る) */

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
	virtual sPtr<cgMesh> op_union       (sPtr<cgMesh> b);
	virtual sPtr<cgMesh> op_intersection(sPtr<cgMesh> b);
	virtual sPtr<cgMesh> op_difference  (sPtr<cgMesh> b);
	virtual sPtr<cgMesh> op_combine     (sPtr<cgMesh> b);   /* 両 Surface_mesh を連結(corefinement なし) */
	virtual sPtr<cgMesh> apply_affine(const double e[12]);
	virtual sPtr<cgMesh> op_offset(double d, int subdiv);   /* Minkowski(icosphere)膨張/収縮 */
	virtual double op_area();   /* 表面積(PMP::area・√含むので double) */
	virtual double op_volume();      /* PMP::volume(閉メッシュの体積) */
	virtual double op_perimeter();   /* 3D は未定義(呼び元 dim ガード)→ 0 */
	virtual int    op_centroid(double out[3]);   /* 体積重心(四面体分割・発散定理) */
	virtual int    op_bbox(double mn[3], double mx[3]);   /* 全頂点走査の AABB(3D・返り 3) */
	/* 近接(3D 限定・binary)。farthest=false で最近接(AABB・両方向頂点-面の近似)、true で最遠(頂点ペア
	 * 総当り=厳密だが O(n·m))。pa/pb に各メッシュ上の点、返り=距離。3D-3D 専用(呼び元が cgMesh3D に d_cast)。 */
	double op_proximity(cgMesh3D& b, bool farthest, double pa[3], double pb[3]);
	/* 肉厚解析(SDF=Shape Diameter Function)。各面で内向き錐状レイを飛ばし反対側壁までの距離=
	 * その場所の肉厚を測る(3Dプリント時の「薄くて割れる」箇所検出)。t_min 未満の面の重心+厚みを
	 * out へ flat に push(x,y,z,thk を 4 個ずつ)。返り=全面の最小肉厚(空/解析不能は 0)。
	 * rays=面ごとのレイ本数(計算時間にほぼ比例。少=粗く速い/多=滑らかで遅い)。
	 * レイ投射は EPICK コピー上で行う(EPECK 厳密レイは非現実的に重い=測定値なので double 近似で十分)。 */
	double op_thin_spots(double t_min, int rays, double cone_deg, std::vector<double>& out);
	virtual int          op_valid();   /* is_closed ∧ ¬does_self_intersect */
	virtual sPtr<cgMesh> op_repair();  /* PMP::autorefine(自己交差を幾何解消) */
	virtual sPtr<cgMesh> op_color(int r, int g, int b);   /* 全面に f:color を付けた新 mesh */
	virtual sPtr<cgMesh> op_section(const double P[3], const double N[3]);   /* 平面で切った 2D 断面 */
	virtual bool write_to(const char *path, const char *unit);   /* OFF/STL/OBJ/PLY(unit 無視) */
protected:
	void	decode_mfm3(cgChunkSource&);   /* MFM3 raw-double framing → EPECK Surface_mesh(無損失昇格) */
	Mesh	m_;
	int	mfm3Input_ = 0;   /* 1 = decode() が MFM3 framing を読む(create_for_meta が MFM3 タグで立てる) */
};

/* 2D 多角形領域(EPECK)。穴あき多角形の集合 = ブール演算結果(複数連結成分・穴)を表せる。
 * extrude/revolve で 3D(cgMesh3D)に持ち上がる。 */
class cgMesh2D : public cgMesh {
public:
	typedef CGAL::Polygon_2<K>		Polygon_2;
	typedef CGAL::Polygon_with_holes_2<K>	Pwh_2;

	typedef std::vector<K::Point_2>		Guide;   /* 開ポリライン(寸法線/ガイド)。閉じない */

	cgMesh2D(sPtr<pigInfo> i = thNULL) : cgMesh(i) {}

	std::vector<Pwh_2>& regions() { return regions_; }   /* 2D 固有(rect/extrude が直接触る) */
	std::vector<Guide>& guides()  { return guides_; }    /* ガイド層(line)。塗りでなく SVG ストロークで描く */

	virtual sPtr<stdString> get_str();
	virtual const char* meta_tag()  { return "PLY2"; }
	virtual const char* type_name() { return "cg-cross2d"; }   /* rev4 実装型名 (PLY2 と 1:1) */
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
	virtual double op_volume();      /* 2D は体積なし(呼び元 dim ガード)→ 0 */
	virtual double op_perimeter();   /* 境界長(全 region の外周+穴。√→double) */
	virtual int    op_centroid(double out[3]);   /* 面積重心(shoelace モーメント・穴は負寄与) */
	virtual int    op_bbox(double mn[3], double mx[3]);   /* 外周頂点走査の AABB(2D・返り 2) */
	virtual int          op_valid();   /* 全 region の外周/穴が is_simple */
	virtual sPtr<cgMesh> op_repair();  /* Polygon_repair::repair(even-odd) */
	virtual sPtr<cgMesh> op_color(int r, int g, int b);   /* 2D は非対応(null=エラー) */
	virtual sPtr<cgMesh> op_section(const double[3], const double[3]) { return sPtr<cgMesh>(); }   /* 2D は断面なし */
	virtual bool write_to(const char *path, const char *unit);   /* SVG(width/height)/DXF($INSUNITS) */
protected:
	void	decode_mfc2(cgChunkSource&);   /* MFC2 raw-double リング列 → Pwh_2(無損失昇格) */
	std::vector<Pwh_2>	regions_;   /* 穴あき多角形の集合(空 = 空領域) */
	std::vector<Guide>	guides_;    /* ガイド層(開ポリライン群)。ブール演算は触れず、SVG/DXF で線として描く */
	int	mfc2Input_ = 0;   /* 1 = decode() が MFC2 framing を読む(create_for_meta が MFC2 タグで立てる) */
};

#endif /* CG_MESH_H */
