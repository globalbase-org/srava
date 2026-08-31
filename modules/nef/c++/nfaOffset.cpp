/*
 * nfaOffset — offset(mesh, d[, subdiv]) の 3D オフセット (#3440 の 2)。
 *
 *   ★cgal.so から**移設**した op。中身が Nef + 凸分解 (Minkowski 和) なので、
 *   「他の幾何カーネルの機能を借りて自分の幾何カーネルの顔で出さない」というモジュール境界の
 *   約束① (docs/srava_module_reference.md「モジュールの境界」章) に cgal.so の実装が違反していた。
 *
 *   ★**2D offset は cgal.so に残る** — あちらは straight skeleton (面取り) で Nef と無関係。
 *   よってこのモジュールが申告するのは **3D の sig だけ**。
 *
 *   d>0 膨張 / d<0 収縮 (補集合トリック) / d==0 は入力そのまま。subdiv は近似球の細分化 (既定 1)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"nf/c++/nfMesh.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/nfaOffset_.h"

#include	<stdio.h>   /* snprintf (エラー文の組み立て) */

CLASS_TINYSTATE(nf/c++/nfaOffset,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	nfaOffset_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<nfMesh>	mesh;
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
class nfMesh;
TS_END_INTERFACE

#endif


nfaOffset_::nfaOffset_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
nfaOffset_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<nfMesh> in = ( na > 0 ) ? sPtr<nfMesh>::d_cast((*args)[0]) : sPtr<nfMesh>();
	double d   = ( na > 1 ) ? (*args)[1]->get_flt() : 0.0;
	int subdiv = ( na > 2 ) ? (int)(*args)[2]->get_int() : 1;   /* 近似球の細分化 (既定 1) */

	if ( ! in.is_notNull() ) {
		result = thNEW(pigDataError,(thNEW(stdString,("offset: needs a Nef mesh"))));
		return;
	}
	/* ★ 近似球の細分化は **0〜6**。範囲外は**黙って丸めずに明示エラー**にする。
	 *   旧実装は 3 を超える値を黙って 3 へ丸めており、利用者は「4 を頼んだのに 3 の結果を
	 *   受け取り、しかもそれと気づけない」状態だった (本プロジェクトの「黙ってフォールバック
	 *   しない」原則に反する)。
	 *   上限が 6 なのは **実測で完走を確認できた最大値**だから (nfMesh::op_offset の表を参照)。
	 *   誤差は 1 段ごとに下がるが、コストの伸びの方が急なので、6 は事実上の終端。 */
	if ( subdiv < 0 || subdiv > 6 ) {
		char b[224];
		::snprintf(b, sizeof b,
		    "offset: subdiv=%d is out of range (0-6). 6 is the highest level measured to complete, "
		    "and it takes 13 minutes there (per level the error drops 4x and the cost rises 5-25x). "
		    "7 and above are unverified", subdiv);
		result = thNEW(pigDataError,(thNEW(stdString,(b))));
		return;
	}
	/* ★非有界は Minkowski が取れない (CGAL は黙って片方を返す) → 先に弾く。 */
	if ( ! in->is_bounded() ) {
		result = thNEW(pigDataError,(thNEW(stdString,
		    ("offset: an unbounded Nef cannot be offset (e.g. the result of complement)"))));
		return;
	}
	mesh = in->op_offset(d, subdiv);
	if ( ! mesh.is_notNull() )
		result = thNEW(pigDataError,(thNEW(stdString,("offset: computation failed"))));
}

/* この演算の結果。エラー時は compute() が result にエラー値を残して mesh 未設定で return するので
 * result 優先。保存 (Writer 起動) は agent が出力 pigDataCache の set_body 経由で行う。 */
sPtr<pigData>
nfaOffset_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
