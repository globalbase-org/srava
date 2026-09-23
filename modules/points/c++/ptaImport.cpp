/*
 * ptaImport — import("scan.xyz") の計算本体 (#3528)。
 *
 * ★ import に **新しい op 名は要らない**。import/export は既に「拡張子で型を決める」仕組みを
 *   持っていて (記述子の import_exts が型つき CSV "ext:出力型"・pigModule.h:152)、
 *   points.so が "xyz:pt-cloud3d" と申告するだけで routing がここへ来る。
 * ⚠⚠ 拡張子だけでは型が決まらない形式は入れない — PLY は**メッシュにも純粋な点群にもなりうる**のに
 *   型は routing の段 (ext_type_in_csv) で決まるので中身を見てから選べない。しかも ply は既に
 *   cgal が ply:cg-mesh3d と申告済み。off も面 0 個なら同じ問題。⇒ 曖昧でない xyz だけ。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"pt/c++/ptCloud.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ptaImport_.h"

CLASS_TINYSTATE(pt/c++/ptaImport,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ptaImport_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<ptCloud>	cloud;
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


ptaImport_::ptaImport_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
ptaImport_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<stdString> path = ( na > 0 ) ? (*args)[0]->get_str()
	                                  : sPtr<stdString>(thNEW(stdString,("")));
	char err[512];
	err[0] = '\0';
	cloud = ptCloud::read_xyz(path->get_str(), err, (int)sizeof err);
	if ( ! cloud.is_notNull() )
		result = pta_err(thNEW(stdString,(( err[0] != '\0' ) ? err : "import: failed")));
}

/* この演算の結果。エラー時は result 優先。保存は agent が出力 pigDataCache 経由で行う。 */
sPtr<pigData>
ptaImport_::get_result()
{
	return ( result != thNULL ) ? result : sPtr<pigData>::d_cast(cloud);
}
