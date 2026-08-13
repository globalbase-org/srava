#ifndef ___common_colorspec_h___
#define ___common_colorspec_h___

/*
 * colorspec.h — color(mesh, c) の **色指定の解釈** (ヘッダオンリー・カーネル非依存)。
 *
 * cgal.so (cgaColor) と manifold.so (mfaColor) の両方が include する。色の *持ち方* は
 * カーネルごとに違う (cgal = per-face property map "f:color" / manifold = 頂点プロパティ ch3..5)
 * が、**受け付ける書き方と名前表は同一**でなければならないので、そこだけを共有する。
 *
 *   受理する形: 名前 ("red"/"green"/…・大小無視) / "#RRGGBB" / [r,g,b] (0-255 の配列)
 *
 * pigData には依存する (引数はスクリプトの値) が、幾何カーネルには依存しない。
 */

#include "pig/c++/pigData.h"
#include "ts2/c++/stdString.h"
#include <string.h>

namespace srava_color {

/* 16 進 1 桁。失敗は -1。 */
inline int hexval(char c) {
	if ( c >= '0' && c <= '9' ) return c - '0';
	if ( c >= 'a' && c <= 'f' ) return c - 'a' + 10;
	if ( c >= 'A' && c <= 'F' ) return c - 'A' + 10;
	return -1;
}

/* 色指定 spec を RGB(0-255) へ。1=成功 / 0=不明。 */
inline int parse_spec(sPtr<pigData> spec, int& r, int& g, int& b) {
	if ( spec == thNULL ) return 0;
	/* 配列 [r,g,b](0-255) */
	sPtr<pigDataArray> arr = spec->obt_array();
	if ( arr.is_notNull() && arr->length() >= 3 ) {
		r = (int)arr->get_ix(thNEW(pigDataInteger,((INTEGER64)0)))->get_flt();
		g = (int)arr->get_ix(thNEW(pigDataInteger,((INTEGER64)1)))->get_flt();
		b = (int)arr->get_ix(thNEW(pigDataInteger,((INTEGER64)2)))->get_flt();
		return 1;
	}
	/* 文字列: "#RRGGBB" または 名前 */
	sPtr<stdString> s = spec->get_str();
	if ( s == thNULL ) return 0;
	const char *cs = s->get_str();
	if ( cs[0] == '#' && ::strlen(cs) >= 7 ) {
		int h[6];
		for ( int i = 0 ; i < 6 ; ++i ) { h[i] = hexval(cs[i+1]); if ( h[i] < 0 ) return 0; }
		r = h[0]*16 + h[1];  g = h[2]*16 + h[3];  b = h[4]*16 + h[5];
		return 1;
	}
	struct { const char *n; int r, g, b; } tbl[] = {
		{ "red",     255,   0,   0 }, { "green",   0, 160,   0 }, { "blue",    0,   0, 255 },
		{ "yellow",  255, 215,   0 }, { "cyan",    0, 200, 200 }, { "magenta", 230,   0, 230 },
		{ "orange",  255, 140,   0 }, { "purple",  150,  0, 200 }, { "white",  255, 255, 255 },
		{ "black",     0,   0,   0 }, { "gray",  150, 150, 150 }, { "grey",  150, 150, 150 },
	};
	for ( unsigned i = 0 ; i < sizeof(tbl)/sizeof(tbl[0]) ; ++i )
		if ( ::strcasecmp(cs, tbl[i].n) == 0 ) { r = tbl[i].r; g = tbl[i].g; b = tbl[i].b; return 1; }
	return 0;
}

/* combine で片方だけが色を持つときに、無色側へ与える既定色 (cgMesh3D::op_combine と同じ灰)。 */
const int DEFAULT_GRAY = 180;

}  /* namespace srava_color */

#endif
