/*
 * guaCast — cast("gu-…", v) — 中立型への入口 (#3527 段 2)。
 *
 * ★★ なぜ gu に cast が要るのか: 型には **入口**が要る。段 2 で 5 カーネルの cast に
 *   (gu-*) の行を足して *出口* を作ったが、入口が無いと gu の値を作る手段が 1 つも無く、
 *   足した行も volume も **一度も走らせられない** (実測で確認した)。⇒ 宣言だけが先に立つ。
 *   ★ cast は「表現力の高→低の落下は cast のみ」という規約上の正規の入口でもある。
 *
 * ★ 中身は **identity**。実体化はすべて guGeom::WIRE が済ませている:
 *     MFM3 (mf / gg / ch / gu 自身) … そのまま読む
 *     MFC2 (mf / gu 自身)           … そのまま読む
 *     MESH / PLY2 (cgal の厳密)     … decode_*_exact が有理数 → double へ落として読む
 *   ⇒ この op は「読めた値をそのまま自分の顔で出す」だけで、幾何は 1 ミリも動かさない。
 *
 * ⚠⚠ #3533 規約②: **降格 (gu-face3d → gu-cross2d) だけは identity ではない**。
 *   「空間に置かれた 2D」を「z=0 の簡易表現」と名乗り直す操作なので、*本当に z=0 に居るとき*
 *   しか許さない。⇒ frame_is_default() が偽なら明示エラー (cgaCast / mfaCast と同じ門)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"gu/c++/guGeom.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/guaCast_.h"
#include	<string.h>

CLASS_TINYSTATE(gu/c++/guaCast,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	guaCast_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<guGeom>	geom;
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
class guGeom;
TS_END_INTERFACE

#endif

guaCast_::guaCast_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}

/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
guaCast_::compute()
{
	/* args=[type(inline), geom(cache)]。geom は WIRE が既に decode 済み。 */
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<guGeom> in = ( na > 1 ) ? sPtr<guGeom>::d_cast((*args)[1]) : sPtr<guGeom>();
	if ( ! in.is_notNull() ) {
		result = gua_err(thNEW(stdString,("cast: needs a mesh or 2D region (2nd arg)")));
		return;
	}
	geom = in;   /* identity */

	/* ★★ #3533 規約②: 降格 (face3d → cross2d) は「本当に z=0 に居るとき」だけ。 */
	sPtr<guPoly> p = sPtr<guPoly>::d_cast(in);
	if ( p.is_notNull() && na > 0 ) {
		sPtr<pigData> tv = (*args)[0];
		const char *tname = ( tv.is_notNull() && tv->get_str() != thNULL )
		                  ? tv->get_str()->get_str() : "";
		if ( ::strcmp(tname, GU_TYPE_2D) == 0 && p->is_placed() ) {
			if ( ! p->frame_is_default() ) {
				result = gua_err(thNEW(stdString,(
				    "cast: this 2D region is placed on another plane, so it cannot be named "
				    "\"" GU_TYPE_2D "\" (the z=0 representation); cast never moves geometry — "
				    "use project_flatten(...) to drop it onto z=0, or transform it back first")));
				geom = thNULL;
				return;
			}
			/* 幾何はそのまま・**名乗りだけ**下げる (共有されうる値なので複製する)。 */
			sPtr<guPoly> out = thNEW(guPoly,());
			out->xy()      = p->xy();
			out->ringLen() = p->ringLen();
			out->set_frame(p->frame_o(), p->frame_u(), p->frame_v());
			out->set_placed(0);
			geom = sPtr<guGeom>::d_cast(out);
		}
	}
}

/* エラー時は compute() が result にエラーを残して geom 未設定で return するので result 優先。 */
sPtr<pigData>
guaCast_::get_result()
{
	return ( result != thNULL ) ? result : sPtr<pigData>::d_cast(geom);
}
