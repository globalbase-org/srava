/* co_ptsLoadControl — ptsLoadControl の**相棒**。CPU 項のための周期サンプルだけを担当する。
 *
 * ★ なぜ別オブジェクトにするか (ひさ判断 2026-08-24):
 *   ランプの間隔 (SRAVA_LOAD_RAMP_MS) と CPU サンプルの周期 (SRAVA_LOAD_CPU_MS) は
 *   **別の物理量**である。同じタイマに相乗りさせると、片方を最適化したときに他方が動く。
 *   分けておけば、設定も最適化も互いに影響しない。
 *
 * ★ なぜ周期でなければならないか:
 *   旧実装は sample_cpu() を **ゲートの入退場のときだけ**呼んでいた。agent が少ない
 *   ワークロードでは入退場が疎で、観測の窓が開きすぎる。
 *   CPU 需要は連続的に変わるので、事象駆動では制御に使える信号にならない。
 *
 * 生存: ptsLoadControl の INI で thNEW、FIN で destroy。
 * ⚠ タイマ待ちの状態では **is_destroyed() をポーリング**すること。状態遷移機械は定義された
 *   遷移以外をしないので、destroy() は印を置くだけ。待つ側が見ないと app が終われない
 *   (ptsLoadControl が実装初回に踏んだ罠・そのコメントを参照)。
 */
#include	"pig/c++/ptsLoadControl.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/osglue.h"   /* osglue_env_int (#3419 §17.2) */
#include	"_ts2/c++/co_ptsLoadControl_.h"

#include	<stdlib.h>

CLASS_TINYSTATE(pig/c++/co_ptsLoadControl,pig/c++/ptsObject)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	/* parent = ptsLoadControl。lc = 標本を渡す相手。 */
	co_ptsLoadControl_(
		sPtr<tinyState> parent,
		sPtr<ptsLoadControl> lc);

	sRptr<tinyState,tinyState>	parent;
private:
	/* ⚠ lc は ctor 引数なので **tscpp2 が private メンバに自動保存**する。
	 *   ここで宣言し直すと二重保持になる ([[codegen-ctor-arg-double-hold]])。 */
	sTimer				timer;
	INTEGER64			periodUs;   /* CPU サンプルの周期 (SRAVA_LOAD_CPU_MS) */
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

co_ptsLoadControl_::co_ptsLoadControl_(TS_ARGS0)
	: ptsObject_(parent),
	  parent(tinyState_::parent)
{
	TS_CPARGS0
	/* ★ 既定 250ms。⚠ ランプの 250ms とは**別の値**で、たまたま同じ既定にしてあるだけ。
	 *   窓のガード SRAVA_LOAD_WINDOW_MS (既定 500ms) 未満の標本は sample_cpu 側が捨てるので、
	 *   実効的には「500ms 窓が 500ms ごとに更新される」形になる。 */
	/* ⚠ 旧: ここに独自の co_env_int があり **v<=0 を def に戻して**いた = 既存の env_int と
	 *   挙動が違った (2026-08-24 に自分で持ち込んだ不整合)。共通実装 osglue_env_int に統一し、
	 *   周期に 0 以下は意味が無いのでここで明示的にクランプする。 */
	int ms = osglue_env_int("SRAVA_LOAD_CPU_MS", 250);
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
	return rDO|ACT_co_ptsLoadControl_TICK;
}

TS_STATE(ACT_co_ptsLoadControl_TICK)
{
	if ( is_destroyed() ) return rDO|FIN_START;
	timer.start(ifThis, periodUs);
	return ACT_co_ptsLoadControl_WAIT;
}

TS_STATE(ACT_co_ptsLoadControl_WAIT)
{
	/* ⚠ 待っている間に destroy されうる。ポーリングしないと app が終われない。 */
	if ( is_destroyed() ) { timer.stop(ifThis); return rDO|FIN_START; }
	if ( ! timer.is_expire(ifThis) ) return 0;
	/* ★ 標本 1 点。pid の集合は app が持っているので app に取りに行かせる
	 *   (ここで loadPids を複製しない = 台帳を二重に持たない)。 */
	if ( ptsApp.is_notNull() )
		ptsApp->load_sample_cpu();
	return rDO|ACT_co_ptsLoadControl_TICK;
}

TS_STATE(FIN_START)
{
	timer.stop(ifThis);
	return rDO|FIN_ptsObject_START;
}
