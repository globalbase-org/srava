/*
 * cgaVolume — volume(mesh) の計算本体(ptsCalcBody 派生)= 体積を返す**値返し op**。
 *   3D=囲む体積(閉メッシュ)。2D は体積なし → エラー(area を使う)。dim() でディスパッチ。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"cg/c++/cgMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/cgaVolume_.h"

CLASS_TINYSTATE(cg/c++/cgaVolume,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	cgaVolume_(
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


cgaVolume_::cgaVolume_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
cgaVolume_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<cgMesh> in = ( na > 0 ) ? sPtr<cgMesh>::d_cast((*args)[0]) : sPtr<cgMesh>();
	if ( ! in.is_notNull() ) {
		result = cga_err(thNEW(stdString,("volume: needs a mesh")));
		return;
	}
	if ( in->dim() != 3 ) {
		result = cga_err(thNEW(stdString,("volume: 2D has no volume (use area)")));
		return;
	}
	/* ⚠⚠ #3525: **閉じていないメッシュに体積は無い**。@PMP::volume@ は閉じていなくても
	 *   落ちずに数を返すので、黙って通すと *意味の無い値*が下流へ流れる
	 *   (2026-09-15 実測: 同一平面の delaunay = 平たい三角形の集まりに 6.666… を返していた)。
	 *   ⇒ @genus@ が「閉じた 2-多様体でない」を断るのとまったく同じ門を置く。
	 *   ★ closed は op_topology が返す (数え直しの 1 パスが増えるが、volume 自体が O(n))。 */
	/* ⚠ **空集合は 0**。門に掛けてはいけない (ブールの結果が空になるのは普通のことで、
	 *   srava_empty_set / srava_affine2d が @volume(A &&& B)@ に 0 を期待している)。
	 *   ⇒ 断るのは「面はあるのに閉じていない」= 本当に体積が定義できない形だけ。 */
	if ( in->op_nfaces() > 0 && ! in->op_topology(0, 0, 0) ) {
		result = cga_err(thNEW(stdString,(
		    "volume: the mesh is not closed, so it does not bound a volume "
		    "(check valid(m); an open or flat mesh has area but no volume)")));
		return;
	}
	result = thNEW(pigDataFloat,(in->op_volume()));
}
