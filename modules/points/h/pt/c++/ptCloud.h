#ifndef PT_CLOUD_H
#define PT_CLOUD_H
/*
 * ptCloud — 点群を値として持つ **カーネル中立**の本体クラス (#3528)。
 *
 * ★★ なぜ中立か (ひさ判断 2026-09-13): 点群を使う外部ライブラリは 5 つあり (CGAL / geogram /
 *   OCCT / OpenVDB / manifold)、**そのどれもが「平坦な double 配列」で受け取る**。
 *     geogram   Delaunay::set_vertices(nb, const double*)  / Mesh::vertices.assign_points(const double*, dim, nb)
 *     CGAL      レンジ + property map (コンテナ自由。点集合処理は EPICK = double)
 *     OCCT      TColgp_Array1OfPnt (曲線) / ⚠ Array2OfPnt = 行×列の格子 (曲面フィット)
 *     OpenVDB   粒子リスト (getPos(n,xyz) を持つ任意のクラス)
 *     manifold  Manifold::Hull(const std::vector<vec3>&)
 *   ⇒ メッシュと違い **「保存すべきカーネル固有表現」が無い**。EPECK Surface_mesh ⇄ manifold の
 *     半辺構造のような情報量の差が無く、変換は 0 コピーか線形 1 パス。だから型をどれか 1 つの
 *     カーネルに置く理由が無く、置くと残り 4 つがそこに (cgal なら GPL に) 依存することになる。
 *   ⇒ 本体クラスと reader/writer を **libsrava_pt** に置き、points.so が型を名乗る。
 *     他モジュール (cgal / geogram / …) は occt_mf が mfGeom を借りるのと同じ作法で
 *     このクラスをそのまま使う (新しいクラスも wire 形式も作らない)。
 *
 * 内部表現: 平坦な double 配列。
 *   xyz_ = dim_ * np 個 (座標)      nrm_ = dim_ * np 個 または空 (法線)
 *
 * ★★ 印は 2 つ — **法線があるか** と **向き付けされているか** は別の事実。
 *   CGAL Shape_detection (RANSAC) は **向きなし**で足り、CGAL Poisson / geogram Co3Ne_reconstruct は
 *   **向きあり**が必須。どちらも法線が無ければ答えを出せないが、要求の強さが違う。
 *   ⇒ has_normals() と oriented() を別に持ち、**両方ともキャッシュに載せる** (載せないと cold と
 *     warm で答えが変わる。前例は openvdb の is_normalized)。
 *   ⚠⚠ **既定の法線は作らない**。(0,0,1) 等で埋めると Poisson も RANSAC もエラーにならずに走り、
 *     **静かに嘘の形**を返す。無ければ要求する op が明示エラーにする。
 *
 * 規約: **明示的に与えられた法線は向き付けされているとみなす** (points3d の入れ子形・xyz の 6 列)。
 *   与えた人が向きに意味を持たせている、と読む。推定した法線は estimate_normals が実際に
 *   向き付けできたときだけ印を立てる。
 *
 * cache 形式 (D_META 4CC "PTC2" / "PTC3"・little-endian):
 *   [u32 np][u32 flags] 座標×np(double × dim) [法線×np(double × dim)]
 *     flags bit0 = 法線あり / bit1 = 向き付けあり
 *   ★ 次元は **4CC が持つ** (PTC2=2D / PTC3=3D)。payload には書かない — cgal が MESH/PLY2 で
 *     次元をタグに畳んでいるのと同じ。二重に持つと必ずずれる。
 */
#include	"pig/c++/pigData.h"
#include	"pig/c++/pigModuleError.h"   /* #3475: 自分の名前でエラーを作る */
#include	"pig/c++/pigOpEntry.h"       /* pigWireClass (配線先) */
#include	<stdint.h>
#include	<vector>

#define PT_MODULE_NAME	"points"
#define PT_TYPE_2D	"pt-cloud2d"
#define PT_TYPE_3D	"pt-cloud3d"
#define PT_TAG_2D	"PTC2"
#define PT_TAG_3D	"PTC3"

/* codec の Sink/Source 抽象 (cgChunkSink/ggChunkSink と同シグネチャ)。 */
struct ptChunkSink   { virtual void chunk(const uint8_t *data, int n) = 0; virtual ~ptChunkSink()   {} };
struct ptChunkSource { virtual void pull (uint8_t *dst, int n)        = 0;
                       virtual int  more()                            { return 1; }
                       virtual ~ptChunkSource() {} };

class ptCloud : public pigDataWireTyped {
public:
	ptCloud(sPtr<pigInfo> i = thNULL) : pigDataWireTyped(i) {}

	virtual sPtr<stdString> get_str();   /* 表示用 (out-of-line = vtable/typeinfo anchor) */

	/* ---- 型軸 / キャッシュ認識 ----
	 * ★ 2D と 3D で **クラスを分けない**。表現が完全に同じ (平坦 double 配列) なので、
	 *   分けると同じコードを 2 本持つだけになる。型名とタグは dim_ から引く。
	 *   ⚠ 型そのものは分かれている (pt-cloud2d / pt-cloud3d)。leaf op は実行時に出力型を
	 *     選べないので、points2d / points3d と **op 名で**分けてある (#3528 本文 2 節)。 */
	virtual const char* type_name() { return ( dim_ == 2 ) ? PT_TYPE_2D : PT_TYPE_3D; }
	const char*         meta_tag()  { return ( dim_ == 2 ) ? PT_TAG_2D  : PT_TAG_3D;  }

	int	dim() const	{ return dim_; }
	void	set_dim(int d)	{ dim_ = d; }
	int	np() const	{ return ( dim_ > 0 ) ? (int)(xyz_.size() / (size_t)dim_) : 0; }
	int	has_normals() const	{ return ! nrm_.empty(); }
	int	oriented() const	{ return oriented_; }
	void	set_oriented(int o)	{ oriented_ = o; }

	std::vector<double>&       xyz()       { return xyz_; }
	const std::vector<double>& xyz() const { return xyz_; }
	std::vector<double>&       nrm()       { return nrm_; }
	const std::vector<double>& nrm() const { return nrm_; }

	/* ---- codec (D_CHUNK ストリーム) ---- */
	void	encode(ptChunkSink&   sink);
	void	decode(ptChunkSource& src);
	/* ★ #3433/#3479: 「読めたが受け取れない」を黙って空で返さないための対。 */
	int	    decode_failed() const { return decodeErr_; }
	const char* decode_why()    const { return decodeWhy_; }

	/* ---- 計測 (既存 op の約束のまま。返り 0 = 出せない = 空の点群) ---- */
	int	op_bbox(double mn[3], double mx[3]) const;   /* 軸平行 AABB。返り = 次元 */
	int	op_centroid(double out[3]) const;            /* **点の平均**。返り = 次元 */
	/* ★ valid は共通定義 (空でない ∧ 閉じている ∧ 自己交差が無い) のうち **① だけ**が
	 *   意味を持つ。②③ は点群では構造的に恒真 (openvdb の距離場と同じ扱い)。 */
	int	op_valid() const { return ( np() > 0 ) ? 1 : 0; }

	/* ---- xyz ファイル (形式は当面これだけ・ひさ判断 2026-09-13) ----
	 * ★ 平坦な 3 列 (x y z) または **6 列 (x y z nx ny nz)**。6 列は geogram の XYZIOHandler /
	 *   CGAL read_xyz_points と同じ並びで、法線をそのまま運べる。
	 * ⚠ 2 列は z=0 の 3D として読む (geogram と同じ)。**2D 点群にはしない** — .xyz は 3D の形式で、
	 *   拡張子から型が決まる以上ここで 2D を返すと routing の申告と食い違う。
	 * err/errsz を渡すと失敗理由が書かれる (モジュールに static を置かないため戻り値経由)。 */
	static sPtr<ptCloud>	read_xyz(const char *path, char *err, int errsz);
	bool			write_to(const char *path, const char *unit);

	/* reader 用ファクトリ: D_META タグから具体型を生成 (未知タグは null)。 */
	static sPtr<ptCloud> create_for_meta(const uint8_t *meta, int len);

	/* ★ ABI v12: **この階層への配線先**。op の OPS 行が OPWIRE(Calc, ptCloud) と書くと、
	 *   引数はこの WIRE 経由で実体化される。定義は ptCacheCodec.cpp。 */
	static const pigWireClass WIRE;

protected:
	void	set_decode_err(const char *why) { decodeErr_ = 1; decodeWhy_ = why; }

	std::vector<double>	xyz_;
	std::vector<double>	nrm_;
	int			dim_       = 3;
	int			oriented_  = 0;
	int			decodeErr_ = 0;
	const char*		decodeWhy_ = 0;
};

/* 値配列から点群を組む (points2d / points3d の中身)。dim = 2 or 3。
 *   [[x,y,z], ...]                 法線なし — 各要素が **点そのもの**  (polygon と同じ形)
 *   [[[x,y,z],[nx,ny,nz]], ...]    法線あり — 各要素が **[点, 法線]**  (tube と同じ形)
 * ★ 判定は「最初の子要素が数かリストか」だけ (長さを数えない)。⚠ 混在は明示エラー。
 * 失敗時は null を返し err に理由を書く (モジュールに static を置かないため戻り値経由)。 */
sPtr<ptCloud>	pt_cloud_from_value(sPtr<pigData> v, int dim, char *err, int errsz);

/* 擬似乱数で点群を組む (rand#pt2d / rand#pt3d の中身)。dim = 2 or 3。
 *   a, b = 長さ dim の配列 (軸ごとの区間) ・ n = 点数 ・ s = シード (省略不可)。
 * ★ 軸ごとの区間の読み方と「整数か浮動小数点か」の規則は pt/c++/ptRandom.h。
 * ⚠ **法線は付けない**。無い法線を (0,0,1) 等で埋めると Poisson も RANSAC もエラーにならずに
 *   走って静かに嘘の形を返す (ptCloud.h 冒頭の「既定の法線は作らない」と同じ理由)。
 * 失敗時は null を返し err に理由を書く (pt_cloud_from_value と同じ流儀)。 */
sPtr<ptCloud>	pt_cloud_random(sPtr<pigData> a, sPtr<pigData> b, sPtr<pigData> n, sPtr<pigData> s,
	                        int dim, const char *opn, char *err, int errsz);

/* ★★ #3576: **正規分布**で点群を組む (rand_gaussian#pt2d / #pt3d の中身)。dim = 2 or 3。
 *   c = 長さ dim の配列 (中心) ・ sigma = **各軸の標準偏差** ・ n = 点数 ・ s = シード。
 * ★ 各軸が独立に N(c[k], sigma) ⇒ 2D なら円形・3D なら球形の対称性を持つ等方ガウス。
 *   ⚠ 「距離の標準偏差」ではない (pt/c++/ptRandom.h の pt_rand_gauss_spec を参照)。
 * ⚠ 一様版と同じく **法線は付けない**。
 * 失敗時は null を返し err に理由を書く。 */
sPtr<ptCloud>	pt_cloud_random_gauss(sPtr<pigData> c, sPtr<pigData> sigma,
	                              sPtr<pigData> n, sPtr<pigData> s,
	                              int dim, const char *opn, char *err, int errsz);

/* ★★ #3577: **混合分布**から点群を組む (rand_gaussian の 点群入力 の行の中身)。
 *   in の各点を中心とする等方ガウスを **均等に重ね合わせた分布**から n 点を引く。
 *
 * ★★★ 「各入力点に (中心版) を施したもの」とは **違う**。重ね合わせた分布から引くので、
 *   実装は次と厳密に等価:
 *       n 回くり返す: 中心を **一様に 1 つ**選ぶ → その中心から N(0, sigma) を 1 点
 *   ⇒ K が大きく n が小さいと **点を 1 つも貰わない中心**が出る (混合分布として正しい)。
 *   ⇒ 出力の点数は **n** (入力の K とは無関係)。
 * ⚠ **入力点群の順序が結果に効く** (順序は #3527 で「格納順 = 入力の順」と定義済み)。
 * ⚠ 入力が空 (K==0) は **明示エラー** — 中心が 1 つも無いので分布が定義できない。
 * ⚠ 一様版と同じく **法線は付けない** (入力の法線も引き継がない — 新しい点なので)。
 * 失敗時は null を返し err に理由を書く。 */
sPtr<ptCloud>	pt_cloud_random_gauss_mix(sPtr<ptCloud> in, sPtr<pigData> sigma,
	                                  sPtr<pigData> n, sPtr<pigData> s,
	                                  const char *opn, char *err, int errsz);

/* ★★ #3578: **アフィン変換**を当てた新しい点群 (in は変えない)。
 *   e = 行優先 3x4 (common/affine.h の規約) ・ outDim = 結果の次元 (2 or 3)。
 *
 * ★★ outDim を **引数で受け取る**のがこの関数の要。どの次元を返すかは *routing が行を選ぶとき
 *   と同じ述語* (pt/c++/ptAffine.h の pt_affine_out_dim) が決めており、ここで独自に決め直すと
 *   「sig は pt-cloud2d と言っているのに 3 次元が返る」が起きる (#3554 段5a と同じ形)。
 *
 * ---- 法線 ----
 * ★ 法線は **ベクトルではなく余ベクトル**なので、点と同じ行列を当ててはいけない
 *   (scale([2,1,1]) で法線が傾く)。
 *     3D … n' = M^-T n = cof(M) n / det(M)   ⚠ det で割る (符号) — 割らないと反射で裏返る
 *     2D … 面内の法線なので、*像の平面の中で* 変換後の接線に直交する向きを取る。
 *          n' ∝ B (-J G J n) ・ B = 線形部の第 1・2 列 (3x2) ・ G = BᵀB ・ J = 90 度回転。
 *          ★ 平面を保つ変換ではこれは 2x2 の逆転置 A^-T と **厳密に一致する** (実測で確認)。
 * ⚠ 法線の長さは 1 に直す。向き付けの印 (oriented) はそのまま運ぶ。
 * 失敗時は null を返し err に理由を書く。 */
sPtr<ptCloud>	pt_cloud_affine(sPtr<ptCloud> in, const double e[12], int outDim,
	                        const char *opn, char *err, int errsz);

/* ★★ #3578: 2 つの点群を **単純に混ぜる** (union)。重複は落とさない。
 *   ⇒ 不変条件 @nverts(union(a,b)) == nverts(a) + nverts(b)@。
 *
 * ★ 次元は **どちらかが 3D なら 3D** (2D 側は z=0 とみなして昇格する)。
 * ★ 並びは **a のあと b** — 索引 (vert) は「格納順 = 入力の順」と定義済み (#3527) なので
 *   union(a,b) と union(b,a) は *同じ点集合で違う点群* になる。
 *   ⚠⚠ だから OPS 行に **可換の印を立てない**。立てるとキャッシュキーが正規化されて
 *     両者が同じ結果を返す = 索引の約束が静かに破れる。
 * ★ 法線は **空でない側がどちらも持っているときだけ**運ぶ。片方にしか無ければ落とす —
 *   無い方を (0,0,1) 等で埋めると Poisson も RANSAC もエラーにならずに走り、静かに嘘の形を
 *   返す (ptCloud.h 冒頭の「既定の法線は作らない」と同じ理由)。落ちたことは *下流の op が
 *   明示エラーで言う* ので、黙って嘘になる経路は無い。
 *   ⚠ 空の点群は法線の有無を問わない — でないと @union(p, points3d([]))@ が恒等でなくなる。
 * 失敗時は null を返し err に理由を書く。 */
sPtr<ptCloud>	pt_cloud_union(sPtr<ptCloud> a, sPtr<ptCloud> b,
	                       const char *opn, char *err, int errsz);

/* ★ #3475: 自分の名前でエラーを作る。素の thNEW(pigDataError,...) はモジュール名が付かない。 */
PIG_DEFINE_MODULE_ERR(pta_err, PT_MODULE_NAME)

#endif /* PT_CLOUD_H */
