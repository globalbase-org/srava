/*
 * mfaImport — import(path) の計算本体(mf 版・cgaImport の 3D 部分のミラー)。
 * STL(binary/ascii)・OFF を Manifold へ読み込む(mfMesh::import_file・CGAL 非依存の自前パーサ)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"mf/c++/mfMesh.h"
#include	"mf/c++/ptsmfWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/mfaImport_.h"

CLASS_TINYSTATE(mf/c++/mfaImport,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	mfaImport_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<mfMesh>	mesh;
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
class mfMesh;
TS_END_INTERFACE

#endif


mfaImport_::mfaImport_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
mfaImport_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<stdString> path = ( na > 0 ) ? (*args)[0]->get_str() : sPtr<stdString>(thNEW(stdString,("")));
	mesh = mfMesh::import_file(path->get_str());
	if ( ! mesh.is_notNull() ) {
		sPtr<stdString> msg = thNEW(stdString,("import: failed to read (STL/OFF only) "));
		result = thNEW(pigDataError,(msg->add(path)));
	}
}

/* この演算の結果 (#3406, 2026-07-30 メモ: get_body/get_result を統一)。エラー時は
 * compute() が result にエラー値を残して mesh を未設定のまま return するので result 優先。
 * 成功時は result=thNULL のままmeshを返す。保存(Writer 起動)は agent が出力 pigDataCache
 * の set_body 経由で行う。 */
sPtr<pigData>
mfaImport_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
