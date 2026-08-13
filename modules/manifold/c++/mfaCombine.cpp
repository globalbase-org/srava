/*
 * mfaCombine — combine(a, b) の計算本体(mf 版・cgaCombine のミラー)。ブールなしの単純合体(viewer 用 +++)。
 * 3D(mfMesh)= Manifold::Compose / 2D(mfCross)= CrossSection::Compose。文法が配列/多引数をペアワイズに畳む。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"mf/c++/mfMesh.h"
#include	"mf/c++/ptsmfWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	<vector>
#include	"common/colorspec.h"   /* DEFAULT_GRAY (無色側の既定色) */
#include	"_ts2/c++/mfaCombine_.h"

CLASS_TINYSTATE(mf/c++/mfaCombine,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	mfaCombine_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<mfGeom>	geom;
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
class mfGeom;
TS_END_INTERFACE

#endif


mfaCombine_::mfaCombine_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
mfaCombine_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<pigData> a = ( na > 0 ) ? (*args)[0] : sPtr<pigData>();
	sPtr<pigData> b = ( na > 1 ) ? (*args)[1] : sPtr<pigData>();
	sPtr<mfMesh>  a3 = sPtr<mfMesh>::d_cast(a),  b3 = sPtr<mfMesh>::d_cast(b);
	sPtr<mfCross> a2 = sPtr<mfCross>::d_cast(a), b2 = sPtr<mfCross>::d_cast(b);
	if ( a3.is_notNull() && b3.is_notNull() ) {
		/* ★色の保持: 片方だけが色を持つ場合、無色側に既定の灰を入れてから Compose する。
		 * Manifold は numProp の違う入力を **0 で padding** するので、そのまま合成すると
		 * 無色側が真っ黒になってしまう。cgMesh3D::op_combine が無色側を灰 (180) にするのと
		 * 同じ見え方に揃える (色指定表と既定灰は common/colorspec.h)。 */
		sPtr<mfMesh> ma = a3, mb = b3;
		if ( ma->has_color() && ! mb->has_color() )
			mb = mb->op_color(srava_color::DEFAULT_GRAY, srava_color::DEFAULT_GRAY, srava_color::DEFAULT_GRAY);
		else if ( ! ma->has_color() && mb->has_color() )
			ma = ma->op_color(srava_color::DEFAULT_GRAY, srava_color::DEFAULT_GRAY, srava_color::DEFAULT_GRAY);
		std::vector<manifold::Manifold> v;
		v.push_back(ma->manifold()); v.push_back(mb->manifold());
		geom = thNEW(mfMesh,(manifold::Manifold::Compose(v)));
	} else if ( a2.is_notNull() && b2.is_notNull() ) {
		std::vector<manifold::CrossSection> v;
		v.push_back(a2->cross()); v.push_back(b2->cross());
		geom = thNEW(mfCross,(manifold::CrossSection::Compose(v)));
	} else {
		result = thNEW(pigDataError,(thNEW(stdString,("combine: incompatible operands (mixed dimension?)"))));
	}
}

/* この演算の結果 (#3406, 2026-07-30 メモ: get_body/get_result を統一)。エラー時は
 * compute() が result にエラー値を残して geom を未設定のまま return するので result 優先。
 * 成功時は result=thNULL のままgeomを返す。保存(Writer 起動)は agent が出力 pigDataCache
 * の set_body 経由で行う。 */
sPtr<pigData>
mfaCombine_::get_result()
{
	return ( result != thNULL ) ? result : geom;
}
