/*
 * cgaPart — part(v, i) の op — **2D の片** (#3525)。
 *   ★ nef の @part@ (3D・凸片) と **同じ名前・違う型**。op 表は型で分かれるので同居する
 *     (1 つの op 名に 2 つの実装は既に普通の形 — #3528 の @points@ と同じ)。
 *   ★ 約束は「**その値が構造として持っている片**の i 番目」。@nparts@ と同じ列を同じ順で見る。
 *     ⇒ @voronoi@ のセルは *サイト順* に並んでいるので @part(voronoi(p,box), i)@ は
 *       *サイト i のセル* になる (cgVoronoi.cpp が並べ替えを 1 回もしないことで保たれる)。
 *   ⚠ 索引の意味は **値の作られ方で決まる** (#3527)。voronoi のように定義で決まるものと、
 *     実装依存のものがある — 後者はキャッシュに焼き付くので、版が変われば同じ式が別の片を返す。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"cg/c++/cgMesh.h"
#include	"cg/c++/ptscgWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/cgaPart_.h"
#include	<stdio.h>

CLASS_TINYSTATE(cg/c++/cgaPart,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	cgaPart_(
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


cgaPart_::cgaPart_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
cgaPart_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	int idx = ( na > 1 ) ? (int)(*args)[1]->get_int() : 0;
	sPtr<cgMesh2D> in2 = ( na > 0 ) ? sPtr<cgMesh2D>::d_cast((*args)[0]) : sPtr<cgMesh2D>();
	if ( in2.is_notNull() ) {
		/* ★ #3545: 片の数は op_topology (2D は nparts = regions の数) から取る
		 *   — regions() は箱の中へ入ったので op からは見えない。 */
		int n = 0;
		in2->op_topology(0, &n, 0);
		if ( idx < 0 || idx >= n ) {
			char b[160];
			::snprintf(b, sizeof b, "part: index %d is out of range (the value has %d part(s))", idx, n);
			result = cga_err(thNEW(stdString,(b)));
			return;
		}
		mesh = sPtr<cgMesh>::d_cast(in2->op_part(idx));
		if ( ! mesh.is_notNull() )
			result = cga_err(thNEW(stdString,("part: could not extract the part")));
		return;
	}
	/* ★ 3D。片リストを持つ値 (voronoi の 3D 等) は **値が持つ順序**で・持たない値は
	 *   面の連結成分で取り出す。⚠ 空洞が在る形は入れ子が要るので op 側が理由つきで断る。 */
	sPtr<cgMesh3D> in3 = ( na > 0 ) ? sPtr<cgMesh3D>::d_cast((*args)[0]) : sPtr<cgMesh3D>();
	if ( ! in3.is_notNull() ) {
		result = cga_err(thNEW(stdString,("part: needs a mesh or a 2D region")));
		return;
	}
	const char *why = 0;
	mesh = sPtr<cgMesh>::d_cast(in3->op_part(idx, &why));
	if ( ! mesh.is_notNull() ) {
		char b[288];
		::snprintf(b, sizeof b, "part: %s", why ? why : "could not extract the part");
		result = cga_err(thNEW(stdString,(b)));
	}
}

/* この演算の結果。エラー時は compute() が result にエラー値を残して mesh 未設定で return するので
 * result 優先。保存 (Writer 起動) は agent が出力 pigDataCache の set_body 経由で行う。 */
sPtr<pigData>
cgaPart_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
