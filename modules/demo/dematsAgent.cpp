/*
 * dematsAgent — 第3モジュール "demo" の実行体 (ptsAgent 派生・docs §7 Phase 6)。
 *   ppatsAgent (pipe_proximity in-proc 実行体) の更なる簡約版:
 *     - value op のみ・入力は全て inline・出力は value (cache 読み書きなし)。
 *     - **EXEC_PROCESS 専用** (exec_caps に THREAD を立てない) なので必ず srava_agent プロセスで走る
 *       = 「2 個目の in-proc thread カーネル」の境界規約 (⑤・保留中) を踏まない。
 *     - process 専用ゆえ計算はブロックしてよい → ptsCalcBody を使わず STARTCALC で **同期 compute**。
 *
 * 位置づけ (完成条件の実証): このモジュールは CGAL/Manifold/srava 言語を一切参照せず、descriptor.ops
 *   に新 op (demo_add / demo_range) を申告するだけ。planner は起動時 load_search_path で demo.so を
 *   dlopen し、mk_call の generic 層 (any_supports_op) が新 op を pigfModuleAgent ノードとして受理する。
 *   スクリプト側は `module("demo.so",{priority:99})` で demo を既定カーネルにするだけ (host 無改修)。
 *
 * 流れ: INI → WAIT (C_OP/C_ARG_DATA/C_ARG_END) → STARTCALC (同期 demo_compute → set_body) → FIN。
 */
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/ptsApplication.h"
#include	"pig/c++/ptsAgent.h"
#include	"pig/c++/pigAgentRegistry.h"
#include	"pig/c++/pigModuleRegistry.h"
#include	"pig/c++/pigModule.h"
#include	"pig/c++/pigOpEntry.h"
#include	"pig/c++/pigData.h"
#include	"pig/c++/pigwire.h"          /* C_OP / C_ARG_DATA / C_ARG_END */
#include	"pig/c++/ptsMediatorPacket.h"
#include	"ts2/c++/stdEvent.h"
#include	"ts2/c++/stdString.h"
#include	"demo_compute.h"
#include	"_ts2/c++/dematsAgent_.h"

#include	<string.h>
#include	"pig/c++/pigModuleError.h"
#include	"pig/c++/ptsCalcBody.h"      /* ★ #3417: calc を別オブジェクトで持つ */
#include	"demo/c++/demaCompute.h"     /* 計算本体 */
/* ★ #3475: このモジュール専用のエラー生成子 (共有ヘッダを持たないので
 *   ここで定義する)。文言は "[TAG] demo/op: message" になる。 */
PIG_DEFINE_MODULE_ERR(dema_err, "demo")


CLASS_TINYSTATE(demo/c++/dematsAgent,pig/c++/ptsAgent)

/* descriptor.ops (レジストリ照会用)。全 op out=value。in/nin/mkCalc は不使用 (単一 demo_compute へ流す)。 */
static const pigOpEntry DEMO_OPS[] = {
	{ "demo_add",   0, 0, AK_INLINE, 0, 1, "->value", 0, 1 /* ★可変部は値 */ },
	{ "demo_range", 0, 0, AK_INLINE, 0, 1, "->value", 0, 1 /* ★可変部は値 */ },
	/* ★ #3417: 中断できる「重い op」。graceful teardown の検証用 (demo_compute.cpp 参照)。 */
	{ "demo_spin",  0, 0, AK_INLINE, 0, 1, "->value", 0, 1 /* ★可変部は値 */ },
};
static const int DEMO_N_OPS = (int)(sizeof(DEMO_OPS) / sizeof(DEMO_OPS[0]));

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	dematsAgent_(
		sPtr<ptsObject> parent);

	sRptr<ptsObject,tinyState>		parent;
protected:
	sArray<sPtr<pigData> >	argv;
	sPtr<pigDataCache>	outCache;
	sPtr<pigData>		err;
	sPtr<stdString>		op;
	sPtr<ptsCalcBody>	calc;   /* ★ #3417: 計算本体 (専用 thread)。ppatsAgent と同じ形 */
private:
	TS_DEFARGS
};

TS_END_IMPLEMENT

TS_BEGIN_INTERFACE
#include	"ts2/c++/sRptr.h"
#include	"ts2/c++/sArray.h"
#include	"ts2/c++/stdString.h"
class tinyState;
class ptsObject;
class pigData;
class pigDataCache;
class stdString;
class ptsCalcBody;
TS_END_INTERFACE

#endif


dematsAgent_::dematsAgent_(TS_ARGS0)
        : ptsAgent_(parent),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}

/* この実行体を "demo" として登録 (srava_agent が dlopen 時に make_agent で起こす)。 */
static sPtr<ptsAgent>
mk_dematsAgent(sPtr<ptsObject> med)
{
	return thNEW(dematsAgent,(med));
}

/* 自己申告記述子。priority=-1 (opt-in: agent(so,{priority}) で既定化)・**EXEC_PROCESS 専用** (⑤ 回避)。
 * namespace scope の const は既定で内部リンケージ → manifest.cpp から extern 参照するため extern 明示。 */
extern const srava_module_descriptor dematsAgent_descriptor;
extern const srava_module_descriptor dematsAgent_descriptor = {
	.abi_version   = SRAVA_MODULE_ABI,
	.name          = "demo",
	.priority      = -1,   /* テスト/実証専用。既定カーネル候補としては最下位群 (負値)・同点回避 */
	.make_agent    = &mk_dematsAgent,
	.exec_caps     = (unsigned)EXEC_PROCESS,
	.exec_default  = EXEC_PROCESS,
	.ops           = DEMO_OPS,
	.n_ops         = DEMO_N_OPS,
	.import_exts   = 0,
	.export_exts   = 0,
	.provides      = 0,   /* 無し */
	/* ★ v18 (#3466): このモジュールが出す結果の版。**計算を変えたら手で上げる**。 */
	.cache_version = 1,
	/* ★ v7 (#3419): op 内並列の方式と σ (docs/srava_load_control_design.md §5.5/§5.6)。
	 *   テスト専用 */
	.initialize    = 0,   /* 無し */
	.configure     = 0,   /* ★ v10 (#3441): opts フックは未使用(このモジュールは module() の
	                       *   opts を消費しない) */
};
/* ★ #3427 ③: 旧・静的初期化の register_descriptor は撤去。登録は dlopen 経路
 * (pigModuleRegistry::load_file → register_descriptor) の 1 本 = app 所有レジストリへ。 */


/*******************************************
	STATE MACHINE
********************************************/

TS_STATE(INI_ptsAgent_START)
{
	op = thNULL;
	return ACT_dematsAgent_WAIT;
}

TS_STATE(ACT_dematsAgent_WAIT)
{
	if ( ev->type == TSE_PACKET ) {
		sPtr<ptsMediatorPacket> mpkt = sPtr<ptsMediatorPacket>::d_cast(ev->msg_obj);
		if ( mpkt == thNULL )
			return 0;
		switch ( mpkt->type ) {
		case C_OP: {
			op = mpkt->str;
			if ( op == thNULL ) {
				err = dema_err("demo: missing op name");
				return rDO|ACT_dematsAgent_ERROR;
			}
			argv.length(0);
			outCache = thNULL;
			break;
		}
		case C_ARG_DATA: {
			if ( op == thNULL ) {
				err = dema_err("arg before C_OP");
				return rDO|ACT_dematsAgent_ERROR;
			}
			int idx = (int)mpkt->idx;
			sPtr<pigData> d = mpkt->data;
			if ( d == thNULL || d->is_error() ) {
				err = ( d != thNULL ) ? d : sPtr<pigData>(dema_err("inline arg decode error"));
				return rDO|ACT_dematsAgent_ERROR;
			}
			if ( d->is_cache() ) {
				err = dema_err("demo: value arguments only (got a mesh handle)");
				return rDO|ACT_dematsAgent_ERROR;
			}
			if ( idx >= argv.length() ) argv.length(idx + 1);
			argv[idx] = d;
			break;
		}
		case C_ARG_END: {
			outCache = sPtr<pigDataCache>::d_cast(mpkt->data);
			if ( outCache == thNULL ) {
				err = dema_err(
				    "dematsAgent: C_ARG_END without a target cache path");
				return rDO|ACT_dematsAgent_ERROR;
			}
			return rDO|ACT_dematsAgent_STARTCALC;
		}
		default:
			break;
		}
		return 0;
	}
	if ( is_destroyed() ) {
		err = dema_err("aborted: agent was destroyed");
		return rDO|ACT_dematsAgent_ERROR;
	}
	return 0;
}

/* ★ #3417 (2026-09-06): **計算は別オブジェクト (calc) の専用 thread で回す**。
 *
 * 旧実装は「EXEC_PROCESS 専用: 同期 compute でよい (ptsCalcBody 不要)」として、この状態関数の
 * 中で demo_compute() を最後まで走らせていた。速い value op しか無いうちは正しかったが、
 * 中断できる op (demo_spin) を足すと破綻する:
 *   ・同期のままだとイベントループが止まり、destroy も wire の EOF も受け取れない
 *   ・この状態関数を TS_THREAD にするだけでも足りない。中断要求を撃つ相手が
 *     **計算しているオブジェクト自身**になり、要求を受けて動く者が居なくなる
 * ⇒ ptsGenericAgent / ptsCalcBody が実カーネル 11 本に提供している形 (実行体 + calc) に揃える。
 *   pipe_proximity (ppatsAgent + ppaCompute) がそのまま手本。 */
TS_STATE(ACT_dematsAgent_STARTCALC)
{
	calc = thNEW(demaCompute,(ifThis, &argv, outCache->get_path(), op));
	return ACT_dematsAgent_CALC;   /* calc の TSE_RETURN 待ち */
}

TS_STATE(ACT_dematsAgent_CALC)
{
	if ( ev->type == TSE_RETURN && ev->source == calc ) {
		sPtr<pigData> cr = calc->get_result();
		/* destroy 済みなら結果を捨てる (中断は「答えが出なかった」であって「答えは空」ではない)。
		 * calc 側のエラー (demo_spin の中断) があればそれを優先してリレーする。 */
		if ( is_destroyed() ) {
			err = ( cr != thNULL && cr->is_error() ) ? cr
			    : sPtr<pigData>(dema_err("aborted: agent was destroyed"));
			return rDO|ACT_dematsAgent_ERROR;
		}
		if ( cr != thNULL && cr->is_error() ) {
			err = cr;
			return rDO|ACT_dematsAgent_ERROR;
		}
		/* value 出力: 出力 cache へ set_body → 親 (ptsAgentApplication) が A_SAVE_BEGIN に相乗りで返す。 */
		outCache->set_body(cr);
		set_result(sPtr<pigData>::d_cast(outCache));
		return rDO|FIN_START;
	}
	/* destroy の作法: 子へ destroy を送り TSE_RETURN を待つ。即 FIN しない。 */
	if ( is_destroyed() ) {
		if ( calc.is_notNull() ) { calc->destroy(); return 0; }
		err = dema_err("aborted: agent was destroyed");
		return rDO|ACT_dematsAgent_ERROR;
	}
	return 0;
}

TS_STATE(ACT_dematsAgent_ERROR)
{
	set_result( ( err != thNULL ) ? err
	    : sPtr<pigData>(dema_err("demo error")) );
	return rDO|FIN_START;
}

TS_STATE(FIN_START)
{
	outCache = thNULL;
	err      = thNULL;
	op       = thNULL;
	argv.length(0);
	return rDO|FIN_ptsAgent_START;
}
