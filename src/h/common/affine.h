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
	return 1;
}

}  /* namespace srava_affine */

#endif
