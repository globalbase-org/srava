/*
 * nfaCast — cast("nf-mesh3d", mesh) — 型変換 op。自型への cast は identity (実変換は codec が担う)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"nf/c++/nfMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/nfaCast_.h"

CLASS_TINYSTATE(nf/c++/nfaCast,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	nfaCast_(
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


nfaCast_::nfaCast_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
nfaCast_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<nfMesh> in = ( na > 1 ) ? sPtr<nfMesh>::d_cast((*args)[1]) : sPtr<nfMesh>();
	if ( ! in.is_notNull() ) {
		result = thNEW(pigDataError,(thNEW(stdString,("cast: needs a mesh"))));
		return;
	}
	mesh = in;
}

/* この演算の結果。エラー時は compute() が result にエラー値を残して mesh 未設定で return するので
 * result 優先。保存 (Writer 起動) は agent が出力 pigDataCache の set_body 経由で行う。 */
sPtr<pigData>
nfaCast_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
