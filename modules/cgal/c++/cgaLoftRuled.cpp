/*
 * cgaLoftRuled — loft_ruled(断面, 断面, …) (#3511)。断面間を **直線で結ぶ** (線織面)。
 *
 * ★★ 断面の **置き場所は op が決めない**。利用者が @transform@ で空間に置いた 2D を
 *   そのまま受ける (occt / manifold と同じ規約)。cgal でこれが書けるようになったのは
 *   #3526 で cg-cross2d が **枠 (平面)** を持ったから。
 *
 *     loft_ruled(rect(2,3), translate(rect(2,3),[0,0,5]))                   まっすぐな角柱
 *     loft_ruled(circle(1,0), translate(rotate(circle(1,0),"x",20),[0,0,4]))  傾いた断面も置ける
 *
 * ★ なめらかな方 (@loft@) は **置かない** — 解析曲面が要るのでメッシュ系には無い。
 *   #3511 で 2 つを別 op にした理由そのもの (#3510 の表に出る差)。
 *
 * 実体は @cg_loft_ruled_from_args@ (cgLoft.cpp)。対応づけの規約は **manifold と同一**で、
 * 但し書きもそちら (mfMesh.cpp) が本文。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"cg/c++/cgMesh.h"
#include	"cg/c++/cgaBoolError.h"
#include	"cg/c++/ptscgWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	<stdio.h>
#include	"_ts2/c++/cgaLoftRuled_.h"

CLASS_TINYSTATE(cg/c++/cgaLoftRuled,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	cgaLoftRuled_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<cgMesh>	mesh;     /* compute() の union 結果(get_result が agent へ返す) */
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


cgaLoftRuled_::cgaLoftRuled_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

/* args[0], args[1] の cgMesh(reader が decode 済)を corefinement union → 結果を cgMesh に。
 * シリアライズは writer 側。重い計算は ACT_START(スレッド)。引き渡しは get_result()(#3406 2026-07-30: get_body 統合)。 */
void
cgaLoftRuled_::compute()
{
	const char *msg = 0;
	/* ★ 理由の受け皿は **この呼び出しのローカル** (static を置かない)。 */
	char why[512];
	why[0] = '\0';
	mesh = cg_loft_ruled_from_args(args, &msg, why, (int)sizeof why);
	if ( ! mesh.is_notNull() ) {
		char b[600];
		::snprintf(b, sizeof b, "loft_ruled: %s", msg ? msg : "failed");
		result = cga_err(thNEW(stdString,(b)));
	}
}

/* この演算の結果 (#3406, 2026-07-30 メモ: get_body/get_result を統一)。エラー時は
 * compute() が result にエラー値を残して mesh を未設定のまま return するので result 優先。
 * 成功時は result=thNULL のままmeshを返す。保存(Writer 起動)は agent が出力 pigDataCache
 * の set_body 経由で行う。 */
sPtr<pigData>
cgaLoftRuled_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
