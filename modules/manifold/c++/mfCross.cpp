/*
 * mfCross — 2D manifold::CrossSection ラッパの実装(cgMesh2D のミラー・CGAL 非依存)。
 *   polygon/rect/circle/ngon で断面を作り、extrude/revolve(mfaExtrude/mfaRevolve)が 3D へ持ち上げる。
 *   codec は ToPolygons() のリング列を raw double で直列化(mfMesh の raw-double 方針と一貫)。
 */
#include	"mf/c++/mfMesh.h"

#include	<math.h>
#include	"ts2/c++/stdString.h"

#include	<cstdio>
#include	<cstring>
#include	<cstdint>
#include	<cstddef>
#include	<cmath>
#include	<vector>
#include	<algorithm>
#include <algorithm>   /* ★ #3529: std::reverse (鏡像で巻き方向を戻す) */

using manifold::CrossSection;
using manifold::SimplePolygon;
using manifold::Polygons;
using manifold::vec2;

/* ★★ #3529: CrossSection の **遅延構築**。真実は p_ で、ここは派生。
 *   実際に Clipper2 が要る op (boolean / offset / hull / combine) だけがここを通る。
 *   ⚠ NonZero なのは ToPolygons() が外周 CCW・穴 CW で出すため (旧 decode と同じ規約)。 */
const manifold::CrossSection&
mfCross::cross() const
{
	if ( ! built_ ) {
		c_ = CrossSection(p_, CrossSection::FillRule::NonZero);
		built_ = true;
	}
	return c_;
}

/* ★★ #3529: 2x3 アフィンを **輪郭列に直接**当てる。
 *   @c CrossSection::Transform を経由すると、その結果から mfCross を作るときに ToPolygons() が
 *   走り、**Clipper2 の量子化に戻ってしまう** (アフィン変換は無損失なのに値が動く)。
 *   ⚠ det<0 (鏡像・負のスケール) は巻き方向が裏返るので、各リングを反転して
 *     「外周 CCW・穴 CW」の規約を保つ。旧実装では CrossSection の FillRule が
 *     結果的に向きを正規化していた部分をここで明示的に引き受ける。 */
static Polygons
mf_xform_polys(const Polygons &in, const manifold::mat2x3 &m)
{
	const double det = (double)m[0].x * (double)m[1].y - (double)m[0].y * (double)m[1].x;
	const bool flip = ( det < 0.0 );
	Polygons out;
	out.reserve(in.size());
	for ( size_t r = 0 ; r < in.size() ; ++r ) {
		const SimplePolygon &ring = in[r];
		SimplePolygon o;
		o.reserve(ring.size());
		for ( size_t i = 0 ; i < ring.size() ; ++i ) {
			const vec2 &p = ring[i];
			o.push_back(vec2(m[0].x * p.x + m[1].x * p.y + m[2].x,
			                 m[0].y * p.x + m[1].y * p.y + m[2].y));
		}
		if ( flip ) std::reverse(o.begin(), o.end());
		out.push_back(o);
	}
	return out;
}

sPtr<stdString>
mfCross::get_str()
{
	char buf[64];
	::snprintf(buf, sizeof buf, "<cross:manifold area=%.6g>", op_area());
	return thNEW(stdString,(buf));
}

/* ---- codec: リング列を raw double で(little-endian)---- */
static void put_u32(mfChunkSink &s, uint32_t v) {
	uint8_t b[4] = { (uint8_t)v, (uint8_t)(v>>8), (uint8_t)(v>>16), (uint8_t)(v>>24) };
	s.chunk(b, 4);
}
static void put_f64(mfChunkSink &s, double d) {
	uint8_t b[8]; ::memcpy(b, &d, 8); s.chunk(b, 8);
}
static uint32_t get_u32(mfChunkSource &s) {
	uint8_t b[4]; s.pull(b, 4);
	return (uint32_t)b[0] | ((uint32_t)b[1]<<8) | ((uint32_t)b[2]<<16) | ((uint32_t)b[3]<<24);
}
static double get_f64(mfChunkSource &s) {
	uint8_t b[8]; s.pull(b, 8); double d; ::memcpy(&d, b, 8); return d;
}

void
mfCross::encode(mfChunkSink &sink)
{
	const Polygons &ps = polys();   /* ★ #3529: 真実をそのまま書く (ToPolygons を通さない) */
	put_u32(sink, (uint32_t)ps.size());
	for ( size_t r = 0 ; r < ps.size() ; ++r ) {
		const SimplePolygon &ring = ps[r];
		put_u32(sink, (uint32_t)ring.size());
		for ( size_t i = 0 ; i < ring.size() ; ++i ) {
			put_f64(sink, ring[i].x);
			put_f64(sink, ring[i].y);
		}
	}
	/* ★★ #3526: 枠 (frame) の節。**既定 (z=0 平面) なら書かない** ので、平面の 2D の blob は
	 *   従来とバイト単位で同じになる (既存キャッシュがそのまま効く)。
	 *   ⚠ 逆は成り立たない — 古いバイナリはこの節を読まずに終わり、空間に置いた 2D が
	 *     **黙って z=0 に戻る**。⇒ cache_version を上げてある。 */
	/* ★★ #3533: 条件が「枠が既定でない」から「**face3d である**」に変わった (cgal の
	 *   cgMesh2D::encode と同じ — ⚠ 片方だけ直さないこと)。@rotate(rect,"z",90)@ は枠が
	 *   既定のままでも型は face3d (規約①) なので、節を書かないと読み直しで cross2d に戻る。
	 *   ⇒ 同じ式でもブロブが #3526 時代と変わる = **cache_version を上げてある**。 */
	if ( placed_ || ! frame_is_default() ) {
		put_u32(sink, 0x4652414dU);   /* "MARF" = frame マーカ (後続の節と取り違えないため) */
		for ( int i = 0 ; i < 3 ; ++i ) put_f64(sink, fo_[i]);
		for ( int i = 0 ; i < 3 ; ++i ) put_f64(sink, fu_[i]);
		for ( int i = 0 ; i < 3 ; ++i ) put_f64(sink, fv_[i]);
	}
}

void
mfCross::decode(mfChunkSource &src)
{
	if ( crossExactInput_ ) { decode_cross_exact(src); return; }   /* ★ CGAL PLY2 → double(cast downgrade) */
	uint32_t nr = get_u32(src);
	Polygons ps;
	ps.reserve(nr);
	for ( uint32_t r = 0 ; r < nr ; ++r ) {
		uint32_t npt = get_u32(src);
		SimplePolygon ring;
		ring.reserve(npt);
		for ( uint32_t i = 0 ; i < npt ; ++i ) {
			double x = get_f64(src);
			double y = get_f64(src);
			ring.push_back(vec2(x, y));
		}
		ps.push_back(ring);
	}
	/* ★★ #3529: **ここで作り直さない**。旧実装は CrossSection(ps, NonZero) で再構成しており、
	 *   構成子が Clipper2 の Union を通るため座標が整数格子に載り、*同じ式が cache hit と miss で
	 *   違う値*を返していた。自分が encode した blob は既に正準形なので、輪郭列をそのまま真実にする。
	 *   ⚠ CrossSection が要る op (boolean / offset / hull) は cross() が遅延構築する。 */
	p_ = ps;
	built_ = false;
	/* ★ #3526: 枠の節 (あれば)。旧 blob には無いので more() で判定する。 */
	if ( src.more() ) {
		uint32_t mark = get_u32(src);
		if ( mark == 0x4652414dU ) {
			for ( int i = 0 ; i < 3 ; ++i ) fo_[i] = get_f64(src);
			for ( int i = 0 ; i < 3 ; ++i ) fu_[i] = get_f64(src);
			for ( int i = 0 ; i < 3 ; ++i ) fv_[i] = get_f64(src);
			placed_ = 1;   /* ★ #3533: 節が在る = face3d (encode と対) */
		}
	}
}

/* ---- 2D ブーリアン(CrossSection +/^/-)---- */
sPtr<mfCross>
mfCross::op_union(sPtr<mfCross> b)
{
	if ( ! b.is_notNull() ) return sPtr<mfCross>();
	/* ★★ #3526: **枠 (平面) が違う 2D どうしのブールは成立しない**。別の平面にある 2 つの
	 *   平面領域の交わりは *線分以下* に落ちるので 2D 領域として表せない (ひさ判断 ②)。
	 *   ⇒ null を返して呼び手に明示エラーを出させる。黙って「同じ平面にあるもの」として
	 *     計算すると、#2940662 で潰した「黙って射影する」と同じ穴になる。 */
	if ( ! same_frame(b->frame_o(), b->frame_u(), b->frame_v()) ) return sPtr<mfCross>();
	sPtr<mfCross> r = thNEW(mfCross,(cross() + b->cross()));
	r->set_frame(fo_, fu_, fv_);   /* 枠は引き継ぐ */
	r->set_placed(placed_ || b->is_placed());   /* ★ #3533 規約③ */
	return r;
}
sPtr<mfCross>
mfCross::op_intersection(sPtr<mfCross> b)
{
	if ( ! b.is_notNull() ) return sPtr<mfCross>();
	/* ★★ #3526: **枠 (平面) が違う 2D どうしのブールは成立しない**。別の平面にある 2 つの
	 *   平面領域の交わりは *線分以下* に落ちるので 2D 領域として表せない (ひさ判断 ②)。
	 *   ⇒ null を返して呼び手に明示エラーを出させる。黙って「同じ平面にあるもの」として
	 *     計算すると、#2940662 で潰した「黙って射影する」と同じ穴になる。 */
	if ( ! same_frame(b->frame_o(), b->frame_u(), b->frame_v()) ) return sPtr<mfCross>();
	sPtr<mfCross> r = thNEW(mfCross,(cross() ^ b->cross()));
	r->set_frame(fo_, fu_, fv_);   /* 枠は引き継ぐ */
	r->set_placed(placed_ || b->is_placed());   /* ★ #3533 規約③ */
	return r;
}
sPtr<mfCross>
mfCross::op_difference(sPtr<mfCross> b)
{
	if ( ! b.is_notNull() ) return sPtr<mfCross>();
	/* ★★ #3526: **枠 (平面) が違う 2D どうしのブールは成立しない**。別の平面にある 2 つの
	 *   平面領域の交わりは *線分以下* に落ちるので 2D 領域として表せない (ひさ判断 ②)。
	 *   ⇒ null を返して呼び手に明示エラーを出させる。黙って「同じ平面にあるもの」として
	 *     計算すると、#2940662 で潰した「黙って射影する」と同じ穴になる。 */
	if ( ! same_frame(b->frame_o(), b->frame_u(), b->frame_v()) ) return sPtr<mfCross>();
	sPtr<mfCross> r = thNEW(mfCross,(cross() - b->cross()));
	r->set_frame(fo_, fu_, fv_);   /* 枠は引き継ぐ */
	r->set_placed(placed_ || b->is_placed());   /* ★ #3533 規約③ */
	return r;
}

/* ---- 計測 ---- */
/* ★★ #3529: 面積は **輪郭列から靴紐公式**で出す。CrossSection::Area() を呼ぶと
 *   遅延構築で Clipper2 の量子化を通ってしまい、*同じ式が経路で違う値*に戻る
 *   (これが起票時の症状そのもの: area(circle) 対 area(translate(circle)))。
 *   外周 CCW が正・穴 CW が負なので、そのまま足すと穴が引かれる (Area() と同じ規約)。 */
double
mfCross::op_area()
{
	double a = 0.0;
	const Polygons &ps = polys();
	for ( size_t r = 0 ; r < ps.size() ; ++r ) {
		const SimplePolygon &ring = ps[r];
		const size_t n = ring.size();
		double s = 0.0;
		for ( size_t i = 0 ; i < n ; ++i ) {
			const vec2 &p = ring[i], &q = ring[(i+1) % n];
			s += p.x * q.y - q.x * p.y;
		}
		a += 0.5 * s;
	}
	return a;
}

/* ---- 計測: 頂点数 / 面数 (#3443)。2D は面を持たないので 0。 ---- */
int
mfCross::op_nverts()
{
	const manifold::Polygons &ps = polys();   /* ★ #3529 */
	int n = 0;
	for ( size_t i = 0 ; i < ps.size() ; ++i ) n += (int)ps[i].size();
	return n;
}

int mfCross::op_nfaces() { return 0; }

int
mfCross::op_bbox(double mn[3], double mx[3])
{
	/* ★ #3529: 輪郭列から直接。CrossSection::Bounds() は遅延構築を誘発する。
	 * ★★ #3533: **face3d は world の 3 成分**・cross2d は局所 2 成分 (cgMesh2D::op_bbox と対。
	 *   ⚠ 片方だけ直さないこと — 同じ式が 2 つのカーネルで別の答えになる)。 */
	const int world = placed_;
	const int nd = world ? 3 : 2;
	double lo[3] = { 0, 0, 0 }, hi[3] = { 0, 0, 0 };
	int seen = 0;
	const manifold::Polygons &ps = polys();
	for ( size_t r = 0 ; r < ps.size() ; ++r )
		for ( size_t i = 0 ; i < ps[r].size() ; ++i ) {
			const vec2 &p = ps[r][i];
			double w[3];
			if ( world ) to_world(p.x, p.y, w);
			else { w[0] = p.x; w[1] = p.y; w[2] = 0.0; }
			if ( ! seen ) { for ( int k = 0 ; k < nd ; ++k ) lo[k] = hi[k] = w[k]; seen = 1; continue; }
			for ( int k = 0 ; k < nd ; ++k ) {
				if ( w[k] < lo[k] ) lo[k] = w[k];
				if ( w[k] > hi[k] ) hi[k] = w[k];
			}
		}
	mn[0] = lo[0]; mn[1] = lo[1]; mn[2] = world ? lo[2] : 0.0;
	mx[0] = hi[0]; mx[1] = hi[1]; mx[2] = world ? hi[2] : 0.0;
	return nd;
}

/* ---- ★★ #3526: アフィン変換 — 枠と局所座標に分けて当てる -----------------------
 * 2D の世界での姿は { O + xU + yV : (x,y) ∈ P }。これに (M,t) を当てると
 *     O' = M O + t   U' = M U   V' = M V
 * になる。⇒ **多角形 P は本来そのまま**でよい。ところが U',V' は正規直交とは限らず、
 * 枠が歪むと area だけでなく perimeter / offset が局所座標で計算できなくなる
 * (非等方な写像は長さを変える) ⇒ *枠は正規直交に保ち、歪みは局所座標が持つ*。
 *
 * ★ 2 つに分かれる (どちらも同じ式で書ける — 違うのは「新しい枠をどう選ぶか」だけ):
 *   ① 平面が動かない  … 枠は **そのまま**。A = (U',V') を旧 (U,V) で表した 2x2。
 *      ⇒ z 軸回転・XY 平行移動・XY スケールは *今日とビット単位で同じ* (図形が動く)。
 *   ② 平面が動く      … 枠を動かす。U'' = normalize(U')・V'' = n x U'' を新しい軸にし、
 *      A = (U',V') を (U'',V'') で表した 2x2。⇒ 断面が空間に置かれる (#3511 の loft の前提)。
 *      ★ U'' の選び方を「U' を正規化する」に固定してあるので、同じ式は同じ枠になる
 *        (再現しないと キャッシュに焼き付いた値が版ごとに別物になる)。
 *
 * ⚠ 線形部が退化 (U' と V' が平行 = 平面が線に潰れる) なら null を返す。呼び手が明示エラーにする。 */
static void mf_cross3(const double a[3], const double b[3], double o[3]) {
	o[0] = a[1]*b[2] - a[2]*b[1];
	o[1] = a[2]*b[0] - a[0]*b[2];
	o[2] = a[0]*b[1] - a[1]*b[0];
}
static double mf_dot3(const double a[3], const double b[3]) {
	return a[0]*b[0] + a[1]*b[1] + a[2]*b[2];
}
static double mf_norm3(double a[3]) {
	double n = ::sqrt(mf_dot3(a,a));
	if ( n > 0 ) { a[0]/=n; a[1]/=n; a[2]/=n; }
	return n;
}

/* ★★ #3526: 同じ平面か (軸の取り方は違ってよい)。 */
int
mfCross::same_plane(const double o[3], const double u[3], const double v[3]) const
{
	double n1[3], n2[3];
	mf_cross3(fu_, fv_, n1);
	mf_cross3(u, v, n2);
	if ( mf_norm3(n1) == 0.0 || mf_norm3(n2) == 0.0 ) return 0;
	double cr[3];
	mf_cross3(n1, n2, cr);
	const double EPS = 1e-12;
	if ( ::sqrt(mf_dot3(cr, cr)) > EPS ) return 0;          /* 法線が平行でない = 別の平面 */
	double d[3] = { o[0]-fo_[0], o[1]-fo_[1], o[2]-fo_[2] };
	double h = mf_dot3(d, n1);
	return ( ( h < 0 ? -h : h ) <= EPS ) ? 1 : 0;           /* 原点が同じ平面に載っているか */
}

/* ★ #3533: 面積重心 (靴紐モーメント・穴は負寄与)。cgMesh2D::op_centroid と **同じ規約**。
 *   ⚠ #3533 まで manifold には 2D の centroid が無かった (cgal / occt は持っていた歯抜け)。
 *   ★ 真実は輪郭列 (#3529) なので CrossSection を構築しない。外周 CCW が正・穴 CW が負なので
 *     そのまま足すと穴が引かれる (op_area と同じ)。
 *   ★★ face3d は **world の 3 成分**を返す (op_bbox と同じ理由 — 局所のままだと world で同じ
 *     図形に 2 つの答えが出る)。cross2d は従来どおり局所 2 成分。 */
int
mfCross::op_centroid(double out[3])
{
	double a = 0.0, mx = 0.0, my = 0.0;
	const Polygons &ps = polys();
	for ( size_t r = 0 ; r < ps.size() ; ++r ) {
		const SimplePolygon &ring = ps[r];
		const size_t n = ring.size();
		for ( size_t i = 0 ; i < n ; ++i ) {
			const vec2 &p = ring[i], &q = ring[(i+1) % n];
			const double cr = p.x * q.y - q.x * p.y;
			a  += cr;
			mx += (p.x + q.x) * cr;
			my += (p.y + q.y) * cr;
		}
	}
	double cx = 0.0, cy = 0.0;
	if ( a != 0.0 ) { cx = mx / (3.0 * a); cy = my / (3.0 * a); }
	if ( placed_ ) { to_world(cx, cy, out); return 3; }
	out[0] = cx; out[1] = cy; out[2] = 0.0;
	return 2;
}

/* ★★ #3526: 相手の枠で表し直す (幾何は動かさない・局所座標の取り方だけ変える)。
 *
 * 局所 (x,y) の世界点は O + xU + yV。これを相手の枠 (o,u,v) で読み直すと
 *     x' = (O-o)·u + x(U·u) + y(V·u)
 *     y' = (O-o)·v + x(U·v) + y(V·v)
 * ⇒ 局所座標に 2x2 + 平行移動を当てるだけ。★ 枠は正規直交なので内積で射影できる。
 * ⚠ 2 つの枠の向きが逆 (n が反対) だと det<0 = 鏡像になる。輪の巻き方が裏返るが、
 *   CrossSection は FillRule で向きを正規化するので、そのまま Transform に渡してよい
 *   (mirror op が同じ経路で動いているのが根拠)。 */
sPtr<mfCross>
mfCross::reexpress(const double o[3], const double u[3], const double v[3]) const
{
	if ( ! same_plane(o, u, v) ) return sPtr<mfCross>();
	double d[3] = { fo_[0]-o[0], fo_[1]-o[1], fo_[2]-o[2] };
	double a00 = mf_dot3(fu_, u), a01 = mf_dot3(fv_, u), a02 = mf_dot3(d, u);
	double a10 = mf_dot3(fu_, v), a11 = mf_dot3(fv_, v), a12 = mf_dot3(d, v);
	manifold::mat2x3 m(
	    vec2(a00, a10),    /* col 0 */
	    vec2(a01, a11),    /* col 1 */
	    vec2(a02, a12));   /* col 2 = 平行移動 */
	sPtr<mfCross> out = thNEW(mfCross,(mf_xform_polys(polys(), m)));   /* ★ #3529: 量子化を通さない */
	out->set_frame(o, u, v);
	out->set_placed(placed_);   /* ★ #3533: 同じ値を別の枠で読み直すだけ = 型は動かない */
	return out;
}

/* ★★ #3534: world の (x,y) を取り z を捨てる (project_flatten)。
 *   局所 (x,y) → world → z を捨てる、は平面上の 2x2 アフィンに畳める (詳細はヘッダ)。
 *   ⚠ 巻き方の反転 (det<0) は mf_xform_polys が見るので、ここでは det の **0 判定だけ**。 */
sPtr<mfCross>
mfCross::project_flatten() const
{
	const double det = fu_[0]*fv_[1] - fu_[1]*fv_[0];   /* = (U x V)・ẑ */
	if ( ( det < 0 ? -det : det ) <= 1e-12 ) return sPtr<mfCross>();   /* 平面が ẑ を含む */
	manifold::mat2x3 m(
	    vec2(fu_[0], fu_[1]),      /* col 0 = U の (x,y) */
	    vec2(fv_[0], fv_[1]),      /* col 1 = V の (x,y) */
	    vec2(fo_[0], fo_[1]));     /* col 2 = O の (x,y) */
	sPtr<mfCross> out = thNEW(mfCross,(mf_xform_polys(polys(), m)));   /* ★ #3529: 量子化を通さない */
	/* 枠は既定のまま・placed_ は 0 ⇒ 型は mf-cross2d (SVG へ書ける)。 */
	return out;
}

sPtr<mfGeom>
mfCross::apply_affine(const double e[12])
{
	/* 枠の像。★ O は平行移動を受けるが、U/V は **線形部だけ** (方向ベクトルなので)。 */
	double O2[3], U2[3], V2[3];
	for ( int i = 0 ; i < 3 ; ++i ) {
		O2[i] = e[4*i+0]*fo_[0] + e[4*i+1]*fo_[1] + e[4*i+2]*fo_[2] + e[4*i+3];
		U2[i] = e[4*i+0]*fu_[0] + e[4*i+1]*fu_[1] + e[4*i+2]*fu_[2];
		V2[i] = e[4*i+0]*fv_[0] + e[4*i+1]*fv_[1] + e[4*i+2]*fv_[2];
	}
	double n2[3];
	mf_cross3(U2, V2, n2);
	if ( mf_norm3(n2) == 0.0 ) return sPtr<mfGeom>();   /* 平面が潰れた */

	/* ① 平面が動かないか: 法線が同じ向き (符号は問わない) かつ O2 が元の平面に載っている。 */
	double n1[3];
	mf_cross3(fu_, fv_, n1);
	mf_norm3(n1);
	double cr[3];
	mf_cross3(n1, n2, cr);
	double d[3] = { O2[0]-fo_[0], O2[1]-fo_[1], O2[2]-fo_[2] };
	const double EPS = 1e-12;
	int same_plane = ( ::sqrt(mf_dot3(cr,cr)) <= EPS
	                && ( mf_dot3(d,n1) < 0 ? -mf_dot3(d,n1) : mf_dot3(d,n1) ) <= EPS );

	double nu[3], nv[3], no[3];
	if ( same_plane ) {                       /* 枠はそのまま */
		for ( int i = 0 ; i < 3 ; ++i ) { nu[i]=fu_[i]; nv[i]=fv_[i]; no[i]=fo_[i]; }
	} else {                                  /* 枠を動かす */
		for ( int i = 0 ; i < 3 ; ++i ) { nu[i]=U2[i]; no[i]=O2[i]; }
		if ( mf_norm3(nu) == 0.0 ) return sPtr<mfGeom>();
		mf_cross3(n2, nu, nv);
		if ( mf_norm3(nv) == 0.0 ) return sPtr<mfGeom>();
	}
	/* A = (U',V') を新しい軸 (nu,nv) で表した 2x2。平行移動は (O2-no) を新軸へ射影したもの。 */
	double do_[3] = { O2[0]-no[0], O2[1]-no[1], O2[2]-no[2] };
	double a00 = mf_dot3(U2,nu), a01 = mf_dot3(V2,nu), a02 = mf_dot3(do_,nu);
	double a10 = mf_dot3(U2,nv), a11 = mf_dot3(V2,nv), a12 = mf_dot3(do_,nv);

	manifold::mat2x3 m(
	    vec2(a00, a10),    /* col 0 */
	    vec2(a01, a11),    /* col 1 */
	    vec2(a02, a12));   /* col 2 = 平行移動 */
	sPtr<mfCross> out = thNEW(mfCross,(mf_xform_polys(polys(), m)));   /* ★ #3529: 量子化を通さない */
	out->set_frame(no, nu, nv);
	/* ★★ #3533 規約①: **transform 系は常に face3d を返す** (cgMesh2D::apply_affine と対)。
	 *   結果がたまたま z=0 に留まっていても型は face3d。幾何として z=0 に居るかは
	 *   @frame_is_default()@ が別に答え、cast (規約②) だけがそちらを見る。 */
	out->set_placed(1);
	return sPtr<mfGeom>::d_cast(out);
}

bool
mfCross::write_to(const char * /*path*/, const char * /*unit*/)
{
	return false;   /* 2D 出力(SVG/DXF)は当面未対応 */
}

/* ---- primitive ---- */
sPtr<mfCross>
mfCross::polygon(const double *xy, int npts)
{
	/* 点列 → SimplePolygon。連続重複・閉じ重複を間引く(cgaPolygon と一貫。零長エッジ除去)。
	 * 符号付き面積が負(CW)なら反転して CCW(=Positive)に正規化。 */
	SimplePolygon ring;
	ring.reserve(npts);
	for ( int i = 0 ; i < npts ; ++i ) {
		vec2 p(xy[2*i], xy[2*i+1]);
		if ( ! ring.empty() && ring.back().x == p.x && ring.back().y == p.y )
			continue;
		ring.push_back(p);
	}
	while ( ring.size() >= 2 && ring.back().x == ring.front().x && ring.back().y == ring.front().y )
		ring.pop_back();
	/* 符号付き面積(shoelace)。負=CW → 反転して CCW。 */
	double a2 = 0.0;
	for ( size_t i = 0 ; i < ring.size() ; ++i ) {
		const vec2 &p = ring[i], &q = ring[(i+1) % ring.size()];
		a2 += p.x * q.y - q.x * p.y;
	}
	if ( a2 < 0.0 )
		std::reverse(ring.begin(), ring.end());
	return thNEW(mfCross,(CrossSection(ring, CrossSection::FillRule::Positive)));
}

sPtr<mfCross>
mfCross::rect(double w, double h)
{
	/* cgaRect に合わせ原点隅(0,0)→(w,h)。center=false。 */
	return thNEW(mfCross,(CrossSection::Square(vec2(w, h), false)));
}

sPtr<mfCross>
mfCross::circle(double r, int segs)
{
	if ( segs < 3 ) segs = 32;   /* ★ #3530: 0 = 未指定 → 既定 32 (op 側で検査済み) */
	return thNEW(mfCross,(CrossSection::Circle(r, segs)));
}

sPtr<mfCross>
mfCross::ngon(int n, double r)
{
	if ( n < 3 ) n = 3;
	SimplePolygon ring;
	ring.reserve(n);
	for ( int k = 0 ; k < n ; ++k ) {
		double a = 2.0 * 3.14159265358979323846 * (double)k / (double)n;
		ring.push_back(vec2(r * ::cos(a), r * ::sin(a)));
	}
	return thNEW(mfCross,(CrossSection(ring, CrossSection::FillRule::Positive)));
}
