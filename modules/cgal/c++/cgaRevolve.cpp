/*
 * cgaRevolve — revolve(poly2d, angle) の計算本体(ptsCalcBody 派生)= 2D→3D の回転体(旋盤)。
 * 入力 cgMesh2D のプロファイル(x=Y 軸からの半径≥0, y=高さ)を Y 軸まわりに回して立体化。
 *   3D 点 = (px·cosθ, py, px·sinθ)。角度は cos/sin の double を K::FT に格納(EPECK 座標は維持)。
 *   全周 360°=端キャップ不要(角度方向ラップ)。部分角(<360)=ラップせず両端に CDT 三角化のキャップ。
 * sphere は半円プロファイルの revolve に相当。穴あきプロファイルも対応。
 */
#include	"pig/c++/ptsCalcBody.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/pigData.h"
#include	"cg/c++/cgMesh.h"
#include	"cg/c++/ptscgWireCacheStreamWriterMesh.h"
#include	"ts2/c++/stdString.h"
#include	"common/segs.h"   /* ★ #3530: segs / n の共通検査 */
#include	"_ts2/c++/cgaRevolve_.h"

#include	<math.h>
#include	<map>
#include	<list>
#include	<vector>

CLASS_TINYSTATE(cg/c++/cgaRevolve,pig/c++/ptsCalcBody)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	cgaRevolve_(
		sPtr<ptsObject> parent,
		sArray<sPtr<pigData> > *_args,
		sPtr<stdString> _target);

	sRptr<ptsObject,tinyState>		parent;

	virtual sPtr<pigData>	get_result();

protected:
	virtual void	compute();
	sPtr<cgMesh3D>	mesh;
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
class cgMesh3D;
TS_END_INTERFACE

#endif


cgaRevolve_::cgaRevolve_(TS_ARGS0)
        : ptsCalcBody_(parent, _args, _target),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

/* ★★ #3535②: **この TU は CGAL を 1 つも参照しない**。CDT の道具一式と掃引の構成は
 * libsrava_cg 側 (cgMesh3D::build_revolve) へ移した。⚠ CGAL は 6.x でヘッダオンリーなので、
 * ここで触ると **この .so の中に CGAL の可変大域状態 (_error_handler / _error_behaviour) の
 * 実体**ができて libsrava_cg 側と別物になる (理由は cgMesh.h の build_box の宣言のところ)。 */

void
cgaRevolve_::compute()
{
	int na = ( args != 0 ) ? args->length() : 0;
	sPtr<cgMesh2D> in    = ( na > 0 ) ? sPtr<cgMesh2D>::d_cast((*args)[0]) : sPtr<cgMesh2D>();
	double         angle = ( na > 1 ) ? (*args)[1]->get_flt() : 360.0;
	int    nseg_in = ( na > 2 ) ? (int)(*args)[2]->get_int() : 0;   /* 0 = 未指定 */
	int    nseg = 0;
	/* ★★ #3530: segs の意味を全 op / 全カーネルで 1 本に揃えた (src/h/common/segs.h)。
	 *   0 or 省略 = 既定値 / 1,2 / 負 = 明示エラー / 3 以上 = その値。 */
	if ( srava_geo::check_segs(nseg_in, 32, &nseg) != srava_geo::SEGS_OK ) {
		result = cga_err(thNEW(stdString,(srava_geo::segs_error("revolve").c_str()))); return;
	}

	mesh = thNEW(cgMesh3D,());
	if ( ! in.is_notNull() ) {
		result = cga_err(thNEW(stdString,("revolve: needs a 2D polygon")));
		return;
	}
	if ( angle <= 0.0 ) {
		result = cga_err(thNEW(stdString,("revolve: angle must be > 0")));
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
	 * ★★ 実装は **2 経路**。枠が既定なら従来のまま (従来の CDT 掃引) — *既存の値を
	 *   1 ビットも動かさない*ため。枠が既定でないときだけ、置いた輪を world Y まわりに
	 *   回した列を張る (cg_revolve_placed)。
	 *   ⚠ 「局所で回してから枠を当てる」では **書けない** (それは軸を枠の V に固定することに
	 *     なり、world Y と違う立体になる)。掃引そのものを世界座標でやる必要がある。 */
	if ( ! in->frame_is_default() ) {
		const char *msg = 0;
		char why[512];
		why[0] = '\0';
		sPtr<cgMesh3D> pm = sPtr<cgMesh3D>::d_cast(
		    cg_revolve_placed(in, angle, nseg, &msg, why, (int)sizeof why));
		if ( ! pm.is_notNull() ) {
			char b[600];
			::snprintf(b, sizeof b, "revolve: %s", msg ? msg : "failed");
			result = cga_err(thNEW(stdString,(b)));
			return;
		}
		mesh = pm;
		return;
	}

	const int FULLSEGS = nseg;   /* 全周 360°相当の分割数(ユーザ指定・既定 32) */
	bool full = ( angle >= 360.0 );
	double aRad = ( full ? 2.0 * M_PI : angle * M_PI / 180.0 );
	int segs = full ? FULLSEGS : (int)(FULLSEGS * angle / 360.0 + 0.5);
	if ( segs < 2 ) segs = 2;
	int nPos = full ? segs : segs + 1;   /* 角度位置の個数(部分角は両端含む) */

	std::vector<double> cs(nPos), sn(nPos);
	for ( int j = 0 ; j < nPos ; ++j ) {
		double t = full ? (2.0 * M_PI * (double)j / (double)segs)
		                : (aRad * (double)j / (double)segs);
		cs[j] = ::cos(t); sn[j] = ::sin(t);
	}

	/* ★ #3535②: 構成は libsrava_cg 側 (cgMesh3D::build_revolve)。 */
	if ( mesh->build_revolve(in, nPos, cs, sn, full ? 1 : 0, segs) != 0 ) {
		result = cga_err(thNEW(stdString,("revolve: profile x (radius) must be >= 0")));
		return;
	}

}

/* この演算の結果 (#3406, 2026-07-30 メモ: get_body/get_result を統一)。エラー時は
 * compute() が result にエラー値を残して mesh を未設定のまま return するので result 優先。
 * 成功時は result=thNULL のままmeshを返す。保存(Writer 起動)は agent が出力 pigDataCache
 * の set_body 経由で行う。 */
sPtr<pigData>
cgaRevolve_::get_result()
{
	return ( result != thNULL ) ? result : mesh;
}
