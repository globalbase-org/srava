/*
 * guGeom — 中立の本体クラスの実体 (#3527)。codec は **既存の 4CC とバイト単位で同じ**。
 *   MFM3 は mfMesh::encode / MFC2 は mfCross::encode の framing をそのまま写している。
 *   ⚠⚠ 片方だけ直さないこと — 共有している以上、ずれた瞬間に *相手のキャッシュが読めなく
 *     なる* のではなく **黙って違う形に読める** (長さだけ合っていれば通ってしまう)。
 */
#include	"pig/c++/pigData.h"
#include	"pig/c++/ptsObject.h"
#include	"gu/c++/guGeom.h"
#include	"common/exact_wire.h"   /* cgal 厳密 wire の有理数文字列パーサ (mf / gg / ch と共通) */
#include	"ts2/c++/stdString.h"
#include	<string.h>
#include	<stdio.h>
#include	<math.h>
#include	<algorithm>

/* ---- little-endian の読み書き (全対象 LE 前提・mfMesh.cpp と同じ) ---------------- */
static void put_u32(guChunkSink &s, uint32_t v) {
	uint8_t b[4] = { (uint8_t)v, (uint8_t)(v>>8), (uint8_t)(v>>16), (uint8_t)(v>>24) };
	s.chunk(b, 4);
}
static void put_f64(guChunkSink &s, double d) {
	uint8_t b[8]; ::memcpy(b, &d, 8); s.chunk(b, 8);
}
static uint32_t get_u32(guChunkSource &s) {
	uint8_t b[4]; s.pull(b, 4);
	return (uint32_t)b[0] | ((uint32_t)b[1]<<8) | ((uint32_t)b[2]<<16) | ((uint32_t)b[3]<<24);
}
static double get_f64(guChunkSource &s) {
	uint8_t b[8]; s.pull(b, 8); double d; ::memcpy(&d, b, 8); return d;
}

#define GU_FRAME_MARK	0x4652414dU	/* "MARF" — mfCross が書く枠マーカ */

/* ---- ファクトリ ----------------------------------------------------------------- */
sPtr<guGeom>
guGeom::create_for_meta(const uint8_t *meta, int len)
{
	if ( len != 4 || meta == 0 )
		return sPtr<guGeom>();
	if ( ::memcmp(meta, GU_TAG_3D, 4) == 0 )
		return sPtr<guGeom>::d_cast(thNEW(guMesh,()));
	if ( ::memcmp(meta, GU_TAG_2D, 4) == 0 )
		return sPtr<guGeom>::d_cast(thNEW(guPoly,()));
	/* ★★ #3527 段 2: cgal の "MESH" / "PLY2" (厳密有理数) も受ける。
	 *   ⚠⚠ **前提が途中で変わった箇所**。素性 op だけを寄せる設計では cg-* を sig で名乗らない
	 *     ので「ここへ渡る経路が存在しない」と判断して受けないことにしていた。ところが
	 *     @cast@ を gu に足した時点で **入口ができた** ⇒ 受ける必要が出た。
	 *     (cast は型の正規の入口で、gg / ch / mf がまったく同じ理由で MESH を受けている。)
	 *   ★ パーサは common/exact_wire.h なので **CGAL をリンクしない** (gg / mf と同じ実体)。
	 *   ⚠ 落とすのは厳密 → double の **不可逆変換**。cgal は据置なので、厳密が要る問いは
	 *     cgal 側で訊くこと (ここは「double 系の道具へ持ち込む」ための降格)。 */
	if ( ::memcmp(meta, "MESH", 4) == 0 ) {
		sPtr<guMesh> m = thNEW(guMesh,());
		m->set_mesh_exact_input();
		return sPtr<guGeom>::d_cast(m);
	}
	if ( ::memcmp(meta, "PLY2", 4) == 0 ) {
		sPtr<guPoly> p = thNEW(guPoly,());
		p->set_cross_exact_input();
		return sPtr<guGeom>::d_cast(p);
	}
	/* ⚠ NEFB (nef の境界形式) は受けない — gg / ch も受けていない。必要になったら
	 *   nef の橋 (nef_cg / nef_mf) と同じ形で足すべきで、ここに書く話ではない。 */
	return sPtr<guGeom>();
}

/* ================= 3D (MFM3) ==================================================== */

sPtr<stdString>
guMesh::get_str()
{
	char buf[64];
	::snprintf(buf, sizeof buf, "<mesh:geomutils tris=%d>", nt());
	return thNEW(stdString,(buf));
}

void
guMesh::encode(guChunkSink &sink)
{
	const uint32_t nvv = (uint32_t)nv(), ntt = (uint32_t)nt();
	put_u32(sink, nvv);
	put_u32(sink, ntt);
	for ( size_t k = 0 ; k < coords_.size() ; ++k ) put_f64(sink, coords_[k]);
	for ( size_t k = 0 ; k < tris_.size()   ; ++k ) put_u32(sink, tris_[k]);
	/* 色節。⚠⚠ 中身は見ないが **落とさない**。
	 *   ★ 2026-09-17 に陽性対照で実地確認した — ここを put_u32(0) に潰すと、往復後に
	 *     OFF へ書いた面色が **赤 0 件 / 青 0 件** になった (落ちても例外は出ないので、
	 *     検定が無ければ誰も気づかない種類の欠落)。 */
	if ( color_.size() == (size_t)nvv && nvv > 0 ) {
		put_u32(sink, 1u);
		for ( size_t k = 0 ; k < color_.size() ; ++k ) put_u32(sink, color_[k]);
	} else {
		put_u32(sink, 0u);
	}
	/* merge ベクタ。⚠⚠ これが無いと manifold 側が座標比較で復元できず
	 *   **非多様体になって volume=0 / valid=0** になる。
	 *   ★ 2026-09-17 に **陽性対照で実地確認した** — ここを put_u32(0) に潰すと
	 *     色つき union の往復 (mf→gu→mf) が volume 1500 → **0** ・ valid 1 → **0** に落ちた。
	 *   ⚠ 検定を作るときの注意: cgal が priority 20 で color / union / combine を取るので、
	 *     **manifold を priority 99 で名指ししないと merge が 0 のまま**で、
	 *     壊しても何も起きない (= 何も検定していない検定になる)。実際 1 度そう踏んだ。 */
	const uint32_t nmg = (uint32_t)(merge_.size() / 2);
	put_u32(sink, nmg);
	for ( size_t k = 0 ; k < (size_t)nmg * 2 ; ++k ) put_u32(sink, merge_[k]);
}

/* ---- cg→gu 降格読み: cgal の "MESH" (厳密有理数文字列) を double 化して読む。
 *   framing: [u32 nv][u32 nf] / 頂点×nv (x,y,z = 各 [u32 len][len byte の "p/q" or 整数]) /
 *            面×nf ([u32 nidx][u32 idx]…) / 色 section。
 *   ★ 面は n-gon がありうる ⇒ **ファン三角化** (encode が三角形しか書けないので、ここで
 *     三角形へ落とす。平面なら体積は変わらない)。頂点 index は cgal 側で共有済み = 統合不要。
 *   ⚠ ggMesh::decode_mesh_exact と **同じ実体**。片方だけ直さないこと。 */
void
guMesh::decode_mesh_exact(guChunkSource &src)
{
	coords_.clear(); tris_.clear(); color_.clear(); merge_.clear();
	const uint32_t nv = srava_exact::get_u32(src);
	const uint32_t nf = srava_exact::get_u32(src);
	coords_.resize((size_t)nv * 3);
	for ( uint32_t i = 0 ; i < nv ; ++i ) {
		coords_[3*(size_t)i]     = srava_exact::get_rational_d(src);
		coords_[3*(size_t)i + 1] = srava_exact::get_rational_d(src);
		coords_[3*(size_t)i + 2] = srava_exact::get_rational_d(src);
	}
	std::vector<uint32_t> idx;
	for ( uint32_t f = 0 ; f < nf ; ++f ) {
		const uint32_t nidx = srava_exact::get_u32(src);
		idx.resize((size_t)nidx);
		for ( uint32_t j = 0 ; j < nidx ; ++j ) idx[j] = srava_exact::get_u32(src);
		for ( uint32_t j = 1 ; j + 1 < nidx ; ++j ) {   /* ファン三角化 */
			tris_.push_back(idx[0]); tris_.push_back(idx[j]); tris_.push_back(idx[j+1]);
		}
	}
	/* 色 section は読まない (必要バイトのみ pull 済み・reader が残りを閉じる)。 */
}

void
guMesh::decode(guChunkSource &src)
{
	if ( meshExactInput_ ) { decode_mesh_exact(src); return; }   /* ★ cgal "MESH" → double */
	const uint32_t nvv = get_u32(src);
	const uint32_t ntt = get_u32(src);
	/* ⚠ #3504 と同じ形の事故を作らないため、u32 のまま扱って size_t へ広げる
	 *   ((int)n が負になって黙って 0 バイトになる経路を持ち込まない)。 */
	coords_.resize((size_t)nvv * 3);
	for ( size_t k = 0 ; k < coords_.size() ; ++k ) coords_[k] = get_f64(src);
	tris_.resize((size_t)ntt * 3);
	for ( size_t k = 0 ; k < tris_.size() ; ++k )   tris_[k] = get_u32(src);
	color_.clear();
	merge_.clear();
	if ( ! src.more() ) return;   /* 旧キャッシュ (色節が無い) — 後方互換 */
	const uint32_t hasColor = get_u32(src);
	if ( hasColor ) {
		color_.resize((size_t)nvv);
		for ( size_t k = 0 ; k < color_.size() ; ++k ) color_[k] = get_u32(src);
	}
	if ( ! src.more() ) return;
	const uint32_t nmg = get_u32(src);
	merge_.resize((size_t)nmg * 2);
	for ( size_t k = 0 ; k < merge_.size() ; ++k )  merge_[k] = get_u32(src);
}

/* ================= 2D (MFC2) ==================================================== */

sPtr<stdString>
guPoly::get_str()
{
	char buf[80];
	::snprintf(buf, sizeof buf, "<%s:geomutils rings=%d>",
	           placed_ ? "face" : "cross", nrings());
	return thNEW(stdString,(buf));
}

void
guPoly::encode(guChunkSink &sink)
{
	put_u32(sink, (uint32_t)ringLen_.size());
	size_t off = 0;
	for ( size_t r = 0 ; r < ringLen_.size() ; ++r ) {
		const int n = ( ringLen_[r] > 0 ) ? ringLen_[r] : 0;
		put_u32(sink, (uint32_t)n);
		for ( int i = 0 ; i < n ; ++i ) {
			put_f64(sink, xy_[2*(off + (size_t)i)]);
			put_f64(sink, xy_[2*(off + (size_t)i) + 1]);
		}
		off += (size_t)n;
	}
	/* ★★ #3533: 枠の節は **face3d のときだけ**書く。⚠ 条件を「枠が既定でない」に
	 *   すると rotate(rect,"z",90) が枠既定のまま型だけ face3d になる場合を落とす
	 *   (mfCross::encode / cgMesh2D::encode と **同じ条件**にしてある)。 */
	if ( placed_ || ! frame_is_default() ) {
		put_u32(sink, GU_FRAME_MARK);
		for ( int i = 0 ; i < 3 ; ++i ) put_f64(sink, fo_[i]);
		for ( int i = 0 ; i < 3 ; ++i ) put_f64(sink, fu_[i]);
		for ( int i = 0 ; i < 3 ; ++i ) put_f64(sink, fv_[i]);
	}
}

/* ---- cg→gu 降格読み: cgal の "PLY2" (厳密有理数リング) を double 化して読む。
 *   framing: [u32 nregions] / region×([u32 npts] 外周 (x,y=有理数文字列)) /
 *            [u32 nholes] / 穴 ring 群。末尾のガイド層は読まない。
 *   ★ CGAL の Pwh は **外周 CCW・穴 CW** なので、リングの列としてそのまま並べれば
 *     ringprops.h の「符号つき面積が正 = 塊」がそのまま成り立つ (向きを触らない)。
 *   ⚠ mfCross::decode_cross_exact と同じ framing。片方だけ直さないこと。 */
void
guPoly::decode_cross_exact(guChunkSource &src)
{
	xy_.clear(); ringLen_.clear();
	const uint32_t nreg = srava_exact::get_u32(src);
	for ( uint32_t r = 0 ; r < nreg ; ++r ) {
		const uint32_t nouter = srava_exact::get_u32(src);
		ringLen_.push_back((int)nouter);
		for ( uint32_t i = 0 ; i < nouter ; ++i ) {
			xy_.push_back(srava_exact::get_rational_d(src));
			xy_.push_back(srava_exact::get_rational_d(src));
		}
		const uint32_t nholes = srava_exact::get_u32(src);
		for ( uint32_t hI = 0 ; hI < nholes ; ++hI ) {
			const uint32_t nh = srava_exact::get_u32(src);
			ringLen_.push_back((int)nh);
			for ( uint32_t i = 0 ; i < nh ; ++i ) {
				xy_.push_back(srava_exact::get_rational_d(src));
				xy_.push_back(srava_exact::get_rational_d(src));
			}
		}
	}
	/* ★★★ #3527 段 6: **PLY2 も枠の節を持つ**。cgMesh2D::encode はリング列の後ろに同じ
	 *   "MARF" 節を書く (cgMesh2D.cpp:240) ので、MFC2 とまったく同じ形で読める。
	 * ⚠⚠ 段 2 でここに「PLY2 は z=0 平面の 2D (枠は cgal 側が別に持つ)」と書いて
	 *   placed_ = 0 を固定していた。**前提が誤り**で、cast("gu-face3d", <cg-face3d>) が
	 *   *宣言では face3d を返すと言いながら実際には gu-cross2d を返し*、
	 *   **空間に置いた 2D が黙って z=0 へ戻っていた** (2026-09-17 に段 6 の cast 監査で発見)。
	 *   ⇒ #3533 規約②「降格は cast のみ」を、cast 自身が *黙って* 破っていた形。
	 *   ★ cgMesh2D の同じ箇所 (cgMesh2D.cpp:330) には「読まないと空間に置いた 2D が黙って
	 *     z=0 に戻る」と **既に書いてあった**。向こうを読んでいれば段 2 で気づけた。 */
	placed_ = 0;
	if ( ! src.more() ) return;
	/* ★★ PLY2 は regions のあとに **ガイド層 (開ポリライン群)** の節を挟み、その後ろに枠を置く
	 *   (cgMesh2D.cpp:217 の順序。manifold は 2D の節が 1 つなので順序の問題が無かった)。
	 *   ⚠ ここを読み飛ばさないと、枠マーカのつもりで **ガイドの本数**を読んでしまう。
	 * ⚠⚠ ガイドは **開いた折れ線**で、RingView には表現が無い。黙って捨てると
	 *   nverts(cast(...)) が cgal と食い違う (cgal の op_verts はガイドの点も歩く)。
	 *   ⇒ **在ったら断る**。黙って落とさない (#3527 の線引き)。 */
	const uint32_t nguide = get_u32(src);
	if ( nguide != 0 ) {
		set_decode_err("this 2D value carries guide polylines (from line(...)), which this "
		               "representation cannot hold; drop them before converting");
		return;
	}
	if ( ! src.more() ) return;   /* ガイドの節で終わり = 枠は既定 (cross2d) */
	/* ⚠ 枠は **生の f64**。PLY2 の座標は有理数文字列だが、MARF 節だけは cgal 側も
	 *   cgaMeshCodec::get_f64 で書き読みしている ⇒ ここも生で読む (混ぜると壊れる)。 */
	const uint32_t mark = get_u32(src);
	if ( mark != GU_FRAME_MARK ) {
		set_decode_err("the exact 2D cache has a trailing section that is not a frame marker");
		return;
	}
	for ( int i = 0 ; i < 3 ; ++i ) fo_[i] = get_f64(src);
	for ( int i = 0 ; i < 3 ; ++i ) fu_[i] = get_f64(src);
	for ( int i = 0 ; i < 3 ; ++i ) fv_[i] = get_f64(src);
	placed_ = 1;
}

void
guPoly::decode(guChunkSource &src)
{
	if ( crossExactInput_ ) { decode_cross_exact(src); return; }   /* ★ cgal "PLY2" → double */
	const uint32_t nr = get_u32(src);
	ringLen_.clear();
	xy_.clear();
	ringLen_.reserve((size_t)nr);
	for ( uint32_t r = 0 ; r < nr ; ++r ) {
		const uint32_t n = get_u32(src);
		ringLen_.push_back((int)n);
		const size_t base = xy_.size();
		xy_.resize(base + (size_t)n * 2);
		for ( uint32_t i = 0 ; i < n ; ++i ) {
			xy_[base + 2*(size_t)i]     = get_f64(src);
			xy_[base + 2*(size_t)i + 1] = get_f64(src);
		}
	}
	placed_ = 0;
	if ( ! src.more() ) return;   /* 枠の節が無い = z=0 平面 (cross2d) */
	const uint32_t mark = get_u32(src);
	if ( mark != GU_FRAME_MARK ) {
		/* ⚠ 黙って進まない。長さが合っていれば別の形に読めてしまうため。 */
		set_decode_err("the 2D cache has a trailing section that is not a frame marker");
		return;
	}
	for ( int i = 0 ; i < 3 ; ++i ) fo_[i] = get_f64(src);
	for ( int i = 0 ; i < 3 ; ++i ) fu_[i] = get_f64(src);
	for ( int i = 0 ; i < 3 ; ++i ) fv_[i] = get_f64(src);
	placed_ = 1;
}

/* ================= 素性 (#3527 段 3) ============================================
 * ★ 中身は全部 common/meshprops.h (3D) と common/ringprops.h (2D)。ここは *配線*だけで、
 *   計算は 1 行も書かない — それがこのモジュールの存在理由 (実装を 1 本に寄せる)。
 */

int
guMesh::op_bbox(double mn[3], double mx[3])
{
	srava_mesh::bbox(view(), mn, mx);
	return 3;
}

int
guMesh::op_centroid(double c[3])
{
	srava_mesh::centroid(view(), c);
	return 3;
}

int
guMesh::op_topology(int *nshells, int *nparts, int *genus)
{
	const srava_mesh::Topology t = srava_mesh::topology(view());
	if ( nshells ) *nshells = t.nshells;
	if ( nparts  ) *nparts  = t.nparts;
	if ( genus   ) *genus   = t.genus;
	return t.closed;
}

/* ---- 2D ----
 * ⚠⚠ #3533: bbox / centroid は **cross2d なら局所 2 成分・face3d なら world 3 成分**。
 *   face3d は z=0 に居ないので 2 成分では答えられない (mfCross / cgMesh2D と同じ約束)。
 *   ⇒ 片方だけ変えないこと。 */
int
guPoly::op_bbox(double mn[3], double mx[3])
{
	double lo[2] = {0,0}, hi[2] = {0,0};
	const int any = srava_poly::bbox(view(), lo, hi);
	if ( ! placed_ ) {
		mn[0] = lo[0]; mn[1] = lo[1]; mn[2] = 0;
		mx[0] = hi[0]; mx[1] = hi[1]; mx[2] = 0;
		return 2;
	}
	/* ★ world の AABB は **枠の 4 隅を写してから**取る (局所の箱をそのまま world と
	 *   名乗ると、傾いた平面で嘘になる)。 */
	if ( ! any ) {
		for ( int i = 0 ; i < 3 ; ++i ) mn[i] = mx[i] = 0;
		return 3;
	}
	const double cx[4] = { lo[0], hi[0], lo[0], hi[0] };
	const double cy[4] = { lo[1], lo[1], hi[1], hi[1] };
	for ( int k = 0 ; k < 4 ; ++k ) {
		double w[3];
		to_world(cx[k], cy[k], w);
		if ( k == 0 ) { for ( int i = 0 ; i < 3 ; ++i ) mn[i] = mx[i] = w[i]; continue; }
		for ( int i = 0 ; i < 3 ; ++i ) {
			if ( w[i] < mn[i] ) mn[i] = w[i];
			if ( w[i] > mx[i] ) mx[i] = w[i];
		}
	}
	return 3;
}

int
guPoly::op_centroid(double c[3])
{
	double p[2] = {0,0};
	srava_poly::centroid(view(), p);
	if ( ! placed_ ) { c[0] = p[0]; c[1] = p[1]; c[2] = 0; return 2; }
	to_world(p[0], p[1], c);
	return 3;
}

int
guPoly::op_topology(int *nshells, int *nparts, int *genus)
{
	/* ⚠ 2D に境界シェルと種数は無い (曲面の量・#3525)。0 のままにし、op 側が
	 *   そもそも 2D の行を宣言しないことで「訊けない」を表す。 */
	if ( nshells ) *nshells = 0;
	if ( genus   ) *genus   = 0;
	if ( nparts  ) *nparts  = srava_poly::topology(view()).nparts;
	return 0;
}

/* ================= 片の取り出し (#3527 段 4) ====================================
 * ★★★ 段 4 が本丸だったのは **入れ子** — 「どの空洞がどの塊のものか」。これが無いと
 *   塊を *取り出す* ことができない (数えるだけなら符号で足りる)。実体は
 *   meshprops.h の nesting() (3D・巻き数) と ringprops.h の nesting() (2D・点の内外)。
 * ★ ここに在るのは **値への配線だけ** — 幾何は全部ヘッダ側。
 */

/* ---- 殻番号の集合 → 新しい guMesh (part / shell の共通部) ----------------------
 * ⚠⚠ **共通化しておくこと**。cgal が cg_extract_faces を 1 本にしてあるのと同じ理由で、
 *   part 側と shell 側に写すと片方だけ直したときに黙ってずれる。
 * ★ 色と merge ベクタを **同じ並びで写す**。捨てると往復で情報が落ちる。 */
static sPtr<guMesh>
gu_mesh_from_shells(guMesh &src, const srava_mesh::Shells &s, const std::vector<int> &shellIds)
{
	/* ★ 殻 → 面 の索引 (CSR) を使う。全面 × 殻数 の走査にすると殻が多い値で効いてくる。
	 * ⚠ 面の順は **元のまま**にする — 索引が走査順で決まる以上、ここで並べ替えると
	 *   part(m,i) と shell(m,i) で面の順が食い違う。 */
	std::vector<int> want;
	for ( size_t k = 0 ; k < shellIds.size() ; ++k ) {
		const int c = shellIds[k];
		if ( c < 0 || c + 1 >= (int)s.faceStart.size() ) continue;
		for ( int j = s.faceStart[(size_t)c] ; j < s.faceStart[(size_t)c + 1] ; ++j )
			want.push_back(s.faceIdx[(size_t)j]);
	}
	std::sort(want.begin(), want.end());
	if ( want.empty() ) return sPtr<guMesh>();

	sPtr<guMesh> out = thNEW(guMesh,());
	std::vector<uint32_t> keep;
	srava_mesh::extract_faces(src.view(), want, out->coords(), out->tris(), keep);

	const int onv = src.nv();
	if ( src.color().size() == (size_t)onv && onv > 0 ) {
		out->color().resize(keep.size());
		for ( size_t j = 0 ; j < keep.size() ; ++j ) out->color()[j] = src.color()[keep[j]];
	}
	if ( ! src.merge().empty() && onv > 0 ) {
		/* 元番号 → 新番号。**両端が残った対だけ**を写す (片端が消えた対は意味を失う)。 */
		std::vector<int> nix((size_t)onv, -1);
		for ( size_t j = 0 ; j < keep.size() ; ++j )
			if ( (int)keep[j] < onv ) nix[keep[j]] = (int)j;
		for ( size_t k = 0 ; k + 1 < src.merge().size() ; k += 2 ) {
			const uint32_t a = src.merge()[k], b = src.merge()[k+1];
			if ( (int)a >= onv || (int)b >= onv ) continue;
			if ( nix[a] < 0 || nix[b] < 0 ) continue;
			out->merge().push_back((uint32_t)nix[a]);
			out->merge().push_back((uint32_t)nix[b]);
		}
	}
	return out;
}

sPtr<guGeom>
guMesh::op_part(int i, const char **why)
{
	*why = 0;
	if ( i < 0 ) { *why = "the index must be >= 0"; return sPtr<guGeom>(); }
	srava_mesh::Shells s;
	srava_mesh::shells(view(), s);
	std::vector<int> ps;
	if ( ! srava_mesh::part_shells(view(), s, i, ps) ) {
		*why = "index out of range";
		return sPtr<guGeom>();
	}
	sPtr<guMesh> out = gu_mesh_from_shells(*this, s, ps);
	if ( ! out.is_notNull() ) { *why = "the part is empty"; return sPtr<guGeom>(); }
	return sPtr<guGeom>::d_cast(out);
}

sPtr<guGeom>
guMesh::op_part_at(const double p[3], const char **why)
{
	*why = 0;
	srava_mesh::Shells s;
	srava_mesh::shells(view(), s);
	const int i = srava_mesh::part_at(view(), s, p);
	if ( i == -1 ) {
		/* ★ 空洞の中・立体の外はどちらもここ。**黙って近い塊を返さない** —
		 *   「そこに材料は無い」が正しい答えで、近いものを返すと嘘になる。 */
		*why = "no solid of this mesh is at that point (the point is outside the mesh, or inside "
		       "a cavity, where there is no material); use shell_at(m,p) to take the nearest "
		       "boundary shell instead";
		return sPtr<guGeom>();
	}
	if ( i == -2 ) {
		*why = "more than one solid of this mesh contains that point, so its parts overlap "
		       "(the mesh is not valid); check valid(m) first";
		return sPtr<guGeom>();
	}
	std::vector<int> ps;
	if ( ! srava_mesh::part_shells(view(), s, i, ps) ) { *why = "index out of range"; return sPtr<guGeom>(); }
	sPtr<guMesh> out = gu_mesh_from_shells(*this, s, ps);
	if ( ! out.is_notNull() ) { *why = "the part is empty"; return sPtr<guGeom>(); }
	return sPtr<guGeom>::d_cast(out);
}

sPtr<guGeom>
guMesh::op_shell(int i, const char **why)
{
	*why = 0;
	if ( i < 0 ) { *why = "the index must be >= 0"; return sPtr<guGeom>(); }
	srava_mesh::Shells s;
	srava_mesh::shells(view(), s);
	if ( s.n <= 0 || i >= s.n ) { *why = "index out of range"; return sPtr<guGeom>(); }
	std::vector<int> one(1, i);
	sPtr<guMesh> out = gu_mesh_from_shells(*this, s, one);
	if ( ! out.is_notNull() ) { *why = "the shell is empty"; return sPtr<guGeom>(); }
	return sPtr<guGeom>::d_cast(out);
}

sPtr<guGeom>
guMesh::op_shell_at(const double p[3], const char **why)
{
	*why = 0;
	srava_mesh::Shells s;
	srava_mesh::shells(view(), s);
	const int c = srava_mesh::shell_at(view(), s, p);
	if ( c == -1 ) { *why = "the mesh has no faces"; return sPtr<guGeom>(); }
	if ( c == -2 ) {
		/* ⚠ 文言は cgal と **一字一句そろえる** — 同じ約束を見ているテスト
		 *   (test/srava_shell.sh ⑦) が両方を同じ grep で見るため (#3527 段 3 の②)。 */
		*why = "more than one shell is equally near that point, so which one is meant is ambiguous "
		       "(move the point off the symmetry, or name the shell by index with shell(m,i))";
		return sPtr<guGeom>();
	}
	std::vector<int> one(1, c);
	sPtr<guMesh> out = gu_mesh_from_shells(*this, s, one);
	if ( ! out.is_notNull() ) { *why = "the shell is empty"; return sPtr<guGeom>(); }
	return sPtr<guGeom>::d_cast(out);
}

/* ---- 2D ----------------------------------------------------------------------- */

/* リング番号の集合 → 新しい guPoly。★ 枠は **そのまま持ち回る** (片を取り出しても
 * 置き場所は変わらない — 幾何を動かす操作ではない)。 */
static sPtr<guPoly>
gu_poly_from_rings(guPoly &src, const std::vector<int> &rings)
{
	const int nr = src.nrings();
	std::vector<int> start((size_t)(nr > 0 ? nr : 0), 0);
	int off = 0;
	for ( int r = 0 ; r < nr ; ++r ) {
		start[(size_t)r] = off;
		off += ( src.ringLen()[(size_t)r] > 0 ) ? src.ringLen()[(size_t)r] : 0;
	}
	sPtr<guPoly> out = thNEW(guPoly,());
	for ( size_t k = 0 ; k < rings.size() ; ++k ) {
		const int r = rings[k];
		if ( r < 0 || r >= nr ) continue;
		const int n = src.ringLen()[(size_t)r];
		if ( n <= 0 ) continue;
		out->ringLen().push_back(n);
		for ( int j = 0 ; j < n ; ++j ) {
			out->xy().push_back(src.xy()[2*(size_t)(start[(size_t)r] + j)]);
			out->xy().push_back(src.xy()[2*(size_t)(start[(size_t)r] + j) + 1]);
		}
	}
	if ( out->ringLen().empty() ) return sPtr<guPoly>();
	out->set_frame(src.frame_o(), src.frame_u(), src.frame_v());
	out->set_placed(src.is_placed());
	return out;
}

/* ★ world → 枠の中の (x,y)。枠は **正規直交** (affine.h の plane_frame がそう作る) なので
 *   内積 3 本で足りる。⚠ 面外成分が 0 でなければ **断る** — 黙って射影すると
 *   「平面の外の点で part_at を訊いたのに答えが返る」= 嘘になる。
 *   ⇒ 射影したい人は project_flatten / transform を明示的に通すこと (#3534 と同じ線)。
 * ★ cross2d (枠は既定) では、この検査はそのまま「z が 0 か」になる。 */
/* ★★ #3553: 点との距離 (3D)。実体は meshprops.h に 1 本 (総当たり・AABB は持たない)。 */
int
guMesh::op_distance_at(const double p[3], double *out, const char **why)
{
	*why = 0;
	const double d = srava_mesh::distance_at(view(), p);
	if ( d < 0.0 ) { *why = "this mesh has no triangles, so there is nothing to measure to"; return 0; }
	if ( out ) *out = d;
	return 1;
}

/* ★★ #3553: 点との距離 (2D)。**平面領域なので閉じた形で合成できる** —
 *   最近点は「射影が材料の上なら射影そのもの / 外なら境界上」なので
 *       距離 = sqrt(面外成分² + 枠の中での距離²)
 *   ⇒ 射影で高さを捨てるのではなく、**答えに含める**。 */
int
guPoly::op_distance_at(const double p[3], double *out, const char **why)
{
	*why = 0;
	double q[2], off = 0.0;
	to_local_full(p, q, &off);
	const double d2 = srava_poly::distance_in_plane(view(), q);
	if ( d2 < 0.0 ) { *why = "this 2D region is empty, so there is nothing to measure to"; return 0; }
	if ( out ) *out = std::sqrt(off * off + d2 * d2);
	return 1;
}

/* ================= #3579: 点群を 3 つに分ける ===================================
 * ★★ 実体は共通ヘッダの @classify_point@ (3D=meshprops.h の巻き数 / 2D=ringprops.h)。
 *   ここが足すのは **尺度の決定と枝刈りだけ** — 判定そのものは書き直さない (#3575 落とし穴 D)。
 *
 * ★ 許容差は **相対** (GU_CLASSIFY_TOL_REL x 模型の尺度)。座標の大きさに比例して丸めが
 *   乗るので、絶対値で見ると大きな模型で境界が拾えなくなる (to_local と同じ理屈)。
 *   ★ 値は shell_at の同距離判定と **同じ 1e-12** に揃えてある (geomutils の double の粒度)。
 *   ⚠ 他モジュールは別の厚みでよい (openvdb = dx の帯 / occt = Precision::Confusion)。
 *     s[0] の厚みがモジュールごとに違うのは欠陥ではない (#3491 が帯を切り離したのと同じ)。
 *     ⚠ 逆に s[1] / s[2] は **どのモジュールでも一致するべき** = #3581 の一致検定。
 */
static const double GU_CLASSIFY_TOL_REL = 1e-12;

/* 尺度 = bbox の対角長 (0 なら 1)。⚠ 原点の取り方に依らない量を選ぶこと。 */
static double
gu_classify_scale(const double mn[3], const double mx[3], int ndim)
{
	double s = 0.0;
	for ( int k = 0 ; k < ndim ; ++k ) { const double e = mx[k] - mn[k]; s += e * e; }
	s = std::sqrt(s);
	return ( s > 0.0 ) ? s : 1.0;
}

/* ---- 3D: 巻き数 (meshprops.h) ---------------------------------------------------
 * ★ @dim@ は 2 でも 3 でもよい。2 のときは **z=0 とみなす** (2D 点群 x 立体 = 「z=0 平面上の
 *   点が立体に入っているか」)。⚠ 暗黙の昇格なので docs に明記すること (#3579)。
 * ★★ **bbox で先に枝刈り**する — 巻き数は 1 点 O(面数) なので、10^5 点を素朴に回すと
 *   10^5 x 面数 になる。bbox の外 (許容差ぶん広げた外) は **走査せずに外側**と決まる。
 *   ⇒ 点群が模型の外に散っているときに効く (rand の一様点はまさにそれ)。 */
int
guMesh::op_classify_points(const double *pts, int npt, int dim,
                           signed char *cls, const char **why)
{
	*why = 0;
	if ( dim != 2 && dim != 3 ) { *why = "the point cloud must be 2D or 3D"; return 0; }
	if ( nt() <= 0 ) { *why = "this mesh has no triangles, so it encloses nothing"; return 0; }
	const srava_mesh::TriView m = view();
	double mn[3], mx[3];
	if ( ! srava_mesh::bbox(m, mn, mx) ) { *why = "this mesh has no vertices"; return 0; }
	const double tol = GU_CLASSIFY_TOL_REL * gu_classify_scale(mn, mx, 3);
	for ( int i = 0 ; i < npt ; ++i ) {
		double p[3];
		p[0] = pts[(size_t)i*dim + 0];
		p[1] = pts[(size_t)i*dim + 1];
		p[2] = ( dim == 3 ) ? pts[(size_t)i*dim + 2] : 0.0;   /* ★ 2D は z=0 */
		int outside = 0;
		for ( int k = 0 ; k < 3 ; ++k )
			if ( p[k] < mn[k] - tol || p[k] > mx[k] + tol ) { outside = 1; break; }
		cls[i] = outside ? (signed char)1
		                 : (signed char)srava_mesh::classify_point(m, p, tol);
	}
	return 1;
}

/* ---- 2D: point-in-polygon (ringprops.h) -----------------------------------------
 * ⚠ 3D の点群 x 2D の領域は **受けない** (#3575 の「X <= Y」)。断る側に倒す —
 *   平面へ射影して答えると「面外の高さを黙って捨てた答え」になる (#3534 と同じ線引き)。
 *   ★ 通常経路では sig が先に弾くので、ここは *sig を書き換えた人* への保険。 */
int
guPoly::op_classify_points(const double *pts, int npt, int dim,
                           signed char *cls, const char **why)
{
	*why = 0;
	if ( dim != 2 ) {
		*why = "a 3D point cloud cannot be split by a 2D region; "
		       "the region must have at least as many dimensions as the points";
		return 0;
	}
	if ( nrings() <= 0 ) { *why = "this 2D region is empty, so it encloses nothing"; return 0; }
	const srava_poly::RingView v = view();
	double mn[2], mx[2];
	if ( ! srava_poly::bbox(v, mn, mx) ) { *why = "this 2D region has no points"; return 0; }
	const double mn3[3] = { mn[0], mn[1], 0.0 }, mx3[3] = { mx[0], mx[1], 0.0 };
	const double tol = GU_CLASSIFY_TOL_REL * gu_classify_scale(mn3, mx3, 2);
	for ( int i = 0 ; i < npt ; ++i ) {
		const double p[2] = { pts[(size_t)i*2 + 0], pts[(size_t)i*2 + 1] };
		int outside = 0;
		for ( int k = 0 ; k < 2 ; ++k )
			if ( p[k] < mn[k] - tol || p[k] > mx[k] + tol ) { outside = 1; break; }
		cls[i] = outside ? (signed char)1
		                 : (signed char)srava_poly::classify_point(v, p, tol);
	}
	return 1;
}

void
guPoly::to_local_full(const double w[3], double xy[2], double *offPlane) const
{
	const double *o = frame_o(), *u = frame_u(), *v = frame_v();
	const double d[3] = { w[0]-o[0], w[1]-o[1], w[2]-o[2] };
	/* 枠は正規直交なので内積 3 本 (法線は u x v)。 */
	const double n[3] = { u[1]*v[2] - u[2]*v[1],
	                      u[2]*v[0] - u[0]*v[2],
	                      u[0]*v[1] - u[1]*v[0] };
	xy[0] = d[0]*u[0] + d[1]*u[1] + d[2]*u[2];
	xy[1] = d[0]*v[0] + d[1]*v[1] + d[2]*v[2];
	if ( offPlane ) *offPlane = d[0]*n[0] + d[1]*n[1] + d[2]*n[2];
}

int
guPoly::to_local(const double w[3], double xy[2]) const
{
	const double *o = frame_o(), *u = frame_u(), *v = frame_v();
	const double d[3] = { w[0]-o[0], w[1]-o[1], w[2]-o[2] };
	const double n[3] = { u[1]*v[2] - u[2]*v[1],
	                      u[2]*v[0] - u[0]*v[2],
	                      u[0]*v[1] - u[1]*v[0] };
	const double out = d[0]*n[0] + d[1]*n[1] + d[2]*n[2];
	const double len = std::sqrt(d[0]*d[0] + d[1]*d[1] + d[2]*d[2]);
	/* ⚠ 相対許容差。座標の大きさに比例して丸めが乗るので絶対値で見ると大きな模型で落ちる。 */
	if ( std::fabs(out) > 1e-9 * ( len > 1.0 ? len : 1.0 ) ) return 0;
	xy[0] = d[0]*u[0] + d[1]*u[1] + d[2]*u[2];
	xy[1] = d[0]*v[0] + d[1]*v[1] + d[2]*v[2];
	return 1;
}

sPtr<guGeom>
guPoly::op_part(int i, const char **why)
{
	*why = 0;
	if ( i < 0 ) { *why = "the index must be >= 0"; return sPtr<guGeom>(); }
	std::vector<int> rings;
	if ( ! srava_poly::part_rings(view(), i, rings) ) {
		*why = "index out of range";
		return sPtr<guGeom>();
	}
	sPtr<guPoly> out = gu_poly_from_rings(*this, rings);
	if ( ! out.is_notNull() ) { *why = "the part is empty"; return sPtr<guGeom>(); }
	return sPtr<guGeom>::d_cast(out);
}

sPtr<guGeom>
guPoly::op_part_at(const double p[3], const char **why)
{
	*why = 0;
	double q[2];
	if ( ! to_local(p, q) ) {
		*why = "that point is not on the plane of this 2D region, so it cannot pick a piece of it "
		       "(part_at never projects the point — give a point on the plane, or flatten the "
		       "region with project_flatten(v) first)";
		return sPtr<guGeom>();
	}
	const int i = srava_poly::part_at(view(), q);
	if ( i == -1 ) {
		*why = "no piece of this 2D region is at that point (the point is outside the region, or "
		       "inside a hole, where there is no material)";
		return sPtr<guGeom>();
	}
	if ( i == -2 ) {
		*why = "more than one piece of this 2D region contains that point, so its pieces overlap "
		       "(the region is not valid); check valid(v) first";
		return sPtr<guGeom>();
	}
	std::vector<int> rings;
	if ( ! srava_poly::part_rings(view(), i, rings) ) { *why = "index out of range"; return sPtr<guGeom>(); }
	sPtr<guPoly> out = gu_poly_from_rings(*this, rings);
	if ( ! out.is_notNull() ) { *why = "the part is empty"; return sPtr<guGeom>(); }
	return sPtr<guGeom>::d_cast(out);
}

/* ⚠ 2D に殻は無い。★ 通常経路ではここへ来ない — sig が 3D の行しか持たないので
 *   ルータが先に "no module can execute op 'shell' on input types (gu-cross2d)" で弾く。
 *   ⇒ ここは *sig を書き換えた人* への保険 (cgaShell と同じ構え)。 */
sPtr<guGeom>
guPoly::op_shell(int, const char **why)
{
	*why = "a 2D region has no shells (a shell is the boundary surface of a solid); "
	       "use part(v,i) to take a piece of a 2D region";
	return sPtr<guGeom>();
}

sPtr<guGeom>
guPoly::op_shell_at(const double *, const char **why)
{
	*why = "a 2D region has no shells (a shell is the boundary surface of a solid); "
	       "use part_at(v,p) to take the piece at a point";
	return sPtr<guGeom>();
}

/* ================= 頂点を読む (#3527 段 5) ======================================
 * ★ 3 つ組の「取り出す」(vert) と「まとめて」(verts)、それに面の **頂点番号** (face_verts)。
 * ⚠⚠ op_vert と op_verts は **同じ列を同じ順**で歩く。⇒ verts(m) の i 番目 == vert(m,i)。
 *   別々に書くと黙ってずれるので、2D は歩き方を 1 本 (gu_poly_walk) に括ってある。
 */

int
guMesh::op_vert(int i, double out[3])
{
	if ( i < 0 || i >= nv() ) return 0;
	const double *p = &coords_[3*(size_t)i];
	out[0] = p[0]; out[1] = p[1]; out[2] = p[2];
	return 3;
}

int
guMesh::op_verts(std::vector<double> &out)
{
	out = coords_;   /* ★ 並びは coords_ そのもの = op_vert の索引と同じ列 */
	return 3;
}

/* ★ 面 i の **頂点番号** [i0,i1,i2]。座標ではない — 連結関係が要る場面のための口で、
 *   座標が要るなら vert(m, face_verts(m,i)[k]) と繋ぐ (番号は同じ列を指す)。
 * ⚠ mesh 系の「面」は **三角形 1 枚**。occt の face (トリム面) とは桁が違う量だが、
 *   *その違いこそ表現の要点* なのであえて同じ語を使う (#3527 の⑤・ocaNfaces と同じ判断)。 */
int
guMesh::op_face_verts(int i, int out[3])
{
	if ( i < 0 || i >= nt() ) return 0;
	for ( int k = 0 ; k < 3 ; ++k ) out[k] = (int)tris_[3*(size_t)i + k];
	return 3;
}

/* ---- 2D: リング順 → 点順 に歩く (op_nverts / op_vert / op_verts で **同じ歩き方**) ----
 * 返り: 成分数 (cross2d=2 / face3d=3)。@want@ >= 0 ならその 1 点だけを out へ書いて返る。
 *   @want@ < 0 なら全点を @all@ へ積む。 */
static int
gu_poly_walk(const guPoly &v, int want, double out[3], std::vector<double> *all)
{
	const int dim = v.is_placed() ? 3 : 2;
	const std::vector<int>    &len = v.ringLen();
	const std::vector<double> &xy  = v.xy();
	int k = 0, off = 0;
	for ( size_t r = 0 ; r < len.size() ; ++r ) {
		const int n = ( len[r] > 0 ) ? len[r] : 0;
		for ( int t = 0 ; t < n ; ++t, ++k ) {
			const double x = xy[2*(size_t)(off + t)];
			const double y = xy[2*(size_t)(off + t) + 1];
			double w[3];
			/* ★★ face3d は **world**。枠の中の (x,y) のまま返すと、違う平面に置いた
			 *   同じ形が同じ答えを返し、*置き場所が黙って落ちる* (#3533 が bbox / centroid で
			 *   先に決めた約束をここへ揃えたもの・ひさ判断 2026-09-17)。 */
			if ( dim == 3 ) v.to_world(x, y, w);
			else            { w[0] = x; w[1] = y; w[2] = 0.0; }

			if ( want >= 0 ) {
				if ( k != want ) continue;
				for ( int c = 0 ; c < dim ; ++c ) out[c] = w[c];
				if ( dim == 2 ) out[2] = 0.0;
				return dim;
			}
			for ( int c = 0 ; c < dim ; ++c ) all->push_back(w[c]);
		}
		off += n;
	}
	return ( want >= 0 ) ? 0 : dim;   /* want が範囲外なら 0 */
}

int
guPoly::op_vert(int i, double out[3])
{
	if ( i < 0 ) return 0;
	return gu_poly_walk(*this, i, out, 0);
}

int
guPoly::op_verts(std::vector<double> &out)
{
	out.clear();
	double dummy[3];
	return gu_poly_walk(*this, -1, dummy, &out);
}
