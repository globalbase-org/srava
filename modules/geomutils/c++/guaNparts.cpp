/*
 * guaNparts — nparts(m) (#3527 段 3)。塊 = **立体の連結成分**の数。3D は符号つき体積が正のシェル・2D は符号つき面積が正のリング。
 *   ★ 中身は src/h/common/meshprops.h / ringprops.h — **定義ごと 1 本**。mf / gg / ch はこれまで内部表現から
 *     素の配列へ写して同じヘッダを通していた。⇒ ここへ寄せても *答えは変わらない*。
 * ★★ 3D と 2D は相似形 — どちらも **符号が判別子**で、包含判定は要らない。
 *   ⚠ *取り出す* (part) には入れ子が要る。そこが段 4。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"gu/c++/guGeom.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/guaNparts_.h"

CLASS_TINYSTATE(gu/c++/guaNparts,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	guaNparts_(
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

guaNparts_::guaNparts_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
guaNparts_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<guGeom> in = ( na > 0 ) ? sPtr<guGeom>::d_cast((*args)[0]) : sPtr<guGeom>();
	if ( ! in.is_notNull() ) {
		result = gua_err(thNEW(stdString,("nparts: needs a mesh or 2D region")));
		return;
	}
	int nparts = 0;
	in->op_topology(0, &nparts, 0);
	result = thNEW(pigDataInteger,((INTEGER64)nparts));
}
