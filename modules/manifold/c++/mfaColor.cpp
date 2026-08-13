/*
 * mfaColor — color(mesh, c) の計算本体(mf 版・cgaColor のミラー)。
 *   c = 名前("red"/"green"/…) / "#RRGGBB" / [r,g,b](0-255) を RGB に解釈し、全体に色を付ける。
 *   3D 専用(2D 断面に色の概念は持たせていない。cgal 版も 2D はエラー)。
 *
 * ★色の持ち方は cgal と違う: cgal は Surface_mesh の per-face property map "f:color"、mf は
 *   **Manifold の頂点プロパティ** (numProp=6 の ch3..5 = RGB)。color() は全頂点を同色にするので
 *   成分ごとに一様になり、combine (Compose) で成分ごとの色が残る = cgal と同じ見え方になる。
 *   色つき export (3MF/AMF) は「三角形の第 1 隅の色」を面色として書く。
 *   色の解釈表 (名前・#RRGGBB・[r,g,b]) は cgaColor と同一のものを common/colorspec.h で共有する。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"mf/c++/mfMesh.h"
#include	"mf/c++/ptsmfWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	"common/colorspec.h"   /* 色指定の解釈 (cgaColor と共通) */
#include	"_ts2/c++/mfaColor_.h"

CLASS_TINYSTATE(mf/c++/mfaColor,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	mfaColor_(
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


mfaColor_::mfaColor_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
mfaColor_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<mfMesh>  in   = ( na > 0 ) ? sPtr<mfMesh>::d_cast((*args)[0]) : sPtr<mfMesh>();
	sPtr<pigData> spec = ( na > 1 ) ? (*args)[1] : sPtr<pigData>();

	int r = 150, g = 150, b = 150;
	if ( ! srava_color::parse_spec(spec, r, g, b) ) {
		result = thNEW(pigDataError,(thNEW(stdString,(
		    "color: 2nd arg は名前(\"red\"…) / \"#RRGGBB\" / [r,g,b](0-255) のいずれか"))));
		return;
	}
	if ( ! in.is_notNull() ) {
		result = thNEW(pigDataError,(thNEW(stdString,("color: needs a 3D mesh (2D は非対応)"))));
		return;
	}
	mesh = in->op_color(r, g, b);
}

/* この演算の結果 (#3406, 2026-07-30 メモ: get_body/get_result を統一)。エラー時は
 * compute() が result にエラー値を残して mesh を未設定のまま return するので result 優先。
 * 成功時は result=thNULL のままmeshを返す。保存(Writer 起動)は agent が出力 pigDataCache
 * の set_body 経由で行う。 */
sPtr<pigData>
mfaColor_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
