/*
 * mfaRevolve — revolve(cross, angle, segs) の計算本体(mf 版・cgaRevolve のミラー)。
 * 2D 断面(mfCross)を Y 軸まわりに回して 3D(mfMesh)を作る。Manifold::Revolve に委譲。
 *   angle=回転角(度・既定 360)/ segs=全周分割数(既定 32)。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"mf/c++/mfMesh.h"
#include	"mf/c++/ptsmfWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	"common/segs.h"   /* ★ #3530: segs / n の共通検査 */
#include	"_ts2/c++/mfaRevolve_.h"

CLASS_TINYSTATE(mf/c++/mfaRevolve,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	mfaRevolve_(
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


mfaRevolve_::mfaRevolve_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
mfaRevolve_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<mfCross> in    = ( na > 0 ) ? sPtr<mfCross>::d_cast((*args)[0]) : sPtr<mfCross>();
	double        angle = ( na > 1 ) ? (*args)[1]->get_flt() : 360.0;
	int    nseg_in = ( na > 2 ) ? (int)(*args)[2]->get_int() : 0;   /* 0 = 未指定 */
	int    nseg = 0;
	/* ★★ #3530: segs の意味を全 op / 全カーネルで 1 本に揃えた (src/h/common/segs.h)。
	 *   0 or 省略 = 既定値 / 1,2 / 負 = 明示エラー / 3 以上 = その値。 */
	if ( srava_geo::check_segs(nseg_in, 32, &nseg) != srava_geo::SEGS_OK ) {
		result = mfa_err(thNEW(stdString,(srava_geo::segs_error("revolve").c_str()))); return;
	}
	if ( ! in.is_notNull() ) {
		result = mfa_err(thNEW(stdString,("revolve: needs a 2D polygon")));
		return;
	}
	if ( angle <= 0.0 ) {
		result = mfa_err(thNEW(stdString,("revolve: angle must be > 0")));
		return;
	}
	if ( angle > 360.0 ) angle = 360.0;
	/* ★★ #3526: **空間に置かれた 2D も回せる**。⇒ 軸は **world Y** (原点まわり)・常に (ひさ判断)。
	 *
	 * ★ 根拠は @extrude@ と同じ論法 — あちらも「world +Z のまま」に決めてある。world 軸に
	 *   固定しておけば「先に revolve して立体を動かす」で枠相対の結果も作れるが、枠相対に
	 *   固定すると *置いた断面を world 軸で回す手段が無くなる*。★ occt (ocaRevolve) は元から
	 *   world Y なので、3 カーネルで同じ規約になる。
	 *
	 * ★★ 実装は **2 経路**。枠が既定なら従来のまま (Manifold::Revolve) — *既存の値を
	 *   1 ビットも動かさない*ため。枠が既定でないときだけ、置いた輪を world Y まわりに
	 *   回した列を張る (mf_revolve_placed)。
	 *   ⚠ 「局所で回してから枠を当てる」では **書けない** (それは軸を枠の V に固定することに
	 *     なり、world Y と違う立体になる)。掃引そのものを世界座標でやる必要がある。 */
	if ( ! in->frame_is_default() ) {
		const char *msg = 0;
		char why[512];
		why[0] = '\0';
		sPtr<mfMesh> pm = mf_revolve_placed(in, angle, nseg, &msg, why, (int)sizeof why);
		if ( ! pm.is_notNull() ) {
			char b[600];
			::snprintf(b, sizeof b, "revolve: %s", msg ? msg : "failed");
			result = mfa_err(thNEW(stdString,(b)));
			return;
		}
		mesh = pm;
		return;
	}
	/* ⚠⚠ **軸をまたぐ / 軸の向こう側にある断面を黙って通さない** (2026-09-13 に実測で判明)。
	 *   @Manifold::Revolve@ は profile の x<0 を受けても止まらず、掃引が自分自身を通り抜けた
	 *   立体の体積を **そのまま返していた**。cgal は @profile x (radius) must be >= 0@ で断り、
	 *   occt は BRep が NotDone で失敗するので、manifold だけが黙っていた。
	 *   ⇒ **cgal と同じ条件・同じ文言**にして 3 カーネルで揃える (#3510 の「同じ op が
	 *     カーネルごとに別のことを答える」を作らない)。
	 *   ★ 枠を持つ 2D の側はこれより一般の検査 (軸をまたぐ / 軸が貫く) を
	 *     @mf_revolve_placed@ が持っている。 */
	{
		manifold::Polygons ps = in->polys();   /* ★ #3529 */
		int bad = 0;
		for ( size_t r = 0 ; r < ps.size() && ! bad ; ++r )
			for ( size_t i = 0 ; i < ps[r].size() ; ++i )
				if ( ps[r][i].x < 0.0 ) { bad = 1; break; }
		if ( bad ) {
			result = mfa_err(thNEW(stdString,("revolve: profile x (radius) must be >= 0")));
			return;
		}
	}
	manifold::Manifold m = manifold::Manifold::Revolve(in->polys(), nseg, angle);   /* ★ #3529 */
	/* ★ 軸合わせ: Manifold::Revolve は断面 XY を回して **高さを Z 軸**にする。cgaRevolve は
	 *   profile(x=半径, y=高さ)を **Y 軸**まわりに回す(高さ=Y)。モデルは cg 規約前提(その後
	 *   @("x",90) で Z-up 化する)なので、-90°/X 回転 (x,y,z)→(x,z,-y) で高さを Z→Y に移して一致させる。 */
	sPtr<mfMesh> raw = thNEW(mfMesh,(m));
	static const double e[12] = {
	    1, 0, 0, 0,
	    0, 0, 1, 0,
	    0,-1, 0, 0
	};
	mesh = sPtr<mfMesh>::d_cast(raw->apply_affine(e));   /* apply_affine は sPtr<mfGeom> 返し */

}

/* この演算の結果 (#3406, 2026-07-30 メモ: get_body/get_result を統一)。エラー時は
 * compute() が result にエラー値を残して mesh を未設定のまま return するので result 優先。
 * 成功時は result=thNULL のままmeshを返す。保存(Writer 起動)は agent が出力 pigDataCache
 * の set_body 経由で行う。 */
sPtr<pigData>
mfaRevolve_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
