/*
 * cgaProjectFlatten — project_flatten(area2d) の計算本体 (#3534)。
 *   world 座標の (x,y) をそのまま取り z を捨てる = **z=0 平面への直投影**。
 *
 * ★★ なぜ「実形のまま寝かせる」ではないか (ひさ 2026-09-14・#3534 の決定)
 *   「実形のまま寝かせる」は **原理的に正準化できない** — 同じ図形の表裏は図形自体からは
 *   決まらず、法線から枠を決める *連続な* 規約も存在しない (毛玉の定理)。どんな規約にも
 *   「わずかに傾けただけで結果が跳ぶ場所」が必ずできる。
 *   ⇒ 規約で決めるのをやめ、**world 幾何だけで決まる射影**にした = 経路非依存。
 *   実形が欲しい利用者は *先に transform で XY と平行にしてから* これを当てる
 *   (表裏もそのとき利用者が明示的に決める)。
 *
 * ⚠ @project@ (#3518・平面図形を曲面へ投影して切る) とは別の op。紛れないよう複合語。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"cg/c++/cgMesh.h"
#include	"cg/c++/ptscgWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/cgaProjectFlatten_.h"

CLASS_TINYSTATE(cg/c++/cgaProjectFlatten,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	cgaProjectFlatten_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

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
TS_END_INTERFACE

#endif


cgaProjectFlatten_::cgaProjectFlatten_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
cgaProjectFlatten_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<cgMesh> in = ( na > 0 ) ? sPtr<cgMesh>::d_cast((*args)[0]) : sPtr<cgMesh>();
	if ( ! in.is_notNull() ) {
		result = cga_err(thNEW(stdString,("project_flatten: needs a 2D region")));
		return;
	}
	sPtr<cgMesh2D> c2 = sPtr<cgMesh2D>::d_cast(in);
	if ( ! c2.is_notNull() ) {
		result = cga_err(thNEW(stdString,("project_flatten: needs a 2D region")));
		return;
	}
	sPtr<cgMesh2D> out = c2->project_flatten();
	if ( ! out.is_notNull() ) {
		/* ★ 文言は manifold / occt と **同じ**にしてある — 同じ状況なので。
		 *   ⚠ 片方だけ直さないこと (同じ式を書いた利用者が別の説明を読むことになる)。 */
		result = cga_err(thNEW(stdString,(
		    "project_flatten: the 2D region stands on a plane that contains world +Z, so "
		    "its shadow on z=0 collapses to a line; rotate the region so its plane is not "
		    "parallel to the projection direction")));
		return;
	}
	mesh = sPtr<cgMesh>::d_cast(out);
}

sPtr<pigData>
cgaProjectFlatten_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
