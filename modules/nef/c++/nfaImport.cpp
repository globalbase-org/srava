/*
 * nfaImport — import(path) — 外部メッシュ読み込み (STL / OFF) の計算本体 (nef 版・#3474)。
 * ★ 読み手は common/meshio.h (カーネル非依存・manifold の自前パーサを共有化)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"nf/c++/nfMesh.h"
#include	"nf/c++/nfTriSink.h"
#include	"common/meshio.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/nfaImport_.h"

CLASS_TINYSTATE(nf/c++/nfaImport,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	nfaImport_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<nfNefMesh>	mesh;
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
class nfNefMesh;
TS_END_INTERFACE

#endif


nfaImport_::nfaImport_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
nfaImport_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<stdString> path = ( na > 0 ) ? (*args)[0]->get_str()
	                                  : sPtr<stdString>(thNEW(stdString,("")));

	nfTriSink sink;
	if ( ! srava_geo::read_mesh_file(path->get_str(), sink) ) {
		sPtr<stdString> msg = thNEW(stdString,("import: failed to read (STL/OFF only) "));
		result = nfa_err(msg->add(path));
		return;
	}
	mesh = sink.finish();
}

/* この演算の結果。エラー時は compute() が result にエラー値を残して本体未設定で return するので
 * result 優先。保存 (Writer 起動) は agent が出力 pigDataCache の set_body 経由で行う。 */
sPtr<pigData>
nfaImport_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
