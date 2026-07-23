/*
 * cgaColor — color(mesh, c) の計算本体(ptsCalcBody 派生)。args=[mesh(cgMesh, reader), c(inline)]。
 * c = 名前("red"/"green"/…) / "#RRGGBB" / [r,g,b](0-255) を RGB に解釈し、全面に f:color を付ける
 * (3D 専用・combine で各成分の色が残る)。結果を cgMesh に保持して get_writer() で返す。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"cg/c++/cgMesh.h"
#include	"cg/c++/ptscgWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/cgaColor_.h"

#include	<string.h>
#include	<stdlib.h>

CLASS_TINYSTATE(cg/c++/cgaColor,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	cgaColor_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<ptsWireCacheStreamWriter>	get_writer();
protected:
	virtual void	compute();
	sPtr<cgMesh>	mesh;
private:
	TS_DEFARGS
};

TS_END_IMPLEMENT

TS_BEGIN_INTERFACE
#include	"ts2/c++/sRptr.h"
#include	"ts2/c++/sArray.h"
#include	"ts2/c++/stdString.h"
class ptsObject;
class pigData;
class stdString;
class cgMesh;
class ptsWireCacheStreamWriter;
TS_END_INTERFACE

#endif


cgaColor_::cgaColor_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

/* 16 進 1 桁。失敗は -1。 */
static int hexval(char c) {
	if ( c >= '0' && c <= '9' ) return c - '0';
	if ( c >= 'a' && c <= 'f' ) return c - 'a' + 10;
	if ( c >= 'A' && c <= 'F' ) return c - 'A' + 10;
	return -1;
}

/* 色指定 spec を RGB(0-255)へ。1=成功 / 0=不明。 */
static int parse_color(sPtr<pigData> spec, int& r, int& g, int& b) {
	if ( spec == thNULL ) return 0;
	/* 配列 [r,g,b](0-255) */
	sPtr<pigDataArray> arr = sPtr<pigDataArray>::d_cast(spec);
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

void
cgaColor_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<cgMesh> in   = ( na > 0 ) ? sPtr<cgMesh>::d_cast((*args)[0]) : sPtr<cgMesh>();
	sPtr<pigData> spec = ( na > 1 ) ? (*args)[1] : sPtr<pigData>();
	int r = 150, g = 150, b = 150;
	if ( ! parse_color(spec, r, g, b) ) {
		result = thNEW(pigDataError,(thNEW(stdString,(
		    "color: 2nd arg は名前(\"red\"…) / \"#RRGGBB\" / [r,g,b](0-255) のいずれか"))));
		mesh = thNEW(cgMesh3D,());
		return;
	}
	mesh = ( in.is_notNull() ) ? in->op_color(r, g, b) : sPtr<cgMesh>();   /* 2D は op_color が null=エラー */
}

sPtr<ptsWireCacheStreamWriter>
cgaColor_::get_writer()
{
	return thNEW(ptscgWireCacheStreamWriterMesh,(parent, target, mesh));
}
