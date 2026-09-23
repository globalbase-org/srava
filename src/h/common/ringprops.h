#ifndef ___common_ringprops_h___
#define ___common_ringprops_h___

/*
 * ringprops.h — 2D 領域の **素性を訊く** 計算 (ヘッダオンリー・カーネル非依存)。
 *               meshprops.h (3D 三角形スープ) の 2D 版。
 *
 * geodesic.h / solids.h / tube.h / affine.h / meshprops.h と同じ方針 (#3474 / #3486 / #3487)。
 * ★ **依存は素の配列だけ** — pigData にも幾何カーネルにも依存しない。
 *
 * ---- なぜ括り出すのか (#3527) ----
 * 3D は meshprops.h が bbox / area / centroid / valid / 位相を **定義ごと 1 本**で持っており、
 * 7 モジュールがそこを通っている。2D にはそれが無く、素性を訊く op が
 *
 *     cg-cross2d / cg-face3d   nverts nfaces bbox centroid area valid nparts part vert verts
 *     mf-cross2d / mf-face3d   nverts nfaces bbox centroid area      ← **5 本が歯抜け**
 *
 * という形で **cgal にしか揃っていない**。しかも cgal の 2D はアルゴリズムではなく
 * @c CGAL::Polygon_set_2 という *表現* を直に読んでいる (@c op_topology は
 * @c regions.size() を返すだけ) ので、そのままでは他カーネルへ配れない。
 * ⇒ 3D と同じように「**表現でなく素の配列**」で書き直したのがこのヘッダ。
 *
 * ---- 表現 (RingView) ----
 * 2D 領域 = **リングの列**。リングは閉じた点列で、最後の点と最初の点は暗黙に繋がる
 * (終点を重複させない — mfCross / cgMesh2D どちらの直列化もそうしている)。
 *
 * ★★ **向きが意味を持つ**。これがこのヘッダの土台:
 *
 *     外周 (材料がある側) … **CCW** ⇒ 靴紐公式の符号つき面積が **正**
 *     穴   (材料が無い側) … **CW**  ⇒ 符号つき面積が **負**
 *
 *   根拠は両カーネルの実装が既にそう書いていること:
 *     mfCross.cpp:27   「NonZero なのは ToPolygons() が外周 CCW・穴 CW で出すため」
 *     mfCross.cpp:203  「外周 CCW が正・穴 CW が負なので、そのまま足すと穴が引かれる」
 *     cgMesh2D.cpp:60  @c add_region_ring が @c is_clockwise_oriented() なら反転して CCW に直す
 *
 * ★★★ **3D との対応**。meshprops.h の topology() は「塊 = 符号つき体積が正のシェル」で
 *   塊を数えている。2D はその 1 次元下の完全な相似形で、**符号つき面積が正のリング = 塊**。
 *   ⇒ 数えるのは符号だけでよく、**包含判定 (レイキャスト) は要らない**。
 *   ⚠ ただし *取り出す* (part) には「どの穴がどの外周のものか」= **入れ子**が要る。
 *     3D で meshprops.h が「シェルの入れ子までは出ない」と書いているのと同じ壁で、
 *     2D はそこを nesting() で解く (2D の包含は点の内外判定 1 本で済むため)。
 *
 * ---- 枠 (frame) はここに持たない ----
 * ⚠ @c cross2d と @c face3d の違い (#3533) = 「z=0 平面に居るか / 空間に置かれているか」は
 *   **値の側の属性**であって、リングの形の性質ではない。@c mfCross が @c fo_/fu_/fv_ を
 *   自分で持っているのと同じで、このヘッダは **枠の中の (x, y) だけ**を見る。
 *   ⇒ 同じ形を別の平面に置いても、ここが答える量は全部同じ (面積・周長・位相は等長不変)。
 *   ⚠ 例外は bbox / centroid で、world 座標が要るなら **呼び側が枠で写す** (cgMesh2D と同じ流儀)。
 *
 * ---- valid の定義 ----
 * ★ meshprops.h が 3D で決めた形 (#3487) を 2D へ写す。**定義は 1 つ・答え方はカーネルごと**:
 *
 *     valid(v) = 1  ⟺  ① 空でない
 *                       ∧ ② 各リングが 3 点以上ある (面積を持ちうる)
 *                       ∧ ③ 自己交差が無い (リング内・リング間のどちらも)
 *
 *   ★ 3D の「② 閉じている (境界辺が無い)」に当たるものが 2D では ② になる。リングは
 *     表現として必ず閉じているので「閉じていない 2D 領域」は作れず、代わりに *面積を
 *     持てない退化リング* を弾く。
 *   ⚠ この実装は **double の述語**で、厳密ではない。cgal は @c Polygon_2::is_simple()
 *     (EPECK の厳密述語) で同じ 3 条件を答え続ける — これは「別のことを答えている」のでは
 *     なく、meshprops.h が 3D で書いたのと同じ「定義は 1 つ・答え方はカーネルごと」。
 *   ⚠ @c mf-cross2d は Clipper2 の出力なので正規化済みで、③ が構造的に真になりうる。
 *     その場合も「その表現では③が恒真」という事実であって、定義のずれではない。
 */

#include <cmath>
#include <vector>
#include <stdint.h>

namespace srava_poly {

/* 素の配列で受ける 2D 領域。
 *   xy      … 2*(Σ ringLen) 個の double。リング順に x,y,x,y,… と並ぶ (終点は重複させない)
 *   ringLen … 各リングの点数 (nrings 個)
 * ⚠ 向きが意味を持つ (外周 CCW / 穴 CW)。ヘッダ冒頭の根拠を参照。 */
struct RingView {
	const double *xy;
	const int    *ringLen;
	int           nrings;
	RingView(const double *p, const int *len, int nr)
	    : xy(p), ringLen(len), nrings(nr) {}
};

namespace detail {

/* 各リングの先頭が xy の何点目から始まるか (offs[nrings] = 総点数)。 */
inline void ring_offsets(const RingView& v, std::vector<int>& offs) {
	offs.assign((size_t)v.nrings + 1, 0);
	for ( int r = 0 ; r < v.nrings ; ++r )
		offs[(size_t)r + 1] = offs[(size_t)r] + ( v.ringLen[r] > 0 ? v.ringLen[r] : 0 );
}

inline const double* pt(const RingView& v, int off, int i) { return v.xy + 2*(size_t)(off + i); }

/* 線分 (p0,p1) と (q0,q1) が **端点を除いて** 交差するか。
 * ⚠ 隣り合う辺は必ず端点を共有するので、共有点だけの接触は交差としない
 *   (meshprops.h の self_intersects が隣接面を除外しているのと同じ約束)。 */
inline int seg_seg(const double *p0, const double *p1, const double *q0, const double *q1) {
	const double rx = p1[0]-p0[0], ry = p1[1]-p0[1];
	const double sx = q1[0]-q0[0], sy = q1[1]-q0[1];
	const double d  = rx*sy - ry*sx;
	const double ax = q0[0]-p0[0], ay = q0[1]-p0[1];
	if ( d == 0.0 ) {
		/* 平行。同一直線上で **重なっている** ときだけ交差とみなす (点接触は除く)。 */
		if ( ax*ry - ay*rx != 0.0 ) return 0;
		const double rr = rx*rx + ry*ry;
		if ( rr == 0.0 ) return 0;
		double t0 = (ax*rx + ay*ry) / rr;
		double t1 = t0 + (sx*rx + sy*ry) / rr;
		if ( t0 > t1 ) { double w = t0; t0 = t1; t1 = w; }
		return ( t1 > 0.0 && t0 < 1.0 ) ? 1 : 0;
	}
	const double t = (ax*sy - ay*sx) / d;   /* p0 + t*r */
	const double u = (ax*ry - ay*rx) / d;   /* q0 + u*s */
	return ( t > 0.0 && t < 1.0 && u > 0.0 && u < 1.0 ) ? 1 : 0;
}

}  /* namespace detail */

/* ---- 頂点数 = 全リングの点数の和 ------------------------------------------------ */
inline int nverts(const RingView& v) {
	int n = 0;
	for ( int r = 0 ; r < v.nrings ; ++r )
		if ( v.ringLen[r] > 0 ) n += v.ringLen[r];
	return n;
}

/* ---- リング 1 本の符号つき面積 (靴紐公式)。外周 CCW が正・穴 CW が負 ---------- */
inline double ring_signed_area(const RingView& v, int off, int n) {
	if ( n < 3 ) return 0.0;
	double s = 0.0;
	for ( int i = 0 ; i < n ; ++i ) {
		const double *p = detail::pt(v, off, i), *q = detail::pt(v, off, (i+1) % n);
		s += p[0]*q[1] - q[0]*p[1];
	}
	return 0.5 * s;
}

/* ---- 面積 = 符号つき面積の総和 (穴がそのまま引かれる) -------------------------- */
inline double area(const RingView& v) {
	std::vector<int> offs;
	detail::ring_offsets(v, offs);
	double a = 0.0;
	for ( int r = 0 ; r < v.nrings ; ++r )
		a += ring_signed_area(v, offs[(size_t)r], v.ringLen[r]);
	return a;
}

/* ---- 周長 = 全リングの辺長の和 (穴の縁も境界なので足す) ------------------------ */
inline double perimeter(const RingView& v) {
	std::vector<int> offs;
	detail::ring_offsets(v, offs);
	double s = 0.0;
	for ( int r = 0 ; r < v.nrings ; ++r ) {
		const int n = v.ringLen[r];
		if ( n < 2 ) continue;
		for ( int i = 0 ; i < n ; ++i ) {
			const double *p = detail::pt(v, offs[(size_t)r], i);
			const double *q = detail::pt(v, offs[(size_t)r], (i+1) % n);
			const double dx = q[0]-p[0], dy = q[1]-p[1];
			s += std::sqrt(dx*dx + dy*dy);
		}
	}
	return s;
}

/* ---- 軸平行バウンディングボックス (枠の中の x,y)。空なら 0 を返し mn/mx は 0 --- */
inline int bbox(const RingView& v, double mn[2], double mx[2]) {
	int first = 1;
	std::vector<int> offs;
	detail::ring_offsets(v, offs);
	for ( int r = 0 ; r < v.nrings ; ++r )
		for ( int i = 0 ; i < v.ringLen[r] ; ++i ) {
			const double *p = detail::pt(v, offs[(size_t)r], i);
			if ( first ) { mn[0] = mx[0] = p[0]; mn[1] = mx[1] = p[1]; first = 0; continue; }
			for ( int k = 0 ; k < 2 ; ++k ) {
				if ( p[k] < mn[k] ) mn[k] = p[k];
				if ( p[k] > mx[k] ) mx[k] = p[k];
			}
		}
	if ( first ) { mn[0] = mn[1] = mx[0] = mx[1] = 0.0; return 0; }
	return 1;
}

/* ---- 重心 = 面積重心 (靴紐モーメント・穴は負寄与) ------------------------------
 * ⚠ 3D の体積重心に対応する量。面積が 0 (退化・穴で相殺) なら 0 を返す。 */
inline int centroid(const RingView& v, double out[2]) {
	std::vector<int> offs;
	detail::ring_offsets(v, offs);
	double a2 = 0.0, cx = 0.0, cy = 0.0;
	for ( int r = 0 ; r < v.nrings ; ++r ) {
		const int n = v.ringLen[r];
		if ( n < 3 ) continue;
		for ( int i = 0 ; i < n ; ++i ) {
			const double *p = detail::pt(v, offs[(size_t)r], i);
			const double *q = detail::pt(v, offs[(size_t)r], (i+1) % n);
			const double cr = p[0]*q[1] - q[0]*p[1];
			a2 += cr;
			cx += (p[0] + q[0]) * cr;
			cy += (p[1] + q[1]) * cr;
		}
	}
	if ( a2 == 0.0 ) { out[0] = out[1] = 0.0; return 0; }
	out[0] = cx / (3.0 * a2);
	out[1] = cy / (3.0 * a2);
	return 1;
}

/* ---- 点がリングの内側か (交差数・crossing number) ------------------------------
 * ⚠ 向きに依らない (穴のリングでも「そのリングが囲む領域の内側か」を答える)。
 * ⚠ 境界のちょうど上は **不定** — 入れ子の判定には境界に載らない点を選ぶこと。 */
inline int point_in_ring(const RingView& v, int off, int n, const double p[2]) {
	if ( n < 3 ) return 0;
	int in = 0;
	for ( int i = 0 ; i < n ; ++i ) {
		const double *a = detail::pt(v, off, i), *b = detail::pt(v, off, (i+1) % n);
		if ( ( (a[1] > p[1]) != (b[1] > p[1]) ) &&
		     ( p[0] < (b[0]-a[0]) * (p[1]-a[1]) / (b[1]-a[1]) + a[0] ) )
			in = ! in;
	}
	return in;
}

/* ================= 位相 (#3527) =================================================
 * ★ 2D の「片」= 塊 = **符号つき面積が正のリング**とその穴。meshprops.h の
 *   「塊 = 符号つき体積が正のシェル」と同じ約束 (#3525 で 2D にも広げてある)。
 * ⚠ 3D の nshells / genus に当たるものは 2D に **無い** (曲面の量なので定義できない)。
 *   ⇒ ここでは数えない。歯抜けではなく意図的な除外 (#3525)。
 */
struct Topology2 {
	int nrings;   /* リングの総数 (外周 + 穴) */
	int nparts;   /* 塊 = 符号つき面積が正のリングの数 */
	Topology2() : nrings(0), nparts(0) {}
};

inline Topology2 topology(const RingView& v) {
	Topology2 t;
	std::vector<int> offs;
	detail::ring_offsets(v, offs);
	for ( int r = 0 ; r < v.nrings ; ++r ) {
		if ( v.ringLen[r] < 3 ) continue;
		++t.nrings;
		if ( ring_signed_area(v, offs[(size_t)r], v.ringLen[r]) > 0.0 ) ++t.nparts;
	}
	return t;
}

/* ---- 入れ子: 各リングの親 (それを直接包む外周リング) -------------------------
 * parent[r] = -1 … 最も外側 (どの外周にも包まれていない)
 *             k  … リング k (符号つき面積が正) の直接の内側
 * ★ 「直接」= 包む外周のうち **面積が最小**のもの。入れ子が深い形 (島の中の池の中の島)
 *   でも正しく組める。
 * ⚠ 判定点はリングの **先頭の点** を使う。リング同士が交差していない (valid) ことが
 *   前提で、交差していれば入れ子はそもそも定義できない。
 */
inline void nesting(const RingView& v, std::vector<int>& parent) {
	std::vector<int> offs;
	detail::ring_offsets(v, offs);
	parent.assign((size_t)v.nrings, -1);
	std::vector<double> absa((size_t)v.nrings, 0.0);
	for ( int r = 0 ; r < v.nrings ; ++r )
		absa[(size_t)r] = std::fabs(ring_signed_area(v, offs[(size_t)r], v.ringLen[r]));

	for ( int r = 0 ; r < v.nrings ; ++r ) {
		if ( v.ringLen[r] < 3 ) continue;
		const double *p = detail::pt(v, offs[(size_t)r], 0);
		int    best = -1;
		double bestA = 0.0;
		for ( int k = 0 ; k < v.nrings ; ++k ) {
			if ( k == r || v.ringLen[k] < 3 ) continue;
			if ( absa[(size_t)k] <= absa[(size_t)r] ) continue;   /* 包むなら必ず大きい */
			if ( ! point_in_ring(v, offs[(size_t)k], v.ringLen[k], p) ) continue;
			if ( best < 0 || absa[(size_t)k] < bestA ) { best = k; bestA = absa[(size_t)k]; }
		}
		parent[(size_t)r] = best;
	}
}

/* ---- i 番目の塊を作るリング番号 (外周 1 本 + その直接の穴) ----------------------
 * ★ 索引 i は **符号つき面積が正のリングの走査順**。⚠ 実装依存 (#3527 の②) なので
 *   「i 番目」を名指す側は版を跨いで信用しないこと。位置で指す口 (part_at) が本命。
 * 返り: 1 = 取り出せた / 0 = i が範囲外。
 * ★ 検定できる形: Σ |area(part(v,i))| == |area(v)| (穴を引いた面積で成立)。
 */
inline int part_rings(const RingView& v, int i, std::vector<int>& out) {
	out.clear();
	if ( i < 0 ) return 0;
	std::vector<int> offs, parent;
	detail::ring_offsets(v, offs);
	nesting(v, parent);

	int outer = -1, seen = 0;
	for ( int r = 0 ; r < v.nrings ; ++r ) {
		if ( v.ringLen[r] < 3 ) continue;
		if ( ring_signed_area(v, offs[(size_t)r], v.ringLen[r]) <= 0.0 ) continue;
		if ( seen == i ) { outer = r; break; }
		++seen;
	}
	if ( outer < 0 ) return 0;

	out.push_back(outer);
	/* その外周の **直接の** 穴だけ (穴の中にまた島がある場合、その島は別の塊)。 */
	for ( int r = 0 ; r < v.nrings ; ++r ) {
		if ( r == outer || v.ringLen[r] < 3 ) continue;
		if ( parent[(size_t)r] != outer ) continue;
		if ( ring_signed_area(v, offs[(size_t)r], v.ringLen[r]) < 0.0 ) out.push_back(r);
	}
	return 1;
}

/* ---- 点 p を **含む** 塊の番号 (#3527 段 4) -------------------------------------
 * ★★ 3D の @srava_mesh::part_at@ と同じ約束 — 「いちばん近い」ではなく **含む**。
 *   塊は *領域* なので内外が言える (殻は曲面なので言えず最近傍しか無い)。
 * ★ 判定は「外周 o の内側 ∧ その **直接の穴** のどれの内側でもない」。
 *   ⚠ 穴の中にまた島がある形でも正しい — その島は **別の塊**なので、島の中の点は
 *     塊 o から見れば「穴の内側」として正しく外れ、島の塊のほうで拾われる。
 * ⚠ 境界のちょうど上は **不定** (point_in_ring の約束)。
 * 返り: >=0 塊の番号 / -1 どの塊も含まない / -2 2 つ以上が含む (入力が valid でない)。 */
inline int part_at(const RingView& v, const double p[2]) {
	std::vector<int> offs, parent;
	detail::ring_offsets(v, offs);
	nesting(v, parent);

	int found = -1, nfound = 0, idx = 0;
	for ( int r = 0 ; r < v.nrings ; ++r ) {
		if ( v.ringLen[r] < 3 ) continue;
		if ( ring_signed_area(v, offs[(size_t)r], v.ringLen[r]) <= 0.0 ) continue;
		int in = point_in_ring(v, offs[(size_t)r], v.ringLen[r], p);
		for ( int k = 0 ; in && k < v.nrings ; ++k ) {
			if ( k == r || v.ringLen[k] < 3 ) continue;
			if ( parent[(size_t)k] != r ) continue;
			if ( ring_signed_area(v, offs[(size_t)k], v.ringLen[k]) >= 0.0 ) continue;
			if ( point_in_ring(v, offs[(size_t)k], v.ringLen[k], p) ) in = 0;   /* 穴の中 */
		}
		if ( in ) { found = idx; ++nfound; }
		++idx;
	}
	if ( nfound == 0 ) return -1;
	if ( nfound >  1 ) return -2;
	return found;
}

/* ---- 自己交差 (リング内・リング間の両方) --------------------------------------
 * ⚠ 端点だけの接触は交差としない (detail::seg_seg の約束)。⇒ 「1 点で触れ合う
 *   蝶ネクタイ」は検出できない。cgal の @c Polygon_2::is_simple() も同じ扱い。 */
/* ★★ #3553: **枠の中で** 点から 2D 領域までの最短距離 (符号なし)。
 *   材料がある側にいれば 0 ・ 外 (または穴の中) なら境界までの距離。
 *   ⚠ 3D の距離はこれだけでは出ない — 面外の成分は **呼び手が合成する**
 *     (平面領域なので最近点は「射影が中なら射影・外なら境界上」⇒ sqrt(面外² + これ²))。
 *   ★ 穴の中の点は「材料が無い」ので 0 にならない (穴の縁までの距離が出る) —
 *     全リングの辺を見るので自然にそうなる。 */
inline double distance_in_plane(const RingView& v, const double q[2]) {
	if ( v.nrings <= 0 ) return -1.0;
	if ( part_at(v, q) >= 0 ) return 0.0;      /* 材料の上 */
	double best = -1.0;
	int off = 0;
	for ( int r = 0 ; r < v.nrings ; ++r ) {
		const int n = v.ringLen[r];
		for ( int i = 0 ; i < n ; ++i ) {
			const double *a = v.xy + 2 * (size_t)(off + i);
			const double *b = v.xy + 2 * (size_t)(off + (i + 1) % n);
			const double ex = b[0] - a[0], ey = b[1] - a[1];
			const double wx = q[0] - a[0], wy = q[1] - a[1];
			const double L2 = ex*ex + ey*ey;
			double t = ( L2 > 0.0 ) ? (wx*ex + wy*ey) / L2 : 0.0;
			if ( t < 0.0 ) t = 0.0; else if ( t > 1.0 ) t = 1.0;
			const double dx = wx - t*ex, dy = wy - t*ey;
			const double d2 = dx*dx + dy*dy;
			if ( best < 0.0 || d2 < best ) best = d2;
		}
		off += n;
	}
	return ( best < 0.0 ) ? -1.0 : std::sqrt(best);
}

/* ---- 点 p の **2D 領域に対する位置** (#3579) --------------------------------------
 * 返り: **0 = 境界ちょうど / -1 = 内側 (開) / +1 = 外側**
 *
 * ★★ 3D の @srava_mesh::classify_point@ と **同じ約束・同じ 3 値**。境界ちょうどの点を
 *   内と外のどちらに入れるかは一意に決まらないので判定器に答えさせず、**第 3 の集合**として
 *   切り出す (ひさ 2026-09-22・#3575)。⇒ 2D と 3D で答え方が変わらない。
 * ★ 内外は @part_at@ (= 巻き数の 2D 版・穴を正しく外す) に任せる。⇒ **穴の中は外側**で、
 *   3D の「空洞の中は外側」と揃う。判定を書き直さないこと。
 * ⚠ @distance_in_plane@ は **使えない** — あれは *材料の上*で 0 を返すので、内側の点まで
 *   境界に化ける。境界までの距離は全リングの辺で測る (下のループ)。
 * ★ 許容差の向きは「ちょうど」を広げる側 (疑わしきは境界へ = 利用者が決める側)。
 * @tol_abs は **絶対**の許容差 (呼び手が尺度を掛けて渡す)。負なら境界を判定しない。 */
inline int classify_point(const RingView& v, const double p[2], double tol_abs) {
	if ( v.nrings <= 0 ) return 1;          /* 空の領域: すべて外側 */
	if ( tol_abs >= 0.0 ) {
		double best = -1.0;
		int off = 0;
		for ( int r = 0 ; r < v.nrings ; ++r ) {
			const int n = v.ringLen[r];
			for ( int i = 0 ; i < n ; ++i ) {
				const double *a = v.xy + 2 * (size_t)(off + i);
				const double *b = v.xy + 2 * (size_t)(off + (i + 1) % n);
				const double ex = b[0] - a[0], ey = b[1] - a[1];
				const double wx = p[0] - a[0], wy = p[1] - a[1];
				const double L2 = ex*ex + ey*ey;
				double s = ( L2 > 0.0 ) ? (wx*ex + wy*ey) / L2 : 0.0;
				if ( s < 0.0 ) s = 0.0; else if ( s > 1.0 ) s = 1.0;
				const double dx = wx - s*ex, dy = wy - s*ey;
				const double d2 = dx*dx + dy*dy;
				if ( best < 0.0 || d2 < best ) best = d2;
			}
			off += n;
		}
		if ( best >= 0.0 && best <= tol_abs * tol_abs ) return 0;
	}
	return ( part_at(v, p) >= 0 ) ? -1 : 1;
}

inline int self_intersects(const RingView& v) {
	std::vector<int> offs;
	detail::ring_offsets(v, offs);
	const int nr = v.nrings;

	for ( int r = 0 ; r < nr ; ++r ) {
		const int n = v.ringLen[r];
		if ( n < 3 ) continue;
		for ( int i = 0 ; i < n ; ++i ) {
			const double *a0 = detail::pt(v, offs[(size_t)r], i);
			const double *a1 = detail::pt(v, offs[(size_t)r], (i+1) % n);
			/* 同じリング内。隣接辺 (i+1) は端点共有なので j は i+2 から。
			 * ⚠ 最後の辺は 0 番と隣接するので、i==0 のときだけ j の上限を 1 つ下げる。 */
			for ( int j = i + 2 ; j < n ; ++j ) {
				if ( i == 0 && j == n - 1 ) continue;
				const double *b0 = detail::pt(v, offs[(size_t)r], j);
				const double *b1 = detail::pt(v, offs[(size_t)r], (j+1) % n);
				if ( detail::seg_seg(a0, a1, b0, b1) ) return 1;
			}
			/* 別のリングとは全対全 (端点共有も無いので除外は不要)。 */
			for ( int s = r + 1 ; s < nr ; ++s ) {
				const int m = v.ringLen[s];
				if ( m < 3 ) continue;
				for ( int j = 0 ; j < m ; ++j ) {
					const double *b0 = detail::pt(v, offs[(size_t)s], j);
					const double *b1 = detail::pt(v, offs[(size_t)s], (j+1) % m);
					if ( detail::seg_seg(a0, a1, b0, b1) ) return 1;
				}
			}
		}
	}
	return 0;
}

/* ---- valid — ① 空でない ∧ ② 各リングが 3 点以上 ∧ ③ 自己交差が無い ----------
 * 定義の全文と根拠はこのヘッダの冒頭 (meshprops.h の 3D の定義から写したもの)。 */
inline int valid(const RingView& v) {
	if ( v.nrings <= 0 ) return 0;                      /* ① */
	int any = 0;
	for ( int r = 0 ; r < v.nrings ; ++r ) {
		if ( v.ringLen[r] <= 0 ) continue;
		if ( v.ringLen[r] < 3 ) return 0;               /* ② 面積を持てないリング */
		any = 1;
	}
	if ( ! any ) return 0;                              /* ① 実質空 */
	return self_intersects(v) ? 0 : 1;                  /* ③ */
}

}  /* namespace srava_poly */

#endif /* ___common_ringprops_h___ */
