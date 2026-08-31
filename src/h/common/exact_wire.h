#ifndef ___common_exact_wire_h___
#define ___common_exact_wire_h___

/*
 * exact_wire.h — CGAL の「厳密境界」wire 形式 ("MESH" / nef の境界形式) を **CGAL 非依存** で
 * 読むための共通パーサ (ヘッダオンリー・カーネル非依存)。
 *
 * cgal.so が書く厳密メッシュは座標を有理数の**十進文字列** ("p/q" もしくは整数) で持つ。
 * これを読む側 (manifold / geogram など、内部表現が double のカーネル) は CGAL をリンク
 * しなくても文字列を double へ落とせる = **昇格読み codec の実体**。同じパーサを
 * manifold.so が抱え込んでいたのを、geogram.so からも使えるようここへ切り出した
 * (2026-08-19・#3435 P3 の残タスク)。
 *
 * framing (呼び手が組み立てる。ここが提供するのは「1 個取り出す」部品だけ):
 *   3D "MESH": [u32 nv][u32 nf] / 頂点×nv (x,y,z = 各 str_field) / 面×nf ([u32 nidx][u32 idx]…)
 *   2D "PLY2": [u32 nregions] / region ごとに 外周 ring + [u32 nholes] + 穴 ring 群
 *   いずれも末尾に色/ガイドの section が続くことがあるが、**必要バイトだけ pull** して
 *   残りは reader が閉じる (読み飛ばしてよい)。
 *
 * Source は `void pull(uint8_t *dst, int n)` を持つ型なら何でもよい (mfChunkSource /
 * ggChunkSource は別クラスだが同一シグネチャなので、共通の基底を導入せずテンプレートで受ける)。
 *
 * ★桁あふれについて: CGAL の有理数は分子・分母とも平気で 100 桁を超える。`strtod` に丸ごと
 *   渡すと inf/0 になるので、仮数を 18 桁で打ち切って 10 の指数を別に持ち、
 *   (mp/mq)·10^(ep-eq) で合成する。厳密→double の丸めは最後の 1 回だけになる。
 */

#include <string>
#include <cmath>
#include <stdint.h>

namespace srava_exact {

/* little-endian u32 (全対象 LE 前提・wire 形式の共通約束)。 */
template <class Source>
inline uint32_t get_u32(Source &s)
{
	uint8_t b[4];
	s.pull(b, 4);
	return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

/* [u32 len][len byte] の可変長文字列 (座標 1 成分の有理数表記)。 */
template <class Source>
inline std::string get_str_field(Source &s)
{
	uint32_t len = get_u32(s);
	std::string str;
	str.resize(len);
	if ( len > 0 ) s.pull((uint8_t*)&str[0], (int)len);
	return str;
}

/* 十進整数文字列 → 仮数 (|m| < 1e18) と 10 の指数 e10。value = m · 10^e10。桁あふれしない。 */
inline double strdec_scaled(const std::string &s, int &e10)
{
	int i = 0, sign = 1;
	if ( i < (int)s.size() && (s[i]=='-' || s[i]=='+') ) { if ( s[i]=='-' ) sign = -1; i++; }
	std::string d = s.substr(i);
	size_t nz = d.find_first_not_of('0');
	if ( nz == std::string::npos ) { e10 = 0; return 0.0; }
	d = d.substr(nz);
	int total = (int)d.size();
	int keep = ( total < 18 ) ? total : 18;
	double m = 0.0;
	for ( int k = 0 ; k < keep ; ++k ) m = m * 10.0 + (double)(d[k] - '0');
	e10 = total - keep;
	return sign * m;
}

/* "p/q" (または整数) → double。巨大な p,q でも (mp/mq)·10^(ep-eq) で桁あふれを避ける。 */
inline double parse_rational_d(const std::string &s)
{
	size_t slash = s.find('/');
	if ( slash == std::string::npos ) {
		int e; double m = strdec_scaled(s, e);
		return m * ::pow(10.0, (double)e);
	}
	int en, ed;
	double mn = strdec_scaled(s.substr(0, slash), en);
	double md = strdec_scaled(s.substr(slash + 1), ed);
	if ( md == 0.0 ) return 0.0;
	return (mn / md) * ::pow(10.0, (double)(en - ed));
}

/* 座標 1 成分 = 可変長文字列を取って double 化する (上の 2 つの合成・呼び手の定型)。 */
template <class Source>
inline double get_rational_d(Source &s)
{
	return parse_rational_d(get_str_field(s));
}

}   /* namespace srava_exact */

#endif
