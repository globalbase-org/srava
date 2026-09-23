/*
 * guaVolume — volume(m) — gu-mesh3d の符号つき体積 (#3527)。値返し。
 *   ★ 中身は src/h/common/meshprops.h の volume() — 発散定理。外殻は正・空洞は負なので、
 *     中空の箱は「外殻 − 空洞」が素直に出る (cgal の PMP::volume / Manifold::Volume() と同じ約束)。
 *
 * ★★ なぜ geomutils が volume を持つのか (#3527・2026-09-17):
 *   part(mf-mesh3d, i) の返りは **gu-mesh3d** になる。ところが volume を gu-mesh3d に対して
 *   答えるモジュールが 1 つも無いと、この設計の中心にある検定
 *       Σ volume(part(m,i)) == volume(m)   (符号なし)
 *       Σ volume(shell(m,i)) == volume(m)  (符号つきでのみ成立)
 *   が **書けない**。⇒ 自分が産んだ型の体積は自分で答える。
 *   ⚠ 他カーネルの型 (mf/gg/ch-mesh3d) は **名乗らない** — あちらは自前のライブラリで
 *     答えており (mfMesh::op_volume() は m_.Volume())、meshprops 経由ではないので
 *     「寄せる」対象ではない。
 *
 * ⚠⚠ **閉じていないメッシュに体積は無い**。meshprops の volume() は閉性を見ずに数を返すので、
 *   ここで門を掛ける。掛けないと開いた / 平たいメッシュにも *意味の無い値* が流れる
 *   (cgaVolume.cpp がまったく同じ理由で同じ門を置いている。2026-09-15 の実測:
 *    同一平面の delaunay = 平たい三角形の集まりに 6.666… を返していた)。
 * ⚠ **空集合は 0**。門に掛けない — ブールの結果が空になるのは普通のことで volume(A &&& B) は 0。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"gu/c++/guGeom.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/guaVolume_.h"

CLASS_TINYSTATE(gu/c++/guaVolume,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	guaVolume_(
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

guaVolume_::guaVolume_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}

/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
guaVolume_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<guMesh> in = ( na > 0 ) ? sPtr<guMesh>::d_cast((*args)[0]) : sPtr<guMesh>();
	if ( ! in.is_notNull() ) {
		result = gua_err(thNEW(stdString,("volume: needs a 3D mesh (2D has no volume — use area)")));
		return;
	}
	const srava_mesh::TriView v = in->view();
	if ( v.nt > 0 && ! srava_mesh::topology(v).closed ) {
		result = gua_err(thNEW(stdString,(
		    "volume: the mesh is not closed, so it does not bound a volume "
		    "(check valid(m); an open or flat mesh has area but no volume)")));
		return;
	}
	result = thNEW(pigDataFloat,(srava_mesh::volume(v)));
}
