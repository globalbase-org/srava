/*
 * ocaText — text(fontPath, str, size) (#3471)。TrueType / OpenType の字形を
 *   **2D の曲線 (Bezier / B-spline) のまま**取り込み、平面上の Face (oc-cross2d) にする。
 *
 * ★ TrueType → BRep は OCCT に既製品がある: StdPrs_BRepFont (Font_BRepFont.hxx はその typedef)。
 *   ::Init(fontPath, size, resolution) / ::RenderGlyph(Utf32Char) -> TopoDS_Shape。
 *   実装は libTKV3d、内部で FreeType を使う。★ 表示には触らないのでヘッドレスで動く。
 *   ⇒ FT_Outline_Decompose を自前で書く必要は無い。
 *
 * ★★ フォントは **必ずパスで指定する** (ひさ指示)。
 *   StdPrs_BRepFont::FindAndInit は fontconfig でシステムフォント **名**を引くので、
 *   同じ .sra が機械によって違う形を出す。srava は値ベースの DAG でキャッシュするので、
 *   名前だけを引数にすると再現性が壊れる。⇒ FindAndInit は **使わない**。
 *   パースは pigDataFileRef で包んであり (ns_sravaParser.y)、キャッシュキーには
 *   **ファイル内容のハッシュ**が入る (import と同じ content-addressed)。
 *   ⇒ フォントを差し替えればキーが変わり、正しく再計算される。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"oc/c++/ocShape.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ocaText_.h"

#include	<StdPrs_BRepFont.hxx>
#include	<StdPrs_BRepTextBuilder.hxx>
#include	<Font_FontMgr.hxx>
#include	<NCollection_String.hxx>
#include	<TopoDS_Shape.hxx>
#include	<TopoDS_Compound.hxx>
#include	<BRep_Builder.hxx>
#include	<TopExp_Explorer.hxx>
#include	<TopoDS.hxx>
#include	<Standard_Failure.hxx>
#include	<string>

CLASS_TINYSTATE(oc/c++/ocaText,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ocaText_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<ocFace2D>	out;
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
class ocFace2D;
TS_END_INTERFACE

#endif

ocaText_::ocaText_(TS_ARGS0)
	: ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
	TS_CPARGS0
}

static sPtr<pigData> xerr(const std::string& m)
{ return oca_err(thNEW(stdString,(m.c_str()))); }

/* UTF-8 → UTF-32。★ 自前で書く (OCCT の NCollection_String は UTF-8 を受けるが、
 *   1 文字ずつ RenderGlyph へ渡すには符号位置が要る)。不正バイトは U+FFFD にする。 */
static void utf8_to_utf32(const char *s, std::vector<Standard_Utf32Char>& out)
{
	const unsigned char *p = (const unsigned char*)s;
	while ( *p ) {
		unsigned c = *p++;
		int n = 0; unsigned v = 0;
		if      ( c < 0x80 ) { v = c;        n = 0; }
		else if ( (c & 0xE0) == 0xC0 ) { v = c & 0x1F; n = 1; }
		else if ( (c & 0xF0) == 0xE0 ) { v = c & 0x0F; n = 2; }
		else if ( (c & 0xF8) == 0xF0 ) { v = c & 0x07; n = 3; }
		else { out.push_back(0xFFFD); continue; }
		for ( int k = 0 ; k < n ; ++k ) {
			if ( (*p & 0xC0) != 0x80 ) { v = 0xFFFD; break; }
			v = (v << 6) | (*p++ & 0x3F);
		}
		out.push_back((Standard_Utf32Char)v);
	}
}

void
ocaText_::compute()
{
	ocShape::ensure_init();   /* ★ OCCT の診断出力を stdout から外す */
	int na = ( args != 0 ) ? args->length() : 0;
	if ( na < 2 ) { result = xerr("text: needs (fontPath, string[, size])"); return; }
	sPtr<stdString> fp = (*args)[0]->get_str();
	sPtr<stdString> tx = (*args)[1]->get_str();
	if ( ! fp.is_notNull() || ! tx.is_notNull() ) { result = xerr("text: fontPath and string must be strings"); return; }
	double size = ( na > 2 ) ? (*args)[2]->get_flt() : 10.0;
	if ( !(size > 0) ) { result = xerr("text: size must be > 0"); return; }

	std::vector<Standard_Utf32Char> cps;
	utf8_to_utf32(tx->get_str(), cps);
	if ( cps.empty() ) { result = xerr("text: the string is empty"); return; }

	try {
		Handle(StdPrs_BRepFont) font = new StdPrs_BRepFont();
		/* ★ **パスで開く**。FindAndInit (fontconfig の名前引き) は使わない — 再現性のため。 */
		if ( ! font->Init(NCollection_String(fp->get_str()), size, 0) ) {
			result = xerr(std::string("text: cannot open the font file '") + fp->get_str() +
			              "' (give a path to a .ttf/.otf; font names are not accepted "
			              "because the same script must produce the same shape on every machine)");
			return;
		}
		/* 字形を 1 文字ずつ取り、前進量で横に並べる。
		 * ⚠ StdPrs_BRepTextBuilder を使えばカーニング込みのレイアウトになるが、
		 *   まずは 1 行の単純な前進で足りる (縦書き・複数行は将来)。 */
		BRep_Builder bb;
		TopoDS_Compound comp;
		bb.MakeCompound(comp);
		Standard_Real pen = 0.0;
		int nglyph = 0;
		for ( size_t i = 0 ; i < cps.size() ; ++i ) {
			TopoDS_Shape g = font->RenderGlyph(cps[i]);
			if ( ! g.IsNull() ) {
				gp_Trsf t; t.SetTranslation(gp_Vec(pen, 0, 0));
				bb.Add(comp, g.Moved(TopLoc_Location(t)));
				++nglyph;
			}
			pen += font->AdvanceX(cps[i], ( i + 1 < cps.size() ) ? cps[i+1] : 0);
		}
		if ( nglyph == 0 ) {
			result = xerr("text: the font has no glyph for any character in the string "
			              "(a space-only string also lands here: it has no outline)");
			return;
		}
		out = thNEW(ocFace2D,());
		out->set_shape(comp);
	} catch ( const Standard_Failure& e ) {
		result = xerr(std::string("text: OCCT failed [") + e.DynamicType()->Name() + "] (" +
		              ( ( e.GetMessageString() && e.GetMessageString()[0] ) ? e.GetMessageString()
		                                                                    : "no message" ) + ")");
		return;
	}
}

sPtr<pigData>
ocaText_::get_result()
{
	return ( result != thNULL ) ? result : sPtr<pigData>::d_cast(out);
}
