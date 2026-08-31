/*
 * nfaPart — part(mesh, i) — **i 番目の塊** を返す (0 始まり・#3441 追補)。
 *   塊 = SNC の marked volume。@nparts(m)@ で数を得て、これで 1 つずつ取り出す。
 *   ★用途: 凸分解した片を個別に扱う (物理エンジンの凸コリジョン形状など)。
 *     @var n = nparts(d); part(d, 0)@ … のように使う。
 *   空洞を持つ塊は空洞つきのまま出る (その volume の全シェルから作るため)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"nf/c++/nfMesh.h"
#include	"ts2/c++/stdString.h"
#include	<stdio.h>
#include	<CGAL/Aff_transformation_3.h>
#include	"_ts2/c++/nfaPart_.h"

CLASS_TINYSTATE(nf/c++/nfaPart,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	nfaPart_(
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
#include	<stdio.h>
class ptsObject;
class pigData;
class stdString;
class nfMesh;
TS_END_INTERFACE

#endif


nfaPart_::nfaPart_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
nfaPart_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<nfMesh> in = ( na > 0 ) ? sPtr<nfMesh>::d_cast((*args)[0]) : sPtr<nfMesh>();
	if ( ! in.is_notNull() ) {
		result = thNEW(pigDataError,(thNEW(stdString,("part: needs a Nef mesh"))));
		return;
	}
	int idx = ( na > 1 ) ? (int)(*args)[1]->get_int() : 0;
	int n   = in->op_nparts();
	if ( idx < 0 || idx >= n ) {
		char b[160];
		::snprintf(b, sizeof b, "part: index %d is out of range (the mesh has %d part(s))", idx, n);
		result = thNEW(pigDataError,(thNEW(stdString,(b))));
		return;
	}
	mesh = in->op_part(idx);
	if ( ! mesh.is_notNull() )
		result = thNEW(pigDataError,(thNEW(stdString,("part: could not extract the part"))));
}

/* この演算の結果。エラー時は compute() が result にエラー値を残して mesh 未設定で return するので
 * result 優先。保存 (Writer 起動) は agent が出力 pigDataCache の set_body 経由で行う。 */
sPtr<pigData>
nfaPart_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
