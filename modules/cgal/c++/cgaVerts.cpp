/*
 * cgaVerts — verts(m) — **全頂点をまとめて点群で返す** op (#3527)。
 *
 * ★★ なぜ点群型 (pt-cloud3d) で返すか:
 *   ① N 万点は **AK_INLINE の値には載らない** (vert(m,i) は 1 点なので値でよい)。
 *   ② 点群は **カーネル中立で外部ライブラリを持たない型** (#3528) なので、
 *      ここで借りているのは *幾何の機能* ではなく **値の器** だけ。
 *      ⇒ モジュール境界の約束①「他カーネルの機能を借りて自分の顔で出さない」に触れない。
 *      ★ 前例あり — @estimate_normals@ が既に cgal で ptCloud を作って返している。
 *   ③ そのまま @distance@ / @closest@ / @voronoi@ / @delaunay@ が食える。
 *
 * ★ 3 つ組の「**まとめて**」側 (#3527 の規約 ④)。⚠ ④ は「**受け皿の型が在るときだけ作る**」
 *   なので、面や片には作らない (@faces(B)@ / @parts(v)@ は作れない — 列を載せる型が無い)。
 *
 * ⚠⚠ @vert(m,i)@ と **同じ列を同じ順**で並ぶ。⇒ @verts(m)@ の i 番目 == @vert(m,i)@。
 *   片方だけ順序を変えると黙ってずれるので、**検査がこの等式を見る** (test/srava_vert.sh)。
 * ⚠ 2D は **枠の中の (x,y)** を積む (返り 2) ⇒ pt-cloud2d になる。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"cg/c++/cgMesh.h"
#include	"pt/c++/ptCloud.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/cgaVerts_.h"

CLASS_TINYSTATE(cg/c++/cgaVerts,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	cgaVerts_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	/* ⚠⚠ get_result は **public 側に置く**。protected に置くと codegen が
	 *   外側クラスへの **転送を作らない** ので、結果が本体から取り出されず
	 *   「値は作れているのに format 'TEXT' で保存される」になる (2026-09-16 に踏んだ)。
	 *   ★ 対照: cgaEstimateNormals は public 側に置いてある。 */
	virtual sPtr<pigData>	get_result();

protected:
	virtual void		compute();
	sPtr<ptCloud>		cloud;
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
class ptCloud;
TS_END_INTERFACE

#endif


cgaVerts_::cgaVerts_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
cgaVerts_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<cgMesh> in = ( na > 0 ) ? sPtr<cgMesh>::d_cast((*args)[0]) : sPtr<cgMesh>();
	if ( ! in.is_notNull() ) {
		result = cga_err(thNEW(stdString,("verts: needs a mesh or a 2D region")));
		return;
	}
	sPtr<ptCloud> out = thNEW(ptCloud,());
	int dim = in->op_verts(out->xyz());
	if ( dim <= 0 ) {
		result = cga_err(thNEW(stdString,("verts: could not read the vertices")));
		return;
	}
	out->set_dim(dim);
	/* ★ 法線は付けない。@estimate_normals@ が要るなら利用者が明示的に通す。
	 *   ⚠ ここで勝手に付けると「頂点を読んだだけ」のはずの op が重くなる。 */
	cloud = out;
}

/* この演算の結果。エラー時は compute() が result にエラー値を残すので result 優先。 */
sPtr<pigData>
cgaVerts_::get_result()
{
	return ( result != thNULL ) ? result : cloud;
}
