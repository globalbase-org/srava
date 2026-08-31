/*
 * nfaTranslate — translate(m, [x,y,z]) の計算本体 (nef 版)。
 * ★Nef のまま Aff_transformation_3 で移動する (型維持・境界表現へ戻さない)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"nf/c++/nfMesh.h"
#include	"ts2/c++/stdString.h"
#include	<CGAL/Aff_transformation_3.h>
#include	"_ts2/c++/nfaTranslate_.h"

CLASS_TINYSTATE(nf/c++/nfaTranslate,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	nfaTranslate_(
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


nfaTranslate_::nfaTranslate_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
nfaTranslate_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<nfMesh> in = ( na > 0 ) ? sPtr<nfMesh>::d_cast((*args)[0]) : sPtr<nfMesh>();
	if ( ! in.is_notNull() ) {
		result = thNEW(pigDataError,(thNEW(stdString,("translate: needs a Nef mesh"))));
		return;
	}
	/* ★配列は d_cast でなく obt_array (map/lambda 由来の遅延ノードも解決される)。 */
	sPtr<pigDataArray> v = ( na > 1 && (*args)[1].is_notNull() ) ? (*args)[1]->obt_array()
	                                                            : sPtr<pigDataArray>();
	if ( ! v.is_notNull() ) {
		result = thNEW(pigDataError,(thNEW(stdString,("translate: needs [x,y,z]"))));
		return;
	}
	double t[3] = { 0.0, 0.0, 0.0 };
	int nd = v->length();
	for ( int k = 0 ; k < 3 && k < nd ; ++k )
		t[k] = v->get_ix(thNEW(pigDataInteger,((INTEGER64)k)))->get_flt();

	typedef CGAL::Aff_transformation_3<nfMesh::K> Aff;
	Aff a(CGAL::TRANSLATION, nfMesh::K::Vector_3(t[0], t[1], t[2]));
	nfMesh::Nef n = in->nef();
	n.transform(a);
	mesh = thNEW(nfMesh,(n));
}

/* この演算の結果。エラー時は compute() が result にエラー値を残して mesh 未設定で return するので
 * result 優先。保存 (Writer 起動) は agent が出力 pigDataCache の set_body 経由で行う。 */
sPtr<pigData>
nfaTranslate_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
