/*
 * ptaTransform — transform(points, matrix) の計算本体 (#3578 ・ 2026-09-22)。点群版。
 * ★ 行列の z 行が (0,0,*,0) なら平面を保つ (2D のまま)。判定は相対 1e-12 —
 *   rotate("x",180) の m21 = sin(pi) = 1.2e-16 は **正当な平面→平面**なので通す。
 * ★★ 結果の次元 (2D のままか 3D へ出るか) は **routing が行を選んだときと同じ述語**が決める
 *   (pt/c++/ptAffine.h の pt_affine_out_dim)。ここで独自に判定すると「sig は pt-cloud2d と
 *   言っているのに 3 次元が返る」が起きる (#3554 段5a で実際に踏んだ形)。
 *
 * 中身 (座標と法線の写し替え) は共通 (pt_cloud_affine)。ここは引数を読むだけ。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"pt/c++/ptCloud.h"
#include	"pt/c++/ptAffine.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ptaTransform_.h"

CLASS_TINYSTATE(pt/c++/ptaTransform,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ptaTransform_(
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


ptaTransform_::ptaTransform_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
ptaTransform_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	if ( na < 2 ) {
		result = pta_err(thNEW(stdString,("transform: needs (points, matrix)")));
		return;
	}
	sPtr<ptCloud> in = sPtr<ptCloud>::d_cast((*args)[0]);
	if ( ! in.is_notNull() ) {
		result = pta_err(thNEW(stdString,("transform: needs a point cloud")));
		return;
	}
	sPtr<pigData> arg = (*args)[1];
	double deg = 0.0;
	double e[12];
	const char *why = 0;
	char buf[256];
	/* ★ 引数の解釈と拒否の理由は common/affine.h (7 カーネル共通・#3486)。 */
	if ( ! pt_affine_matrix(PT_AFF_TRANSFORM, arg, deg, e, &why, buf, (int)sizeof buf) ) {
		result = pta_err(thNEW(stdString,(why)));
		return;
	}
	int outDim = pt_affine_out_dim(in->dim(), PT_AFF_TRANSFORM, arg);
	char err[256];
	err[0] = '\0';
	cloud = pt_cloud_affine(in, e, outDim, "transform", err, (int)sizeof err);
	if ( ! cloud.is_notNull() )
		result = pta_err(thNEW(stdString,(err)));
}

/* この演算の結果。エラー時は compute() が result にエラー値を残して本体未設定で return するので
 * result 優先 (ptaPoints3D と同じ)。 */
sPtr<pigData>
ptaTransform_::get_result()
{
	return ( result != thNULL ) ? result : sPtr<pigData>::d_cast(cloud);
}
