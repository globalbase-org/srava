/*
 * cgaGenus — genus(mesh) — 種数 (取っ手の総数) (#3514)。値返し。
 *   ★ 3 つの数 (nshells / nparts / genus) は **1 回の走査で同時に**出るので、cgMesh は
 *     op_topology() 1 本で 3 つとも返す。op を 3 本に分けているのは *訊きたいものだけ訊ける*
 *     ようにするため (名前が答えを説明する)。
 *   ★ 定義は src/h/common/meshprops.h の冒頭 — シェル = 面の連結成分 / 塊 = 立体の連結成分
 *     (= 符号つき体積が正のシェル) / 種数 = 取っ手の総数。nef の nparts と同じ約束。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"cg/c++/cgMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/cgaGenus_.h"

CLASS_TINYSTATE(cg/c++/cgaGenus,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	cgaGenus_(
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
class cgMesh;
TS_END_INTERFACE

#endif


cgaGenus_::cgaGenus_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
cgaGenus_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<cgMesh> in = ( na > 0 ) ? sPtr<cgMesh>::d_cast((*args)[0]) : sPtr<cgMesh>();
	if ( ! in.is_notNull() ) {
		result = cga_err(thNEW(stdString,("genus: needs a mesh")));
		return;
	}
	int nshells = 0, nparts = 0, genus = 0;
	int closed = in->op_topology(&nshells, &nparts, &genus);
	/* ⚠ 閉じていないメッシュでは chi = 2-2g が成り立たないので、**黙って整数を返さない**。
	 *   (shells / parts は境界があっても数として意味を持つのでこの門は要らない。) */
	if ( ! closed ) {
		result = cga_err(thNEW(stdString,(
		    "genus: the mesh is not a closed 2-manifold (genus is undefined; check valid(m))")));
		return;
	}
	result = thNEW(pigDataInteger,((INTEGER64)genus));
}
