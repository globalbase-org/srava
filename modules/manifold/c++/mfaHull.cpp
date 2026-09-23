/*
 * mfaHull — hull(a[,b,…]) — 与えた形すべての**凸包**(#3511)。args = mfMesh か mfCross を 1 個以上。
 * 本体は mf_hull_from_args (mfMesh.cpp) にあり、3D/2D の振り分けもそちらが持つ。
 *
 * ★ **情報を落とす op** — 入力は「点の集合」としてしか見られないので、穴も凹みも消える。
 *   形を整える道具ではない。
 * ★ hull(hull(a,b),c) = hull(a,b,c) なので sig は fold 形 (木に分解してよい)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"mf/c++/mfMesh.h"
#include	"mf/c++/ptsmfWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	<stdio.h>
#include	"_ts2/c++/mfaHull_.h"

CLASS_TINYSTATE(mf/c++/mfaHull,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	mfaHull_(
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
class mfCross;
TS_END_INTERFACE

#endif


mfaHull_::mfaHull_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
mfaHull_::compute()
{
	/* ★ #3511: 1 個以上を受け、まとめて 1 回の QuickHull にかける。 */
	const char *msg = 0;
	geom = mf_hull_from_args(args, &msg);
	if ( ! geom.is_notNull() ) {
		char b[128];
		::snprintf(b, sizeof b, "hull: %s", msg ? msg : "convex hull failed");
		result = mfa_err(thNEW(stdString,(b)));
		return;
	}
	/* ★★ #3498: 遅延 CSG 木を **ここで** 評価する。旧来これは encode() (GetMeshGL64) で
	 *   起きており、compute() が返った後だったので中断が届かなかった。詳細は
	 *   mfMesh.h の mfGeom::force_eval と mf_eval_err。 */
	if ( (result = mf_eval_err(geom, brk_, "hull")) != thNULL ) { geom = thNULL; return; }

	/* ★★ 退化を**黙って通さない** (#3511)。点が 1 点 / 1 直線上 / 1 平面上に乗っていると
	 *   凸包は立体にならないが、Manifold の Hull は **空の Manifold を Status=NoError で返す**
	 *   ので、そのままだと利用者には volume 0 の普通の答えとして届く。
	 *   ⚠ 空の入力 (empty3d) も同じくここで落ちる。cgal / nef / geogram は「頂点が無い」で
	 *     明示エラーにするので、4 カーネルで揃った振る舞いになる。
	 *   ⚠⚠ **面数では検出できない**。実測: box(1,1,0) の hull は面 6 枚の「潰れた箱」として
	 *     返り、Status も NoError のまま volume だけが 0 になる (QuickHull が平面上の点集合に
	 *     対して閉じた向きつき三角形群を返すため)。⇒ **体積 (2D なら面積) を見る**。
	 *     cgal / nef は is_closed / 面数で構造的に検出できるが、manifold ではここが唯一の口。
	 *   ★ 評価は上の mf_eval_err で済んでいるので、ここで測っても余計な仕事は増えない。 */
	{
		sPtr<mfMesh>  h3 = sPtr<mfMesh>::d_cast(geom);
		sPtr<mfCross> h2 = sPtr<mfCross>::d_cast(geom);
		int degenerate = h3.is_notNull() ? ( h3->op_nfaces() < 4 || h3->op_volume() <= 0.0 )
		               : h2.is_notNull() ? ( h2->op_nverts() < 3 || h2->op_area()   <= 0.0 ) : 0;
		if ( degenerate ) {
			/* ★ #3533: 2D 領域が混ざっていたなら「全部 1 つの平面に載っていた」が一番ありそうな
			 *   原因なので、**2D として求め直す書き方**まで言う (cgal の同じ枝と文言を揃える。
			 *   ⚠ 片方だけ直さないこと)。 */
			int had2d = 0;
			for ( int i = 0 ; args != 0 && i < args->length() ; ++i )
				if ( sPtr<mfCross>::d_cast((*args)[i]).is_notNull() ) had2d = 1;
			result = mfa_err(thNEW(stdString,( had2d
			    ? "hull: the points are degenerate (all on one plane), so the convex hull is not "
			      "a solid; if the operands really are on one plane, cast(\"mf-cross2d\", ...) the "
			      "placed ones first so the whole call is a 2D hull"
			    : "hull: the points are degenerate (a single point, all on one line, or all on one "
			      "plane), so the convex hull is not a solid")));
			geom = thNULL;
		}
	}
}

/* この演算の結果 (#3406, 2026-07-30 メモ: get_body/get_result を統一)。エラー時は
 * compute() が result にエラー値を残して geom を未設定のまま return するので result 優先。
 * 成功時は result=thNULL のままgeomを返す。保存(Writer 起動)は agent が出力 pigDataCache
 * の set_body 経由で行う。 */
sPtr<pigData>
mfaHull_::get_result()
{
	return ( result != thNULL ) ? result : geom;
}
