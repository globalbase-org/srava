/*
 * ocaCast — cast("oc-cross2d", 2D) — **名乗りだけ**を下げる (#3544)。
 *
 * ★★ 規約② (docs/srava_language_reference.md#two-2d-types):
 *   「降格 (face3d → cross2d) は **cast だけ**。対象が本当に z=0 平面に居るときだけ通り、
 *     傾いていれば **明示エラー**。cast は幾何を 1 ミリも動かさない。」
 *
 * ⚠ 幾何は触らない。動かしたいなら project_flatten (z=0 へ落とす) が別の op として在る。
 * ⚠ 昇格 (cross2d → face3d) は cast を通さなくてよい — 一般表現のほうが広いので、
 *   transform 系が勝手に face3d を返す (規約①)。ここでは同型 cast として素通しする。
 *
 * ★★ #3544 (案 i・ひさ判断 2026-09-17): occt の名乗りは **幾何から導く**ので、この op は
 *   「値を作り替える」ものではなく **通すか断るかを決めるだけ**になった。
 *   ⇒ 降格は「本当に z=0 に居るか」を確かめる関門・昇格は素通し。どちらも値は identity。
 *   ⚠ 昇格した値に *それ自身の* 名乗りを訊けば、幾何が z=0 なら依然 oc-cross2d と答える。
 *     食い違って見えるが、routing が見るのは sig のスタンプなので op の選択は規約①どおり。
 *     ★ ここで名乗りを上書きできる形にすると「幾何と名乗りが離れた値」が作れてしまい、
 *       案 i が塞いだ穴がそこから開く。⇒ 上書きの口は **置かない**。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"oc/c++/ocShape.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ocaCast_.h"

#include	<Standard_Failure.hxx>
#include	<string>
#include	<cstring>

CLASS_TINYSTATE(oc/c++/ocaCast,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ocaCast_(
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


ocaCast_::ocaCast_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
ocaCast_::compute()
{
	ocShape::ensure_init();
	int na = ( args != 0 ) ? args->length() : 0;
	/* ⚠ 引数の並びは他カーネルの cast と同じ — (型名, 値)。 */
	sPtr<stdString> tn = ( na > 0 ) ? (*args)[0]->get_str() : sPtr<stdString>();
	const char *want = tn.is_notNull() ? tn->get_str() : "";
	sPtr<ocFace2D> in = ( na > 1 ) ? sPtr<ocFace2D>::d_cast((*args)[1]) : sPtr<ocFace2D>();
	if ( ! in.is_notNull() || in->shape().IsNull() ) {
		result = oca_err(thNEW(stdString,("cast: input must be a 2D region (oc-cross2d / oc-face3d)")));
		return;
	}
	const int toCross = ( ::strcmp(want, OC2C_TYPE) == 0 );
	const int toFace  = ( ::strcmp(want, OC2_TYPE)  == 0 );
	if ( ! toCross && ! toFace ) {
		char m[224];
		snprintf(m, sizeof m, "cast: occt can only name a 2D region \"%s\" or \"%s\", not \"%s\"",
		    OC2C_TYPE, OC2_TYPE, ( want[0] != '\0' ) ? want : "(empty)");
		result = oca_err(thNEW(stdString,(m))); return;
	}
	try {
		/* ★★ 規約②: 降格は **本当に z=0 に居るときだけ**。幾何は動かさない。 */
		if ( toCross && ! in->on_z0_plane() ) {
			result = oca_err(thNEW(stdString,(
			    "cast: this 2D region is placed off the z=0 plane, so it cannot be named "
			    "\"oc-cross2d\" (the z=0 representation); cast never moves geometry — "
			    "use project_flatten(...) to drop it onto z=0, or transform it back first")));
			return;
		}
		/* ★★ #3544 (案 i): 値は **そのまま返す (identity)**。
		 *   名乗りは幾何から導かれる (ocShape.h の type_name) ので、通す/断るを決めた時点で
		 *   この op の仕事は終わっている — 名乗りを書き換える入れ物は要らない。
		 *   ⚠ 「入れ物を作ってビットを立てる」形に戻すと、*幾何が z=0 の外なのに cross2d を
		 *     名乗る値* を作る道が復活する。cast は幾何を動かさないのだから、
		 *     ここに矛盾を作れる余地が在ること自体が誤り。 */
		out = in;
	} catch ( const Standard_Failure& e ) {
		std::string m = std::string("cast: OCCT failed [") + e.DynamicType()->Name() + "] (" +
		    ( ( e.GetMessageString() && e.GetMessageString()[0] ) ? e.GetMessageString() : "no message" ) + ")";
		result = oca_err(thNEW(stdString,(m.c_str())));
		return;
	}
}

sPtr<pigData>
ocaCast_::get_result()
{
	return ( result != thNULL ) ? result : sPtr<pigData>::d_cast(out);
}
