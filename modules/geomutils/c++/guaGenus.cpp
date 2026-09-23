/*
 * guaGenus — genus(m) (#3527 段 3)。種数 = 取っ手の総数。Euler 標数から (chi = 2 - 2g)。
 *   ★ 中身は src/h/common/meshprops.h — **定義ごと 1 本**。mf / gg / ch はこれまで内部表現から
 *     素の配列へ写して同じヘッダを通していた。⇒ ここへ寄せても *答えは変わらない*。
 * ⚠ 閉じていないメッシュでは chi と種数の関係が成り立たない ⇒ **明示エラー**にする
 *   (黙って意味の無い整数を返さない)。2D にも無い。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"gu/c++/guGeom.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/guaGenus_.h"

CLASS_TINYSTATE(gu/c++/guaGenus,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	guaGenus_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

protected:
	virtual void	compute();
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

guaGenus_::guaGenus_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
guaGenus_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<guGeom> in = ( na > 0 ) ? sPtr<guGeom>::d_cast((*args)[0]) : sPtr<guGeom>();
	if ( ! in.is_notNull() ) {
		result = gua_err(thNEW(stdString,("genus: needs a mesh or 2D region")));
		return;
	}
	int genus = 0;
	if ( ! in->op_topology(0, 0, &genus) ) {
		result = gua_err(thNEW(stdString,(
		    /* ⚠ 文言は cgaGenus と **一字一句そろえる** — test/srava_topology.sh が
		     *   "not a closed 2-manifold" を grep して検定している。寄せたときに
		     *   言い換えると *同じ約束なのにテストだけ落ちる* (2026-09-17 に実際に踏んだ)。 */
		    "genus: the mesh is not a closed 2-manifold (genus is undefined; check valid(m))")));
		return;
	}
	result = thNEW(pigDataInteger,((INTEGER64)genus));
}
