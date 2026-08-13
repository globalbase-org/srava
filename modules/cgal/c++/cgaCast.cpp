/*
 * cgaCast — cast("exact", mesh) の計算本体(ptsCalcBody 派生・#3404)。
 *   カーネル間の **明示的な表現変換**。cg agent(CGAL カーネル)側の cast は、入力メッシュをそのまま
 *   出力する identity。核心は **リーダ側**にある: cg のメッシュリーダ(ptscgWireCacheStreamReaderMesh)は
 *   D_META タグが "MESH"(exact)でも "MFM3"(Manifold)でも読め、MFM3 の場合は double→EPECK 有理数へ
 *   **無損失昇格** して cgMesh3D を作る(cgMesh3D::decode_mfm3)。従って cast は「読んで(=昇格が起きる)
 *   そのまま MESH で書き出す」だけで float→exact 変換が完成する。
 *
 *   引数: args=[type_string(inline・"exact"), mesh(cache)]。type はプランナ(pigfModuleAgent::
 *   decide_out_module)が既にカーネル選択に使っており、ここ(agent)では無視してよい。
 *   既に exact なメッシュを cast("exact", ...) した場合も単なる再エンコード(no-op 相当)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"cg/c++/cgMesh.h"
#include	"cg/c++/ptscgWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/cgaCast_.h"

CLASS_TINYSTATE(cg/c++/cgaCast,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	cgaCast_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<cgMesh>	mesh;
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
class cgMesh;
TS_END_INTERFACE

#endif


cgaCast_::cgaCast_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
cgaCast_::compute()
{
	/* args=[type(inline), mesh(cache)]。mesh は args[1](リーダが MESH/MFM3 どちらも decode 済み・
	 * MFM3 なら EPECK へ無損失昇格済み)。cast はそれをそのまま出力する identity。 */
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<cgMesh> in = ( na > 1 ) ? sPtr<cgMesh>::d_cast((*args)[1]) : sPtr<cgMesh>();
	if ( ! in.is_notNull() ) {
		result = thNEW(pigDataError,(thNEW(stdString,("cast: needs a mesh (2nd arg)"))));
		return;
	}
	mesh = in;   /* identity。保存時の WriterMesh が MESH(exact)で再エンコード = float→exact 変換の実体 */
}

/* この演算の結果 (#3406, 2026-07-30 メモ: get_body/get_result を統一)。エラー時は
 * compute() が result にエラー値を残して mesh を未設定のまま return するので result 優先。
 * 成功時は result=thNULL のままmeshを返す。保存(Writer 起動)は agent が出力 pigDataCache
 * の set_body 経由で行う。 */
sPtr<pigData>
cgaCast_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
