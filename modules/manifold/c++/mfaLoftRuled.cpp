/*
 * mfaLoftRuled — loft_ruled(断面, 断面, …) (#3511)。断面間を **直線で結ぶ** (線織面)。
 *
 * ★★ 断面の **置き場所は op が決めない**。利用者が @transform@ で空間に置いた 2D を
 *   そのまま受ける (occt の ocaLoftRuled と同じ規約)。メッシュ系でこれが書けるようになったのは
 *   #3526 で mf-cross2d が **枠 (平面)** を持ったから。
 *
 *     loft_ruled(rect(2,3), translate(rect(2,3),[0,0,5]))                   まっすぐな角柱
 *     loft_ruled(circle(1,0), translate(rotate(circle(1,0),"x",20),[0,0,4]))  傾いた断面も置ける
 *
 * ★ なめらかな方 (@loft@) は **置かない** — 解析曲面が要るのでメッシュ系には無い。
 *   #3511 で 2 つを別 op にした理由そのもの (#3510 の表に「線織なら 3 カーネル・
 *   なめらかなら occt だけ」として出る)。
 *
 * 実体は @mf_loft_ruled_from_args@ (mfMesh.cpp)。対応づけの規約と occt との一致 / 不一致の
 * 但し書きはそちらに書いてある。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"mf/c++/mfMesh.h"
#include	"mf/c++/ptsmfWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	<stdio.h>
#include	"_ts2/c++/mfaLoftRuled_.h"

CLASS_TINYSTATE(mf/c++/mfaLoftRuled,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	mfaLoftRuled_(
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


mfaLoftRuled_::mfaLoftRuled_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
mfaLoftRuled_::compute()
{
	const char *msg = 0;
	/* ★ 理由の受け皿は **この呼び出しのローカル** (static を置かない)。 */
	char why[512];
	why[0] = '\0';
	geom = mf_loft_ruled_from_args(args, &msg, why, (int)sizeof why);
	if ( ! geom.is_notNull() ) {
		char b[600];
		::snprintf(b, sizeof b, "loft_ruled: %s", msg ? msg : "failed");
		result = mfa_err(thNEW(stdString,(b)));
		return;
	}
	/* ★ 遅延木の評価。この op は MeshGL64 から即時に組む葉なので Status() が即返るだけだが、
	 *   mfMesh.h の但し書きどおり「呼んでも害は無い」ので中断の口は開けておく。 */
	if ( (result = mf_eval_err(geom, brk_, "loft_ruled")) != thNULL ) { geom = thNULL; return; }
}

/* この演算の結果 (#3406, 2026-07-30 メモ: get_body/get_result を統一)。エラー時は
 * compute() が result にエラー値を残して geom を未設定のまま return するので result 優先。
 * 成功時は result=thNULL のままgeomを返す。保存(Writer 起動)は agent が出力 pigDataCache
 * の set_body 経由で行う。 */
sPtr<pigData>
mfaLoftRuled_::get_result()
{
	return ( result != thNULL ) ? result : geom;
}
