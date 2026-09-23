/*
 * cgaVert — vert(m, i) — **i 番目の頂点の座標** を返す値 op (#3527)。
 *
 * ★★ なぜこの名前か (#3527 の規約):
 *   片を持つ値は **3 つ組**で名乗る — 数える @n<片s>@ / 取り出す @<片>@ / 位置で指す @<片>_at@。
 *     nparts → part ・ nfaces → face ・ nshells → shell ・ **nverts → vert**
 *   ⇒ @vertex@ ではなく @vert@。n<X>s から機械的に導けることを優先した (ひさ裁定 2026-09-16)。
 *
 * ★★ なぜ @value@ で返すか:
 *   @bbox@ が @[[x,y,z],[x,y,z]]@ を pigDataArray で返しているので、**1 点の座標に新しい型は
 *   要らない**。⇒ 3D は @[x,y,z]@ ・ 2D は @[x,y]@ (枠の中の座標)。
 *   ⚠ 全頂点をまとめて欲しい場合は別 op (@verts@ → 点群型) の担当。N 万点を
 *     AK_INLINE の値に載せることはできない。
 *
 * ⚠⚠ 索引は **実装依存**。列挙順をなぞるだけなので版・ビルド・入力順で変わりうる。
 *   キャッシュのキーに i が入るので、**同じ式が同じ i で同じ頂点を返し続けること**を
 *   検査で押さえる (test/srava_vert.sh)。⇒ #3527 の「索引には 2 種類ある」の *実装依存* 側。
 *
 * ★ 「座標で返す」のと「構成要素の番号で返す」は **別の約束**なので、1 つの op に
 *   両方を持たせない。番号が要る場面 (連結性) は @face_verts@ が担当する (未配線・#3527)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"cg/c++/cgMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/cgaVert_.h"
#include	<stdio.h>

CLASS_TINYSTATE(cg/c++/cgaVert,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	cgaVert_(
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
TS_END_INTERFACE

#endif


cgaVert_::cgaVert_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
cgaVert_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<cgMesh> in = ( na > 0 ) ? sPtr<cgMesh>::d_cast((*args)[0]) : sPtr<cgMesh>();
	if ( ! in.is_notNull() ) {
		result = cga_err(thNEW(stdString,("vert: needs a mesh or a 2D region")));
		return;
	}
	int idx = ( na > 1 ) ? (int)(*args)[1]->get_int() : 0;
	int n   = in->op_nverts();
	if ( idx < 0 || idx >= n ) {
		char b[160];
		::snprintf(b, sizeof b, "vert: index %d is out of range (the value has %d vertex/vertices)", idx, n);
		result = cga_err(thNEW(stdString,(b)));
		return;
	}
	double p[3] = {0,0,0};
	int dim = in->op_vert(idx, p);
	if ( dim <= 0 ) {
		result = cga_err(thNEW(stdString,("vert: could not read the vertex")));
		return;
	}
	/* ★ 返りの次元ぶんだけ並べる — 3D は [x,y,z] ・ 2D は [x,y] (bbox と同じ約束)。 */
	sPtr<pigDataArray> arr = thNEW(pigDataArray,());
	for ( int i = 0 ; i < dim ; ++i )
		arr->push(thNEW(pigDataFloat,(p[i])));
	result = arr;
}
