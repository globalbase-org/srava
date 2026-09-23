#ifndef GU_GEOM_H
#define GU_GEOM_H
/*
 * guGeom — 素性を訊く計算のための **カーネル中立**の本体クラス (#3527)。
 *
 * ★★ なぜ中立か (ひさ設計 2026-09-17): bbox / area / centroid / valid / 位相 の中身は
 *   既に src/h/common/meshprops.h に **定義ごと 1 本**で書かれており、manifold / geogram /
 *   cherchi はどれも *内部表現から素の配列へ写して* そこを通っている。⇒ 実質「同じ型」を
 *   3 者が名乗らずに作っている。型として名乗らせ、1 モジュールが所有する。
 *   ⇒ points.so (#3528) と同じ形 — 外部ライブラリを持たず、常にビルドされる中立の型。
 *
 * ★ 2D も同じ相似形だった。3D の「塊 = 符号つき体積が正のシェル」に対して、
 *   2D は「塊 = **符号つき面積が正のリング**」(外周 CCW / 穴 CW)。⇒ ringprops.h を対で新設。
 *
 * 型は 3 本:
 *   gu-mesh3d    三角形スープ       guMesh   TriView  (meshprops.h)
 *   gu-cross2d   リング列           guPoly   RingView (ringprops.h)
 *   gu-face3d    リング列 + 枠      guPoly   同上 — #3533 の「置き場所の有無」を継ぐ
 *
 * ★★ 4CC は **共有する**。gu-mesh3d は "MFM3"・gu-cross2d / gu-face3d は "MFC2" で、
 *   manifold / geogram / cherchi が既に使っている形式そのもの (geogram と cherchi は
 *   現に MFM3 を共有している)。⇒ こちらが書いたキャッシュを向こうの create_for_meta が
 *   **無改造で読める**ので、part(mf-mesh3d,i) -> gu-mesh3d の往復に新しい変換が要らない。
 *   ⚠ 読む側は "MESH"(cgal) / "PLY2"(cgal) も受ける — そちらは厳密有理数なので decode で
 *     double へ落とす。⇒ 段 2 で入れる (いまは MFM3 / MFC2 だけ)。
 *
 * cache 形式は **既存とバイト単位で同じ**でなければならない (共有する以上そうなる):
 *   MFM3  [u32 nv][u32 nt] 頂点×nv(f64 x,y,z) 三角形×nt*3(u32)
 *         [u32 hasColor](+色×nv(u32 0xRRGGBB)) [u32 nmerge](+ (u32 from,u32 to)×nmerge)
 *         ⚠ 旧キャッシュは色節が無い ⇒ more() で判定する (mfMesh と同じ後方互換)
 *   MFC2  [u32 nrings] リング×([u32 npts] 点×(f64 x,y))
 *         [u32 "MARF"] + 枠 (fo,fu,fv 各 f64×3)   ← **face3d のときだけ**書く
 *
 * ⚠ 色と merge ベクタは **持ち回るだけ**で、この モジュールは中身を見ない。捨てると
 *   往復で情報が落ちる (manifold は merge が無いと非多様体になり volume=0 / valid=0 になる)。
 */
#include	"pig/c++/pigData.h"
#include	"pig/c++/pigModuleError.h"   /* #3475: 自分の名前でエラーを作る */
#include	"pig/c++/pigOpEntry.h"       /* pigWireClass (配線先) */
#include	"common/meshprops.h"         /* TriView  (3D) */
#include	"common/ringprops.h"         /* RingView (2D) */
#include	<stdint.h>
#include	<vector>

#define GU_MODULE_NAME	"geomutils"
#define GU_TYPE_3D	"gu-mesh3d"
#define GU_TYPE_2D	"gu-cross2d"
#define GU_TYPE_2DP	"gu-face3d"
#define GU_TAG_3D	"MFM3"
#define GU_TAG_2D	"MFC2"

/* codec の Sink/Source 抽象 (mfChunkSink / ptChunkSink と同シグネチャ)。 */
struct guChunkSink   { virtual void chunk(const uint8_t *data, int n) = 0; virtual ~guChunkSink()   {} };
struct guChunkSource { virtual void pull (uint8_t *dst, int n)        = 0;
                       virtual int  more()                            { return 1; }
                       virtual ~guChunkSource() {} };

class guGeom : public pigDataWireTyped {
public:
	guGeom(sPtr<pigInfo> i = thNULL) : pigDataWireTyped(i) {}

	virtual const char* meta_tag() = 0;   /* D_META 4 バイトタグ */
	virtual void	encode(guChunkSink&   sink) = 0;
	virtual void	decode(guChunkSource& src)  = 0;

	/* ★ #3433/#3479: 「読めたが受け取れない」を黙って空で返さないための対。 */
	int		decode_failed() const { return decodeErr_; }
	const char*	decode_why()    const { return decodeWhy_; }

	/* ---- 素性を訊く (#3527 段 3) ----
	 * ★ 返り値の約束は既存カーネル (cgMesh / mfGeom) と **同じ**。ここを揃えないと
	 *   「同じ op がカーネルごとに別のことを答える」を自分で作ることになる (#3487)。
	 *   op_bbox / op_centroid の返りは **成分数** (2D の cross2d は 2・face3d と 3D は 3)。
	 *   op_topology の返りは **closed** (0 なら genus は意味を持たない)。 */
	virtual int    op_nverts() = 0;
	virtual int    op_nfaces() = 0;
	virtual double op_area()   = 0;
	virtual int    op_valid()  = 0;
	virtual int    op_bbox(double mn[3], double mx[3]) = 0;
	virtual int    op_centroid(double c[3]) = 0;
	virtual int    op_topology(int *nshells, int *nparts, int *genus) = 0;

	/* ---- 片の取り出し (#3527 段 4) ----
	 * ★ 4 本とも「取れなければ **null + 理由**」の約束 (cgMesh3D::op_part と同じ形)。
	 *   黙って近いものを返さない — 「同じ式に 2 通りの値」を作らないため (#3516 / #3518-1)。
	 * ★★ @_at@ の 2 本は意味が **わざと違う**:
	 *     part_at  … 点を **含む** 塊   (立体は内側を持つ)
	 *     shell_at … 点に **いちばん近い** 殻 (曲面は内側を持たない ⇒ 最近傍しか言えない)
	 *   どちらも「その位置に在る片を指す」という 1 つの規約の、次元による 2 つの姿。
	 *   わけは meshprops.h の part_at / shell_at の頭に書いてある。
	 * ⚠ 2D に殻は無い ⇒ guPoly の op_shell / op_shell_at は理由を言って断る。 */
	virtual sPtr<guGeom> op_part    (int i,             const char **why) = 0;
	virtual sPtr<guGeom> op_part_at (const double p[3], const char **why) = 0;
	virtual sPtr<guGeom> op_shell   (int i,             const char **why) = 0;
	virtual sPtr<guGeom> op_shell_at(const double p[3], const char **why) = 0;

	/* ---- 頂点を読む (#3527 段 5) ----
	 * ★ 3 つ組の「取り出す」と「まとめて」。返りは **成分数** (bbox / centroid と同じ約束):
	 *     3D        … 3   (x,y,z)
	 *     cross2d   … 2   (枠が既定なので局所 = world)
	 *     face3d    … 3   **world**  ← ひさ判断 2026-09-17。#3533 が bbox / centroid で決めた
	 *                      「face3d は world」に揃えた。枠の中の 2 成分だと *置き場所が黙って落ち*、
	 *                      違う高さの断面が同じ答えを返してしまう (cgal も同時に直した)。
	 * ⚠⚠ op_vert と op_verts は **同じ列を同じ順**で歩くこと。⇒ verts(m) の i 番目 == vert(m,i)。
	 *   別々に書くと黙ってずれるので、実装は必ず 1 本の歩き方を共有する。
	 * ★ op_face_verts は面の **頂点番号** [i0,i1,i2] (座標ではない)。番号は op_vert の索引と同じ列。
	 *   返り 0 = その型は面を持たない (2D)。 */
	/* ★★ #3553 (2026-09-18・ひさ裁定): **点との距離**。定義は 3D / 2D で 1 つ —
	 *   「p から **面の集合** までの最短距離 (符号なし)」。@*-face3d@ / @*-cross2d@ は
	 *   *3D に埋め込まれた 2 次元* なので、3D の定義がそのまま当てはまる。
	 *   ⇒ occt / cgal / geogram / openvdb と同じ約束。mf / ch はここで初めて持つ。
	 *   ⚠ 「平面へ射影して 2D で測る」ことはしない — 面外の高さを黙って捨てる形になる
	 *     (#3533 / #3534 で何度も直した *置き場所が落ちる* 事故と同じ)。
	 *   ★ part_at が面外の点を **断る** のと非対称に見えるが、あちらは「どの片か」を
	 *     答える op で射影すると *嘘になる*。距離は面外成分を **答えに含める**ので落ちない。
	 *   返り 1 = 出せた / 0 = 出せない (+ why に理由)。 */
	virtual int op_distance_at(const double p[3], double *out, const char **why) = 0;

	virtual int op_vert(int i, double out[3]) = 0;
	virtual int op_verts(std::vector<double> &out) = 0;
	virtual int op_face_verts(int i, int out[3]) = 0;

	/* ---- #3579: 点群を立体 / 領域に対して **3 つに分ける** -----------------------
	 * @cls[i]@ = **0 境界ちょうど / -1 内側 (開) / +1 外側**。@pts@ は @dim@ 成分 x @npt@。
	 *
	 * ★★★ なぜ 3 値か — 境界ちょうどの点を内と外のどちらへ入れるかは **一意に決まらない**。
	 *   この木は同型の縮退を 3 回とも「黙って片方を選ばない」で解いている
	 *   (section の共面 / shell_at の同距離 / openvdb #3491 の零交差の帯)。⇒ 判定器に
	 *   答えさせず **第 3 の集合として切り出す** (ひさ 2026-09-22・#3575)。
	 *   ⚠ shell_at 式の明示エラーは採れない — 整数格子の点群では **97% が境界に載る**。
	 *
	 * ★ 分類は 1 回で 3 つぶん出す。⇒ mode 形を 3 回呼んでも **ここは 1 周**で済む形に
	 *   しておく (将来 sig に配列の出力型が入ったらそのまま使える)。
	 * ★★ 実体は共通ヘッダの @classify_point@ (3D=meshprops.h 巻き数 / 2D=ringprops.h)。
	 *   ⚠ 判定を各モジュールで書き直さないこと (#3575 落とし穴 D)。
	 * 返り 1 = 分けた / 0 = 断った (+ @why@ に理由)。 */
	virtual int op_classify_points(const double *pts, int npt, int dim,
	                               signed char *cls, const char **why) = 0;

	/* reader 用ファクトリ: D_META タグから具体型を生成 (未知タグは null)。 */
	static sPtr<guGeom> create_for_meta(const uint8_t *meta, int len);

	/* ★ ABI v12: **この階層への配線先**。op の OPS 行が OPWIRE(Calc, guGeom) と書くと、
	 *   引数はこの WIRE 経由で実体化される。定義は guCacheCodec.cpp。 */
	static const pigWireClass WIRE;

protected:
	void	set_decode_err(const char *why) { decodeErr_ = 1; decodeWhy_ = why; }
	int		decodeErr_ = 0;
	const char*	decodeWhy_ = 0;
};

/* ---- 3D: 三角形スープ ---------------------------------------------------------- */
class guMesh : public guGeom {
public:
	guMesh(sPtr<pigInfo> i = thNULL) : guGeom(i) {}

	virtual sPtr<stdString> get_str();   /* out-of-line = vtable/typeinfo anchor */
	virtual const char* type_name() { return GU_TYPE_3D; }
	virtual const char* meta_tag()  { return GU_TAG_3D; }

	virtual void	encode(guChunkSink&   sink);
	virtual void	decode(guChunkSource& src);
	/* ★ #3527 段 2: cgal の "MESH" (厳密有理数) を double へ落として読む経路。
	 *   create_for_meta が MESH タグで立てる (gg / ch / mf がまったく同じ形を持つ)。 */
	void	set_mesh_exact_input() { meshExactInput_ = 1; }

	int	nv() const { return (int)(coords_.size() / 3); }
	int	nt() const { return (int)(tris_.size()   / 3); }
	std::vector<double>&         coords()       { return coords_; }
	const std::vector<double>&   coords() const { return coords_; }
	std::vector<uint32_t>&       tris()         { return tris_; }
	const std::vector<uint32_t>& tris()   const { return tris_; }

	virtual int    op_nverts() { return nv(); }
	virtual int    op_nfaces() { return nt(); }
	virtual double op_area()   { return srava_mesh::area(view()); }
	virtual int    op_valid()  { return srava_mesh::valid(view()); }
	virtual int    op_bbox(double mn[3], double mx[3]);
	virtual int    op_centroid(double c[3]);
	virtual int    op_topology(int *nshells, int *nparts, int *genus);
	virtual sPtr<guGeom> op_part    (int i,             const char **why);
	virtual sPtr<guGeom> op_part_at (const double p[3], const char **why);
	virtual sPtr<guGeom> op_shell   (int i,             const char **why);
	virtual sPtr<guGeom> op_shell_at(const double p[3], const char **why);
	virtual int op_distance_at(const double p[3], double *out, const char **why);
	virtual int op_vert(int i, double out[3]);
	virtual int op_verts(std::vector<double> &out);
	virtual int op_face_verts(int i, int out[3]);
	virtual int op_classify_points(const double *pts, int npt, int dim,
	                               signed char *cls, const char **why);

	/* ★ #3527 段 4: 片を取り出すとき色と merge を **同じ並びで写す**ために要る。
	 *   ⚠ 捨てると往復で情報が落ちる — manifold は merge が無いと非多様体になり
	 *     volume=0 / valid=0 になる (guGeom.h 冒頭の約束そのもの)。 */
	std::vector<uint32_t>&       color()       { return color_; }
	const std::vector<uint32_t>& color() const { return color_; }
	std::vector<uint32_t>&       merge()       { return merge_; }
	const std::vector<uint32_t>& merge() const { return merge_; }

	/* ★ これがこのモジュールの存在理由 — meshprops.h の入口。 */
	srava_mesh::TriView view() const {
		return srava_mesh::TriView(coords_.empty() ? 0 : &coords_[0], nv(),
		                           tris_.empty()   ? 0 : &tris_[0],   nt());
	}

private:
	std::vector<double>	coords_;   /* 3*nv */
	std::vector<uint32_t>	tris_;     /* 3*nt */
	/* ⚠ 持ち回るだけ (中身は見ない)。捨てると往復で情報が落ちる。 */
	std::vector<uint32_t>	color_;    /* 0 または nv 個の packed 0xRRGGBB */
	std::vector<uint32_t>	merge_;    /* 2*nmerge (from,to) */
	int			meshExactInput_ = 0;   /* 1 = decode() が cgal "MESH" を読む */
	void	decode_mesh_exact(guChunkSource &src);
};

/* ---- 2D: リング列 (+ 枠) -------------------------------------------------------- */
class guPoly : public guGeom {
public:
	guPoly(sPtr<pigInfo> i = thNULL) : guGeom(i) {}

	virtual sPtr<stdString> get_str();
	/* ★ #3533: 同じ実体で **型は 2 つ**。置き場所を持つなら face3d。 */
	virtual const char* type_name() { return placed_ ? GU_TYPE_2DP : GU_TYPE_2D; }
	virtual const char* meta_tag()  { return GU_TAG_2D; }

	virtual void	encode(guChunkSink&   sink);
	virtual void	decode(guChunkSource& src);
	/* ★ #3527 段 2: cgal の "PLY2" (厳密有理数リング) を double へ落として読む経路。 */
	void	set_cross_exact_input() { crossExactInput_ = 1; }

	int	nrings() const { return (int)ringLen_.size(); }
	std::vector<double>&       xy()            { return xy_; }
	const std::vector<double>& xy()      const { return xy_; }
	std::vector<int>&          ringLen()       { return ringLen_; }
	const std::vector<int>&    ringLen() const { return ringLen_; }

	int	is_placed() const  { return placed_; }
	void	set_placed(int p)  { placed_ = p ? 1 : 0; }

	/* ---- 枠 (平面) ----
	 * ⚠ 枠は **値の属性**であってリングの形の性質ではない。ringprops.h は枠の中の (x,y)
	 *   しか見ない (mfCross が fo_/fu_/fv_ を自分で持つのと同じ)。
	 * ★★ このモジュールは枠を **運ぶだけ** — 法線から軸を導く規約 (plane_frame) には
	 *   一切触らない。導くのは section / polygonize / import の 3 箇所だけで、表は
	 *   src/h/common/affine.h に 1 つ (test/srava_frame_table.sh がそれを数えている)。
	 * ⚠ 名前と既定値は **mfMesh.h の mfCross と同じ**にしてある。片方だけ変えないこと。 */
	const double* frame_o() const { return fo_; }
	const double* frame_u() const { return fu_; }
	const double* frame_v() const { return fv_; }
	void set_frame(const double o[3], const double u[3], const double v[3]) {
		for ( int i = 0 ; i < 3 ; ++i ) { fo_[i] = o[i]; fu_[i] = u[i]; fv_[i] = v[i]; }
	}
	int frame_is_default() const {
		return ( fo_[0]==0 && fo_[1]==0 && fo_[2]==0
		      && fu_[0]==1 && fu_[1]==0 && fu_[2]==0
		      && fv_[0]==0 && fv_[1]==1 && fv_[2]==0 ) ? 1 : 0;
	}

	/* ⚠ 2D は **面を持たない** ので nfaces は 0 (cgal / mf と同じ約束)。 */
	virtual int    op_nverts() { return srava_poly::nverts(view()); }
	virtual int    op_nfaces() { return 0; }
	virtual double op_area()   { return srava_poly::area(view()); }
	virtual int    op_valid()  { return srava_poly::valid(view()); }
	virtual int    op_bbox(double mn[3], double mx[3]);
	virtual int    op_centroid(double c[3]);
	/* ⚠ 2D に nshells / genus は無い (曲面の量・#3525) ⇒ 0 のまま。nparts だけ答える。 */
	virtual int    op_topology(int *nshells, int *nparts, int *genus);
	virtual sPtr<guGeom> op_part    (int i,             const char **why);
	virtual sPtr<guGeom> op_part_at (const double p[3], const char **why);
	/* ⚠ 2D に殻は無い ⇒ 理由を言って断る (null + why)。★ 通常経路では sig が先に弾くので
	 *   ここへは来ない — *sig を書き換えた人* への保険 (cgaShell と同じ構え)。 */
	virtual sPtr<guGeom> op_shell   (int i,             const char **why);
	virtual sPtr<guGeom> op_shell_at(const double p[3], const char **why);
	virtual int op_vert(int i, double out[3]);
	virtual int op_verts(std::vector<double> &out);
	/* ⚠ 2D は面を持たない ⇒ 0 を返す (op 側が明示エラーにする)。 */
	virtual int op_face_verts(int, int[3]) { return 0; }
	virtual int op_distance_at(const double p[3], double *out, const char **why);
	virtual int op_classify_points(const double *pts, int npt, int dim,
	                               signed char *cls, const char **why);

	/* ★ #3553: world の点を枠へ落とす — @to_local@ と違って **面外成分も返す** (断らない)。
	 *   距離は面外成分を答えに含めるので、ここで落としてはいけない。 */
	void to_local_full(const double w[3], double xy[2], double *offPlane) const;

	/* ★ #3527 段 4: world の点を枠へ落とす (to_world の逆)。枠は正規直交なので内積 3 本。
	 *   返り 0 = その点は **この 2D の平面上に無い** (黙って射影しない)。 */
	int to_local(const double w[3], double xy[2]) const;

	/* 枠の中の (x,y) → world。face3d の bbox / centroid が 3 成分を返すのに使う。 */
	void to_world(double x, double y, double w[3]) const {
		for ( int i = 0 ; i < 3 ; ++i ) w[i] = fo_[i] + x*fu_[i] + y*fv_[i];
	}

	srava_poly::RingView view() const {
		return srava_poly::RingView(xy_.empty()      ? 0 : &xy_[0],
		                            ringLen_.empty() ? 0 : &ringLen_[0], nrings());
	}

private:
	std::vector<double>	xy_;        /* 2*(Σ ringLen) */
	std::vector<int>	ringLen_;
	int			placed_ = 0;
	double			fo_[3] = {0,0,0};
	double			fu_[3] = {1,0,0};
	double			fv_[3] = {0,1,0};
	int			crossExactInput_ = 0;   /* 1 = decode() が cgal "PLY2" を読む */
	void	decode_cross_exact(guChunkSource &src);
};

/* ★ #3475: このモジュール専用のエラー生成子。文言は "[TAG] geomutils/op: message" になる。 */
PIG_DEFINE_MODULE_ERR(gua_err, GU_MODULE_NAME)

#endif
