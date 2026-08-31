/* co_ptsConfigWatch — ptsLoadControl の相棒その 2。**設定の周期チェック専用**。
 *
 * ★ なぜ co_ptsLoadControl (CPU サンプル) と分けるか (ひさ判断 2026-08-24):
 *   設定の反映周期と CPU の標本周期は**別の物理量**。同じタイマに相乗りさせると、
 *   片方を最適化したときに他方が動く。co_ptsLoadControl を分けたのと同じ理由。
 *
 * ★★ **destroy の瞬間にも 1 度チェックする** (ひさ指摘 2026-08-24):
 *   スクリプトが 250ms より短く終わると周期タイマが**一度も発火しない**。そのとき
 *   「LOAD_RAMP_START は起動時にしか読まれない = 代入は効かない」の警告が出ないまま終わる。
 *   ⚠ **短い run ほど「効かなかったこと」に気づけない**のは最悪なので、FIN で必ず 1 回見る。
 *
 * 生存: ptsLoadControl の INI で thNEW、FIN で destroy。
 * ⚠ タイマ待ちでは is_destroyed() をポーリングすること (destroy は印を置くだけ)。
 */
#include	"pig/c++/ptsLoadControl.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/osglue.h"
#include	"_ts2/c++/co_ptsConfigWatch_.h"

CLASS_TINYSTATE(pig/c++/co_ptsConfigWatch,pig/c++/ptsObject)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	/* parent = ptsLoadControl。lc = 設定を読み直させる相手。 */
	co_ptsConfigWatch_(
		sPtr<tinyState> parent,
		sPtr<ptsLoadControl> lc);

	sRptr<tinyState,tinyState>	parent;
private:
	/* ⚠ lc は ctor 引数なので tscpp2 が自動保存する。ここで宣言し直すと二重保持
	 *   ([[codegen-ctor-arg-double-hold]])。 */
	sTimer				timer;
	INTEGER64			periodUs;
	TS_DEFARGS
};

TS_END_IMPLEMENT

TS_BEGIN_INTERFACE
#include	"ts2/c++/sRptr.h"
#include	"ts2/c++/sTimer.h"
class tinyState;
class ptsLoadControl;
TS_END_INTERFACE

#endif

co_ptsConfigWatch_::co_ptsConfigWatch_(TS_ARGS0)
	: ptsObject_(parent),
	  parent(tinyState_::parent)
{
	TS_CPARGS0
	/* ★ 既定 250ms。CPU サンプルの SRAVA_LOAD_CPU_MS とは**別の口**。 */
	int ms = osglue_env_int("SRAVA_CFG_MS", 250);
	if ( ms <= 0 ) ms = 250;
	periodUs = (INTEGER64)ms * 1000;
}

/*******************************************
	STATE MACHINE
********************************************/

TS_STATE(INI_ptsObject_START)
{
	if ( lc == thNULL )
		return rDO|FIN_START;
	return rDO|ACT_co_ptsConfigWatch_TICK;
}

TS_STATE(ACT_co_ptsConfigWatch_TICK)
{
	if ( is_destroyed() ) return rDO|FIN_START;
	timer.start(ifThis, periodUs);
	return ACT_co_ptsConfigWatch_WAIT;
}

TS_STATE(ACT_co_ptsConfigWatch_WAIT)
{
	if ( is_destroyed() ) { timer.stop(ifThis); return rDO|FIN_START; }
	if ( ! timer.is_expire(ifThis) ) return 0;
	if ( lc.is_notNull() ) lc->refresh_config();
	return rDO|ACT_co_ptsConfigWatch_TICK;
}

TS_STATE(FIN_START)
{
	timer.stop(ifThis);
	/* ★★ 最後に 1 回。250ms 未満で終わる run でも「効かない設定」を検出できるようにする。 */
	if ( lc.is_notNull() ) lc->refresh_config();
	return rDO|FIN_ptsObject_START;
}
