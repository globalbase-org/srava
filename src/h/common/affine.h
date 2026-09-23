#ifndef ___common_affine_h___
#define ___common_affine_h___

/*
 * affine.h — アフィン変換 4 op (rotate / scale / mirror / transform) の
 *            **引数の解釈と行列の組み立て** (ヘッダオンリー・カーネル非依存)。
 *
 * colorspec.h と同じ立ち位置: pigData には依存する (引数はスクリプトの値) が、
 * 幾何カーネルには依存しない。全モジュールがこのヘッダを include する。
 *
 * ---- なぜ括り出すのか (#3486) ----
 * 4 op はどれも「引数を読む → 行優先 3x4 の double[12] を作る → カーネルの
 * apply_affine へ渡す」だけで、**行列を作るところまではカーネル差が無い**。
 * にもかかわらず 2026-09-05 の時点で cgal / manifold / occt / openvdb の 4 本が
 * 同じ解釈を別々に書いており、受理する書き方は同じなのにエラー文だけが 4 通りに
 * 割れていた。nef / geogram / cherchi の 3 本へ配るとこれが 7 通りになる。
 * ⇒ **受け付ける書き方と拒否の理由**を 1 箇所に集約する (colorspec.h と同じ理由)。
 *
 * ---- 受理する書き方 (全カーネル共通) ----
 *   rotate(m, axis, deg)  axis = "x"/"y"/"z" (既定 "z") または [x,y,z] (原点通過の任意軸)
 *   scale(m, s)           s    = スカラ (均等) または [sx,sy,sz]
 *   mirror(m, axis)       axis = "x"/"y"/"z" (既定 "x") または [nx,ny,nz] (原点通過の平面の法線)
 *   transform(m, matrix)  matrix = 行優先 12 要素 (3x4) または 16 要素 (4x4・最終行は無視)
 *
 * ---- 規約 ----
 *  * 行列は **行優先の 3x4** = { m00 m01 m02 tx  m10 m11 m12 ty  m20 m21 m22 tz }。
 *  * 回転はすべて **原点を通る軸**まわり・右手系・反時計回り (Rodrigues)。
 *  * 鏡像面・スケール中心も **原点**。位置を変えたいなら translate を前後に置く。
 *  * ⚠ 反射 (det<0) では面の向きが裏返る。**向きの補正はカーネル側の責任**
 *    (このヘッダは行列を作るだけ)。det3() を用意してあるので各 apply_affine が見る。
 *
 * ---- エラーの返し方 ----
 * 1 = 成功 / 0 = 失敗。失敗時は *why に理由を置く。書式化が要る文言 (未知の軸名) は
 * 呼び手が渡した buf に組む。⚠ **モジュール側に static を置かない** (ひさ指示 2026-08-26)。
 * in-proc 実行では 1 プロセスに複数 op が同居しうるので、理由を大域に溜めると混線する。
 */

#include "pig/c++/pigData.h"
#include "ts2/c++/stdString.h"
#include <cmath>
#include <string.h>
#include <stdio.h>

namespace srava_affine {

/* M_PI は環境依存 (MSVC/Cygwin の一部で未定義) なのでここで持つ (solids.h と同じ)。 */
const double AFF_PI = 3.14159265358979323846;

/* 恒等 3x4。 */
inline void identity(double e[12]) {
	for ( int i = 0 ; i < 12 ; ++i ) e[i] = 0.0;
	e[0] = e[5] = e[10] = 1.0;
}

/* 線形部 (3x3) の行列式。<0 なら反射 = 面の向きが裏返る。 */
inline double det3(const double e[12]) {
	return e[0] * ( e[5]*e[10] - e[6]*e[9] )
	     - e[1] * ( e[4]*e[10] - e[6]*e[8] )
	     + e[2] * ( e[4]*e[9]  - e[5]*e[8] );
}

/* 点 (x,y,z) に e を適用して o[3] へ。double 座標のカーネル (geogram / cherchi) 用。 */
inline void xform_point(const double e[12], double x, double y, double z, double o[3]) {
	o[0] = e[0]*x + e[1]*y + e[2] *z + e[3];
	o[1] = e[4]*x + e[5]*y + e[6] *z + e[7];
	o[2] = e[8]*x + e[9]*y + e[10]*z + e[11];
}

/* 配列引数の k 番目を double で読む。 */
inline double elem(sPtr<pigDataArray> a, int k) {
	return a->get_ix(thNEW(pigDataInteger,((INTEGER64)k)))->get_flt();
}

/* ---- 点 [x,y,z] の解釈 (#3514) ----------------------------------------------
 * ★ 「点を受け取る op」はカーネルをまたいで増える (distance_at / face_at …) ので、
 *   受け付ける書き方と **拒否の文言**をここに一本化する。2D 向けに [x,y] (z=0) も受ける。
 *   ⚠ 3 成分に満たない配列・配列でない値は明示エラー (黙って 0 を埋めない)。 */
inline int point3(sPtr<pigData> arg, const char* op, double p[3],
                  const char** why, char* buf, int bufsz)
{
	sPtr<pigDataArray> v = arg.is_notNull() ? arg->obt_array() : sPtr<pigDataArray>();
	if ( ! v.is_notNull() || v->length() < 2 ) {
		::snprintf(buf, bufsz, "%s: needs a point [x,y,z]", op);
		*why = buf;
		return 0;
	}
	p[0] = elem(v,0);
	p[1] = elem(v,1);
	p[2] = ( v->length() >= 3 ) ? elem(v,2) : 0.0;
	return 1;
}

/* ---- 内部: 軸 / 法線ベクトルの解釈 ------------------------------------------
 * arg が配列なら [x,y,z] を正規化して u へ。文字列なら "x"/"y"/"z"。
 * arg が無ければ dflt ("x" or "z") を使う。op は文言に混ぜる op 名。
 * what = "axis" / "normal" (エラー文の語)。 */
inline int unit_vector(sPtr<pigData> arg, const char* dflt, const char* op, const char* what,
                       double u[3], const char** why, char* buf, int bufsz)
{
	sPtr<pigDataArray> av = arg.is_notNull() ? arg->obt_array() : sPtr<pigDataArray>();
	if ( av.is_notNull() ) {
		if ( av->length() < 3 ) {
			::snprintf(buf, bufsz, "%s: %s vector needs 3 components [x,y,z]", op, what);
			*why = buf;
			return 0;
		}
		double x = elem(av,0), y = elem(av,1), z = elem(av,2);
		double len = std::sqrt(x*x + y*y + z*z);
		if ( len == 0.0 ) {
			::snprintf(buf, bufsz, "%s: degenerate %s vector [0,0,0]", op, what);
			*why = buf;
			return 0;
		}
		u[0] = x/len; u[1] = y/len; u[2] = z/len;
		return 1;
	}
	const char* s = arg.is_notNull() ? arg->get_str()->get_str() : dflt;
	if      ( ::strcmp(s, "x") == 0 ) { u[0] = 1; u[1] = 0; u[2] = 0; }
	else if ( ::strcmp(s, "y") == 0 ) { u[0] = 0; u[1] = 1; u[2] = 0; }
	else if ( ::strcmp(s, "z") == 0 ) { u[0] = 0; u[1] = 0; u[2] = 1; }
	else {
		::snprintf(buf, bufsz, "%s: unknown %s '%s' (expected \"x\"/\"y\"/\"z\" or [x,y,z])",
		           op, what, s);
		*why = buf;
		return 0;
	}
	return 1;
}

/* ---- translate(m, [x,y,z]) --------------------------------------------------
 * ⚠ 2D 向けに [x,y] (z=0) も受ける。[0,0(,0)] は恒等でエラーにしない。 */
inline int matrix_translate(sPtr<pigData> arg, double e[12],
                            const char** why, char* buf, int bufsz)
{
	sPtr<pigDataArray> v = arg.is_notNull() ? arg->obt_array() : sPtr<pigDataArray>();
	if ( ! v.is_notNull() || v->length() < 2 ) {
		::snprintf(buf, bufsz, "translate: needs a vector [x,y] or [x,y,z]");
		*why = buf;
		return 0;
	}
	identity(e);
	e[3]  = elem(v,0);
	e[7]  = elem(v,1);
	e[11] = ( v->length() >= 3 ) ? elem(v,2) : 0.0;
	return 1;
}

/* ---- rotate(m, axis, deg) ---------------------------------------------------
 * Rodrigues の回転行列 (原点通過の任意軸 u まわり・右手系・反時計回り)。主軸はその特例。
 * ⚠ cos/sin は double の近似なので、EPECK のカーネルでも **回転は厳密ではない**
 *   (厳密なのは「近似された行列を厳密に適用すること」まで)。 */
inline int matrix_rotate(sPtr<pigData> axisArg, double deg, double e[12],
                         const char** why, char* buf, int bufsz)
{
	double u[3];
	if ( ! unit_vector(axisArg, "z", "rotate", "axis", u, why, buf, bufsz) ) return 0;
	double rad = deg * AFF_PI / 180.0;
	double c = std::cos(rad), s = std::sin(rad), C = 1.0 - c;
	double ux = u[0], uy = u[1], uz = u[2];
	e[0]  = c + ux*ux*C;     e[1]  = ux*uy*C - uz*s;  e[2]  = ux*uz*C + uy*s;  e[3]  = 0.0;
	e[4]  = uy*ux*C + uz*s;  e[5]  = c + uy*uy*C;     e[6]  = uy*uz*C - ux*s;  e[7]  = 0.0;
	e[8]  = uz*ux*C - uy*s;  e[9]  = uz*uy*C + ux*s;  e[10] = c + uz*uz*C;     e[11] = 0.0;
	return 1;
}

/* ---- scale(m, s | [sx,sy,sz]) -----------------------------------------------
 * 0 倍は立体が潰れるのでエラー。負の倍率は許す (反射になる = det<0)。 */
inline int matrix_scale(sPtr<pigData> arg, double e[12],
                        const char** why, char* buf, int bufsz)
{
	double sx, sy, sz;
	sPtr<pigDataArray> av = arg.is_notNull() ? arg->obt_array() : sPtr<pigDataArray>();
	if ( av.is_notNull() ) {
		if ( av->length() < 3 ) {
			::snprintf(buf, bufsz, "scale: vector needs 3 components [sx,sy,sz]");
			*why = buf;
			return 0;
		}
		sx = elem(av,0); sy = elem(av,1); sz = elem(av,2);
	} else {
		sx = sy = sz = arg.is_notNull() ? arg->get_flt() : 1.0;   /* 均等スケール */
	}
	if ( sx == 0.0 || sy == 0.0 || sz == 0.0 ) {
		::snprintf(buf, bufsz, "scale: degenerate (zero) scale factor");
		*why = buf;
		return 0;
	}
	identity(e);
	e[0] = sx; e[5] = sy; e[10] = sz;
	return 1;
}

/* ---- mirror(m, axis | [nx,ny,nz]) -------------------------------------------
 * Householder 鏡像行列 H = I - 2 n nᵀ (原点通過の平面・単位法線 n)。det = -1。 */
inline int matrix_mirror(sPtr<pigData> axisArg, double e[12],
                         const char** why, char* buf, int bufsz)
{
	double n[3];
	if ( ! unit_vector(axisArg, "x", "mirror", "normal", n, why, buf, bufsz) ) return 0;
	double nx = n[0], ny = n[1], nz = n[2];
	e[0]  = 1 - 2*nx*nx;  e[1]  = -2*nx*ny;     e[2]  = -2*nx*nz;     e[3]  = 0.0;
	e[4]  = -2*ny*nx;     e[5]  = 1 - 2*ny*ny;  e[6]  = -2*ny*nz;     e[7]  = 0.0;
	e[8]  = -2*nz*nx;     e[9]  = -2*nz*ny;     e[10] = 1 - 2*nz*nz;  e[11] = 0.0;
	return 1;
}

/* ---- transform(m, matrix) ---------------------------------------------------
 * 行優先 12 (3x4) または 16 (4x4・最終行 0,0,0,1 は読み飛ばす)。 */
inline int matrix_transform(sPtr<pigData> arg, double e[12],
                            const char** why, char* buf, int bufsz)
{
	sPtr<pigDataArray> mat = arg.is_notNull() ? arg->obt_array() : sPtr<pigDataArray>();
	int nm = mat.is_notNull() ? mat->length() : 0;
	if ( nm != 12 && nm != 16 ) {
		::snprintf(buf, bufsz,
		           "transform: matrix must have 12 (3x4) or 16 (4x4) elements");
		*why = buf;
		return 0;
	}
	for ( int i = 0 ; i < 12 ; ++i )
		e[i] = elem(mat, i);
	/* ★ #3516: **特異行列を弾く** (scale の 0 倍と同じ理由)。線形部の det が 0 だと立体が
	 *   平面・直線・点へ潰れる = 立体でなくなる。
	 *   ⚠ 弾かないとカーネルごとに壊れ方が割れる。実測 (2026-09-12):
	 *     nef は **SIGFPE で agent ごと死に** (CGAL が有理数の逆行列を作るところ)、
	 *     cgal / manifold / geogram は「体積 0 の立体」を黙って返していた。
	 *   ★ ここは 7 カーネル共通の解釈層なので、1 箇所で全部に効く。
	 *   ⚠ 負の det (反射) は**正当**なので通す。弾くのは厳密に 0 のときだけ。 */
	if ( det3(e) == 0.0 ) {
		::snprintf(buf, bufsz,
		           "transform: singular matrix (the linear part has determinant 0); "
		           "it would flatten the solid onto a plane, a line, or a point");
		*why = buf;
		return 0;
	}
	return 1;
}

/* ---- 2D (z=0 平面に生きる型) が受けられる変換か (#3518) ----------------------
 * ★ 対象は **平面に生きる 2D** = cg-cross2d / mf-cross2d。
 *   ⚠ occt の oc-face3d は TopoDS_Face (任意の曲面 + (u,v) を切り取るワイヤ) なので
 *     平面の外へ出られる。**この検査を occt に当ててはいけない** (能力を殺す)。
 *
 * 平面上の点 (x,y,0) の行き先の z は  z' = m20*x + m21*y + tz。
 * これが全ての (x,y) で 0 = **z 行が (0,0,·,0)** のときだけ結果は z=0 平面に留まる。
 * ⚠ m02 / m12 / m22 は z=0 に掛かるので効かない。mirror("z") や scale([1,1,-1]) は
 *   2D では恒等であって、**エラーではない**。
 *
 * ★★ なぜ厳密な 0 と比べないか (実測): cos/sin は double 近似なので
 *   rotate("x",180) の m21 = sin(π) = 1.2246e-16 になる。これは平面を平面へ写す
 *   **正当な**変換 (2D では mirror("y") と同じ) なので通さないといけない。
 *   ⇒ 線形部 / 平行移動それぞれの大きさに対する **相対 1e-12** で見る。
 *   意図した面外回転は 1e-9 度でも m21 ≈ 1.7e-11 なので、この閾値で分離できる。
 *
 * 1 = 受けられる / 0 = 面外へ出る。0 のとき *why に理由 (buf に組む)。
 */
inline int keeps_z_plane(const double e[12], const char* op,
                         const char** why, char* buf, int bufsz)
{
	/* 相対誤差の基準。線形部は無次元・平行移動は長さなので **別々に**取る
	 * (混ぜると単位の違うものを 1 つの閾値で見ることになる)。下限 1 は
	 * 「ほぼ恒等な行列で tz だけ 1e-16」を 0 と読むため。 */
	double ls = 1.0, ts = 1.0;
	for ( int i = 0 ; i < 3 ; ++i ) {
		double l0 = std::fabs(e[4*i + 0]), l1 = std::fabs(e[4*i + 1]), l2 = std::fabs(e[4*i + 2]);
		if ( l0 > ls ) ls = l0;
		if ( l1 > ls ) ls = l1;
		if ( l2 > ls ) ls = l2;
		double t = std::fabs(e[4*i + 3]);
		if ( t > ts ) ts = t;
	}
	const double REL = 1e-12;
	if ( std::fabs(e[8]) <= REL*ls && std::fabs(e[9]) <= REL*ls && std::fabs(e[11]) <= REL*ts )
		return 1;

	/* ⚠ 呼び手の buf は 256 バイト。**入り切る長さ**にしておくこと (切れると読めない)。 */
	::snprintf(buf, bufsz,
	    "%s: this would move the 2D region out of the z=0 plane, where this kernel's 2D lives; "
	    "it would silently project it back (matrix z row must be (0,0,*,0), got (%g,%g,*,%g)). "
	    "Only occt can place a 2D region in space",
	    op, e[8], e[9], e[11]);
	*why = buf;
	return 0;
}

/* ---- ★★ #3536: 平面から **正準な枠 (O,U,V)** を作る -------------------------
 * 用途は「置き場所を *面そのもの* で持つ表現 (occt の oc-face3d = TopoDS_Face) を、
 * 「**局所座標 + 枠**」で持つ表現 (cg-cross2d / mf-cross2d) へ渡すとき」。
 * ⇒ いまの唯一の呼び手は occt_mf の polygonize。
 *
 * ★★ **正準** = 枠が *平面だけ* から決まること。面をどう作ったかに依存させない。
 *   OCCT の Geom_Plane は gp_Ax3 (Location + Direction + XDirection) を持つが、これは
 *   **面の作られ方で変わる** (同じ平面でも XDirection が違いうる)。それをそのまま枠にすると
 *   *同じ幾何が 2 通りの局所座標になる* = キャッシュも値も式の書き方で変わる。
 *   ⇒ 平面 (法線と原点からの距離) だけを見て決める。
 *
 * 規約 (★ 軸に平行な法線は **cgal の section の基底表と同じ**・cgMesh3D.cpp):
 *
 *     n ∥ x  →  U=(0,1,0)  V=(0,0,1)     局所 = (y,z)
 *     n ∥ y  →  U=(0,0,1)  V=(1,0,0)     局所 = (z,x)
 *     n ∥ z  →  U=(1,0,0)  V=(0,1,0)     局所 = (x,y)     ← 既定の枠
 *
 * ★★ 3 行とも **右手系** (U x V = +n)。⚠ 2026-09-15 まで n∥y の行だけ左手系 (U=(0,0,-1))
 *   だった — cgal の section の実装がそうなっていて、それに合わせたため。2026-09-14 に指摘が
 *   出たが、当時は表が 3 箇所に手書きされていて **片方だけ直すと経路依存が入る**状態だった。
 *   表を 1 つに畳んだので直せる。⇒ 値が動くので cache_version を上げてある (cgal / occt_mf)。と同じ
 *     一般    →  U = 正規化(a - (a·n)n)  (a は x 軸・|n.x|>=0.9 なら y 軸) ・ V = n×U
 *     O      →  **原点に最も近い平面上の点** = n·(n·pt)
 *
 * ⚠ **法線の符号は正準化する** (最大成分が正になるよう反転)。面の向きは枠に持ち込まない。
 *   ⇒ cgal の section は *切断法線の符号* で U を反転する (-z なら U=(-1,0,0)) ので、
 *     そこだけこちらと食い違う。⚠ ただし **同じ平面の別の枠**なので幾何は 1 ミリも違わず、
 *     mfCross::reexpress / cgMesh2D::reexpress が吸収する (#3526 の 2 段の 1 段目)。
 * ⚠ 軸の判定は **厳密な 0** で行う (keeps_z_plane の相対誤差とは事情が違う)。丸めで
 *   n=(0,1e-17,1) になった平面は一般枝へ落ちるが、そこで軸へ *丸める* と U,V が本当の平面を
 *   張らなくなり、局所座標へ落とす段で **面外成分を黙って捨てる**ことになる。
 *   ⇒ 見た目が不格好でも、一般枝の枠は正しく平面を張る。
 *
 * ★ z=0 平面 (n ∥ z ・原点を通る) なら O=(0,0,0) U=(1,0,0) V=(0,1,0) = **既定の枠**。
 *   ⇒ 従来 z=0 しか扱えなかった呼び手は、局所座標も blob もバイト単位で従来どおりになる。
 *
 * n / pt は正規化前でよい。n が退化 (長さ 0) なら 0 を返す (呼び手が明示エラーにする)。
 *
 * ★★★ #3533 (2026-09-15): **表をここ 1 つに集約した**。以前は同じ表が
 *   @modules/cgal/c++/cgMesh3D.cpp@ の @op_section@ の中に *べた書きで 2 回* (射影用と枠用)、
 *   @modules/manifold/c++/mfaSection.cpp@ に *n∥z の 1 行だけ* 、計 3 箇所にあった。
 *   ⚠ 片方だけ直すと **同じ平面の 2D が経路によって別の局所座標になる**。しかも #3533 で
 *     bbox / centroid を world にしたので、その食い違いは **言語からは観測できない**
 *     (面積も周長も不変・bbox も centroid も world)。⇒ 検査で気づけないまま経路依存が入る。
 *
 * ★ 呼び手ごとに **正当に違う** ところは 2 つだけで、それは @opts@ で明示する:
 *
 *     PLANE_FRAME_CANONICAL     平面だけから決める            occt_mf の polygonize
 *     PLANE_FRAME_ORIENT_BY_N   生の法線の向きに合わせる      cgal の section
 *     PLANE_FRAME_ORIGIN_AT_PT  一般法線のとき原点を pt に    cgal の section
 *
 * ⚠⚠ @ORIENT_BY_N@ と @ORIGIN_AT_PT@ は **枝ごとに効き方が違う** (下のコード参照)。これは
 *   @section@ が歴史的にそう書かれていたのを *意味を変えずに* 移したもので、きれいではない。
 *   ★ だからこそ 1 箇所に集めた — 不揃いが 3 箇所に散っていると「どれが正か」が言えない。
 *   ⇒ ★ ①「n∥y の行だけ左手系」は 2026-09-15 に揃えた (表が 1 つになったので直せた)。
 *     残りの 2 点は **値が動く** ので、揃えるなら別に cache_version が要る:
 *     ② ORIENT_BY_N が軸平行では U を・一般では V を反転する (向きは合うが鏡映の軸が違う)
 *     ③ ORIGIN_AT_PT が一般枝にしか効かない (軸平行枝は元から原点最近点)
 */
enum {
	PLANE_FRAME_CANONICAL    = 0u,
	PLANE_FRAME_ORIENT_BY_N  = 1u,   /* 生の法線が「主成分が負」なら向きを合わせる */
	PLANE_FRAME_ORIGIN_AT_PT = 2u    /* ⚠ 一般法線のときだけ原点を pt にする */
};

inline int plane_frame(const double n_in[3], const double pt[3], unsigned opts,
                       double o[3], double u[3], double v[3])
{
	double n[3] = { n_in[0], n_in[1], n_in[2] };
	const double nl = std::sqrt(n[0]*n[0] + n[1]*n[1] + n[2]*n[2]);
	if ( !(nl > 0) ) return 0;
	for ( int k = 0 ; k < 3 ; ++k ) n[k] /= nl;

	/* 符号の正準化: 絶対値が最大の成分を正にする (同値な法線 ±n を 1 つに畳む)。
	 * ⚠ ここは **不連続** — 2 成分の絶対値が並ぶところで反転が切り替わる。ただし
	 *   「法線 → 平面内の軸」を全方向で連続に選ぶことは原理的にできない (球面上に連続な
	 *   非ゼロ接ベクトル場が無い) ので、どんな規約でも必ずどこかで跳ぶ。
	 *   ★ 跳ぶのは **表現であって答えではない** — world の幾何も面積も周長も連続。 */
	int mx = 0;
	for ( int k = 1 ; k < 3 ; ++k )
		if ( std::fabs(n[k]) > std::fabs(n[mx]) ) mx = k;
	const int neg = ( n[mx] < 0 ) ? 1 : 0;   /* ★ 生の法線が「裏向き」か (ORIENT_BY_N が使う) */
	if ( neg ) for ( int k = 0 ; k < 3 ; ++k ) n[k] = -n[k];
	int axis = -1;

	if ( n[1] == 0 && n[2] == 0 ) {            /* n ∥ x : 局所 = (y,z) */
		axis = 0;
		u[0] = 0; u[1] = 1; u[2] = 0;
		v[0] = 0; v[1] = 0; v[2] = 1;
	} else if ( n[0] == 0 && n[2] == 0 ) {     /* n ∥ y : 局所 = (z,x) */
		axis = 1;
		u[0] = 0; u[1] = 0; u[2] = 1;
		v[0] = 1; v[1] = 0; v[2] = 0;
	} else if ( n[0] == 0 && n[1] == 0 ) {     /* n ∥ z : 局所 = (x,y) */
		axis = 2;
		u[0] = 1; u[1] = 0; u[2] = 0;
		v[0] = 0; v[1] = 1; v[2] = 0;
	} else {                                   /* 一般 */
		const double a0 = ( std::fabs(n[0]) < 0.9 ) ? 1.0 : 0.0;
		const double a1 = ( std::fabs(n[0]) < 0.9 ) ? 0.0 : 1.0;
		const double an = a0*n[0] + a1*n[1];
		u[0] = a0 - an*n[0]; u[1] = a1 - an*n[1]; u[2] = -an*n[2];
		const double ul = std::sqrt(u[0]*u[0] + u[1]*u[1] + u[2]*u[2]);
		if ( !(ul > 0) ) return 0;
		for ( int k = 0 ; k < 3 ; ++k ) u[k] /= ul;
		v[0] = n[1]*u[2] - n[2]*u[1];
		v[1] = n[2]*u[0] - n[0]*u[2];
		v[2] = n[0]*u[1] - n[1]*u[0];
	}
	/* ★ ORIENT_BY_N: 生の法線が裏向きなら向きを合わせる。
	 *   ⚠ 軸平行枝は **U** を・一般枝は **V** を反転する。どちらも「生の法線に対して右手系」に
	 *     なるが、鏡映する軸が違うので **同じ枠にはならない**。これは cgal の section の
	 *     既存の振る舞い (ax/flip と「生の n で v を作る」) をそのまま写したもの。 */
	if ( ( opts & PLANE_FRAME_ORIENT_BY_N ) && neg ) {
		if ( axis >= 0 ) for ( int k = 0 ; k < 3 ; ++k ) u[k] = -u[k];
		else             for ( int k = 0 ; k < 3 ; ++k ) v[k] = -v[k];
	}
	/* 原点。既定は **平面上で原点に最も近い点**。
	 * ⚠ ORIGIN_AT_PT は **一般法線のときだけ** pt を原点にする — 軸平行枝では
	 *   n(n·pt) が「pt の軸成分だけを残したもの」= section の fo と元から一致するため。 */
	if ( ( opts & PLANE_FRAME_ORIGIN_AT_PT ) && axis < 0 ) {
		for ( int k = 0 ; k < 3 ; ++k ) o[k] = pt[k];
	} else {
		const double d = n[0]*pt[0] + n[1]*pt[1] + n[2]*pt[2];   /* 原点から平面までの符号つき距離 */
		for ( int k = 0 ; k < 3 ; ++k ) o[k] = n[k] * d;
	}
	/* ★★ **負のゼロを潰す**。法線を反転すると 0 の成分が -0.0 になり、値としては同じ
	 *   (-0.0 == 0.0 は真) なのに **枠を書き出したバイト列が変わる** (符号ビットだけ違う)。
	 *   ⇒ 「同じ平面なら同じ枠」を *バイトで* 言えるようにするため、ここで +0.0 に揃える。
	 *   ⚠ 2026-09-15 に実際に踏んだ: 表を 1 箇所へ集約したとき、値は全 9 方向で一致したのに
	 *     ブロブが 4 方向で変わった。原因がこれ。★ 突き合わせを `!=` でやっていると
	 *     -0.0 == 0.0 なので **検査をすり抜ける** — 代理で見ていた形。 */
	for ( int k = 0 ; k < 3 ; ++k ) {
		if ( o[k] == 0.0 ) o[k] = 0.0;
		if ( u[k] == 0.0 ) u[k] = 0.0;
		if ( v[k] == 0.0 ) v[k] = 0.0;
	}
	return 1;
}

/* 平面だけから決まる枠 (従来の名前・@opts@ 無しの呼び出し)。 */
inline int plane_frame_canonical(const double n_in[3], const double pt[3],
                                 double o[3], double u[3], double v[3])
{
	return plane_frame(n_in, pt, PLANE_FRAME_CANONICAL, o, u, v);
}

/* ---- ★★ #3533: DXF の **任意軸アルゴリズム** (Arbitrary Axis Algorithm) ------------------
 * DXF は LWPOLYLINE の載る平面を押し出し方向 210/220/230 (法線) + elevation 38 で表し、頂点の
 * 10/20 は **その OCS の座標**。⚠ OCS の X 軸は **選べない** — 法線から下の規約で一意に決まる。
 *
 *     |Nx| < 1/64 かつ |Ny| < 1/64  →  Ax = Wy x N   (Wy = (0,1,0))
 *     それ以外                      →  Ax = Wz x N   (Wz = (0,0,1))
 *     Ay = N x Ax   (どちらも正規化)
 *
 * ⚠ これは @plane_frame@ の表とは **別物** — 規約が DXF 側で決まっているので寄せられない。
 *   ★ ただし「平面 → 軸」であることは同じなので **同じファイルに置く**。書き手 (cgMesh2D の
 *     write_dxf) と読み手 (cgaImport の parse_dxf) が **同じ実装を共有する**のが目的
 *     (片方だけ直すと書いたものが読めなくなる。#3533 で表を 1 箇所に畳んだのと同じ理由)。
 * n は正規化前でよい。退化 (長さ 0) なら 0 を返す。 */
inline int dxf_ocs_axes(const double n_in[3], double n[3], double ax[3], double ay[3])
{
	double nl = std::sqrt(n_in[0]*n_in[0] + n_in[1]*n_in[1] + n_in[2]*n_in[2]);
	if ( !(nl > 0) ) return 0;
	for ( int k = 0 ; k < 3 ; ++k ) n[k] = n_in[k] / nl;
	const double R = 1.0/64.0;
	double w[3];
	if ( std::fabs(n[0]) < R && std::fabs(n[1]) < R ) { w[0]=0; w[1]=1; w[2]=0; }
	else                                              { w[0]=0; w[1]=0; w[2]=1; }
	ax[0] = w[1]*n[2] - w[2]*n[1];
	ax[1] = w[2]*n[0] - w[0]*n[2];
	ax[2] = w[0]*n[1] - w[1]*n[0];
	double al = std::sqrt(ax[0]*ax[0] + ax[1]*ax[1] + ax[2]*ax[2]);
	if ( !(al > 0) ) return 0;
	for ( int k = 0 ; k < 3 ; ++k ) ax[k] /= al;
	ay[0] = n[1]*ax[2] - n[2]*ax[1];
	ay[1] = n[2]*ax[0] - n[0]*ax[2];
	ay[2] = n[0]*ax[1] - n[1]*ax[0];
	return 1;
}

/* ★ #3533: 枠が **軸に平行** (U,V の成分が 0 / ±1 ちょうど) か。真なら局所座標への射影を
 *   厳密カーネルのまま取れる (内積の係数が 0/±1 なので丸めが入らない)。 */
inline int frame_is_axis_aligned(const double u[3], const double v[3])
{
	for ( int k = 0 ; k < 3 ; ++k ) {
		if ( u[k] != 0.0 && u[k] != 1.0 && u[k] != -1.0 ) return 0;
		if ( v[k] != 0.0 && v[k] != 1.0 && v[k] != -1.0 ) return 0;
	}
	return 1;
}

}  /* namespace srava_affine */

#endif
