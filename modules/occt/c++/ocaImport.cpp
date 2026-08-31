/*
 * ocaImport — import(path) の計算本体 (#3437)。**STEP (.step/.stp) と .brep を読む**。
 *
 * ★★ これは「mesh → B-rep」ではない。STEP も BREP も**解析曲面をそのまま持っている**形式
 *   なので、読むだけで B-rep が手に入る (復元も推定もしない)。三角形群から解析曲面を復元する
 *   reverse engineering は別種の問題であり、そちらの入口は作らない方針のまま変えない。
 * ★ これで occt は**外から実物の CAD データを受け取れる**ようになる。それまでは自前の
 *   box / sphere / cylinder / torus から組む以外に B-rep を得る手段が無かった。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"oc/c++/ocShape.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ocaImport_.h"


CLASS_TINYSTATE(oc/c++/ocaImport,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ocaImport_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<ocShape>	out;
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
class ocShape;
TS_END_INTERFACE

#endif


ocaImport_::ocaImport_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
ocaImport_::compute()
{
	ocShape::ensure_init();   /* ★ OCCT の診断出力を stdout から外す (ocShape.h 参照) */
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<stdString> path = ( na > 0 ) ? (*args)[0]->get_str()
	                                  : sPtr<stdString>(thNEW(stdString,("")));
	out = ocShape::read_file(path->get_str());
	if ( ! out.is_notNull() ) {
		/* ★ 部分的に読めた形を黙って返さない。読めなければ明示エラー。 */
		sPtr<stdString> msg = thNEW(stdString,("import: cannot read as B-rep (occt supports step/stp/brep) "));
		result = thNEW(pigDataError,(msg->add(path)));
	}
}

sPtr<pigData>
ocaImport_::get_result()
{
	return ( result != thNULL ) ? result : out;
}
