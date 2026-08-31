/*
 * nfaSolidify — solidify(m) — **壊れた境界からソリッドを組み直す** (#3445)。
 *
 *   自己交差した閉メッシュ (自分を貫く tube など) は、cgal/manifold/nef のどれも解決できず、
 *   重なりを二重に数えた値を黙って返していた。この op はそれを
 *   面ごとの Nef の n 項 union → 有界セルの mark で解き直す。
 *
 *   ★入力は nf のみ (@solidify(nf)->nf@ の 1 本)。自己交差した閉メッシュは cg→nf の厳密変換が
 *     **通る** (Nef 構築は面どうしの交差を検査せず、局所の接続だけから SNC を組む) ので、
 *     壊れた形のまま nf に入り、@to_mesh()@ で面が取り出せる = 組み直す材料は nf 側にある。
 *     よって「壊れたまま mesh で受け取る」入口 (nfSoup) は要らない。cg から使うときは
 *     利用者が @solidify(cast("nf-mesh3d", x))@ と書く (約束②: 低→高は cast)。
 *
 *   ★空洞を守るため **連結成分ごとに**組み直し、成分どうしは入れ子の深さで合成する
 *     (@Mark_bounded_volumes@ は有界セルを無差別に塗るので、単純に適用すると空洞が埋まる)。
 *
 *   ★**重い**: 面数に比例して Nef の union を繰り返す (通常の Nef 構築より桁で重い)。
 *     **重いのは再構成**であって
 *     @to_mesh@ ではない (境界の取り出しは無料同然)。だから既定の変換経路には置かず、
 *     利用者が明示的に呼ぶ op にしてある。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"nf/c++/nfMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/nfaSolidify_.h"

CLASS_TINYSTATE(nf/c++/nfaSolidify,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	nfaSolidify_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<nfMesh>	mesh;
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
class nfMesh;
TS_END_INTERFACE

#endif


nfaSolidify_::nfaSolidify_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
nfaSolidify_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<pigData> in = ( na > 0 ) ? (*args)[0] : sPtr<pigData>();
	if ( ! in.is_notNull() ) {
		result = thNEW(pigDataError,(thNEW(stdString,("solidify: needs a mesh"))));
		return;
	}
	mesh = nfMesh::solidify_mesh(in);
	if ( ! mesh.is_notNull() )
		result = thNEW(pigDataError,(thNEW(stdString,
		    ("solidify: could not rebuild a solid from the given boundary"))));
}

/* この演算の結果。エラー時は compute() が result にエラー値を残して mesh 未設定で return するので
 * result 優先。保存 (Writer 起動) は agent が出力 pigDataCache の set_body 経由で行う。 */
sPtr<pigData>
nfaSolidify_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
