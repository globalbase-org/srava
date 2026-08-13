/*
 * cgaTransform — transform(mesh, matrix) の計算本体(ptsCalcBody 派生)= 低レベル一般アフィン変換。
 * args=[mesh(cgMesh), matrix(array)]。matrix は行優先の 12 要素(3x4 アフィン m00..m23)または
 * 16 要素(4x4。最終行 0,0,0,1 は無視)。各要素 double を K::FT に格納(EPECK 座標のまま近似)。
 * 反射(det<0)は cga_apply_affine が向き反転で補正。要素数不正は result にエラーを立て A_ERROR。
 * 高レベルの translate/rotate/mirror はこの一般変換の特例。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"cg/c++/cgMesh.h"
#include	"cg/c++/ptscgWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/cgaTransform_.h"

CLASS_TINYSTATE(cg/c++/cgaTransform,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	cgaTransform_(
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


cgaTransform_::cgaTransform_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
cgaTransform_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<cgMesh> in = ( na > 0 ) ? sPtr<cgMesh>::d_cast((*args)[0]) : sPtr<cgMesh>();
	sPtr<pigDataArray> mat = ( na > 1 ) ? (*args)[1]->obt_array()
	                                    : sPtr<pigDataArray>();

	int nm = mat.is_notNull() ? mat->length() : 0;
	if ( nm != 12 && nm != 16 ) {
		result = thNEW(pigDataError,(thNEW(stdString,(
		    "transform: matrix must have 12 (3x4) or 16 (4x4) elements"))));
		mesh = thNEW(cgMesh3D,());
		return;
	}

	/* 行優先で m00..m23 の 12 要素を取り出す(16 要素なら最終行を読み飛ばす)→ 3D 多態 apply_affine へ。 */
	double e[12];
	for ( int i = 0 ; i < 12 ; ++i )
		e[i] = mat->get_ix(thNEW(pigDataInteger,((INTEGER64)i)))->get_flt();

	mesh = ( in.is_notNull() ) ? in->apply_affine(e) : sPtr<cgMesh>();
}

/* この演算の結果 (#3406, 2026-07-30 メモ: get_body/get_result を統一)。エラー時は
 * compute() が result にエラー値を残して mesh を未設定のまま return するので result 優先。
 * 成功時は result=thNULL のままmeshを返す。保存(Writer 起動)は agent が出力 pigDataCache
 * の set_body 経由で行う。 */
sPtr<pigData>
cgaTransform_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
