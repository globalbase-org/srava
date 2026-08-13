/*
 * cgaPolygon — polygon([[x,y],...]) の計算本体(ptsCalcBody 派生)= 明示点列の単純多角形(2D)。
 * 点列を順に頂点に。時計回りで与えられたら CCW に正規化(外周規約)。退化(< 3 点 / 非単純)はエラー。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"cg/c++/cgMesh.h"
#include	"cg/c++/ptscgWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/cgaPolygon_.h"
#include	<vector>

CLASS_TINYSTATE(cg/c++/cgaPolygon,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	cgaPolygon_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<cgMesh2D>	mesh;
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
class cgMesh2D;
TS_END_INTERFACE

#endif


cgaPolygon_::cgaPolygon_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
cgaPolygon_::compute()
{
	typedef cgMesh::K K;
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<pigDataArray> pts = ( na > 0 ) ? (*args)[0]->obt_array()
	                                    : sPtr<pigDataArray>();
	mesh = thNEW(cgMesh2D,());
	int np = pts.is_notNull() ? pts->length() : 0;
	if ( np < 3 ) {
		result = thNEW(pigDataError,(thNEW(stdString,(
		    "polygon: needs >= 3 points [[x,y],...]"))));
		return;
	}
	/* 連続重複頂点を間引きながら集める。曲線(arc/bezier/…)を concat で繋ぐと継ぎ目で
	 * 「前点 == 次の関数の始点」の完全重複が必ず出る(零長エッジ)。これを残すと多角形が
	 * 非単純(valid=0)になり、offset(straight skeleton)が空になる等の不具合を招くため除去する。
	 * (tube が連続重複点を間引くのと一貫。自己交差そのものは許容=valid/repair で扱う) */
	std::vector<K::Point_2> verts;
	for ( int i = 0 ; i < np ; ++i ) {
		sPtr<pigDataArray> xy = pts->get_ix(thNEW(pigDataInteger,((INTEGER64)i)))->obt_array();
		if ( ! xy.is_notNull() || xy->length() < 2 ) {
			result = thNEW(pigDataError,(thNEW(stdString,(
			    "polygon: each point must be [x,y]"))));
			return;
		}
		double x = xy->get_ix(thNEW(pigDataInteger,((INTEGER64)0)))->get_flt();
		double y = xy->get_ix(thNEW(pigDataInteger,((INTEGER64)1)))->get_flt();
		K::Point_2 pt = K::Point_2(K::FT(x), K::FT(y));
		if ( ! verts.empty() && verts.back() == pt )
			continue;   /* 連続重複(継ぎ目の零長エッジ・点の二重指定)を間引く */
		verts.push_back(pt);
	}
	/* 閉じ重複(末尾 == 先頭。始点を末尾にも書いて閉じた場合)も除去 */
	while ( verts.size() >= 2 && verts.back() == verts.front() )
		verts.pop_back();
	if ( verts.size() < 3 ) {
		result = thNEW(pigDataError,(thNEW(stdString,(
		    "polygon: needs >= 3 distinct points (重複頂点を除くと 3 点未満)"))));
		return;
	}
	cgMesh2D::Polygon_2 p(verts.begin(), verts.end());
	/* 自己交差(非単純)も許容して値として作る(tube が自己交差 3D を作れるのと一貫)。
	 * valid(p)=0 で検出、repair(p) で even-odd 修復できる。
	 * 向き正規化(外周=CCW)は orientation/area が単純多角形を前提とするため、単純な時だけ行う。 */
	if ( p.is_simple() && p.is_clockwise_oriented() )
		p.reverse_orientation();
	mesh->regions().push_back(cgMesh2D::Pwh_2(p));
}

/* この演算の結果 (#3406, 2026-07-30 メモ: get_body/get_result を統一)。エラー時は
 * compute() が result にエラー値を残して mesh を未設定のまま return するので result 優先。
 * 成功時は result=thNULL のままmeshを返す。保存(Writer 起動)は agent が出力 pigDataCache
 * の set_body 経由で行う。 */
sPtr<pigData>
cgaPolygon_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
