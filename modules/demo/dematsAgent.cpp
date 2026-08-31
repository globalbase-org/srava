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

CLASS_TINYSTATE(demo/c++/dematsAgent,pig/c++/ptsAgent)

/* descriptor.ops (レジストリ照会用)。全 op out=value。in/nin/mkCalc は不使用 (単一 demo_compute へ流す)。 */
static const pigOpEntry DEMO_OPS[] = {
	{ "demo_add",   0, 0, AK_INLINE, 0, 1, "->value", 0, 1 /* ★可変部は値 */ },
	{ "demo_range", 0, 0, AK_INLINE, 0, 1, "->value", 0, 1 /* ★可変部は値 */ },
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
	.hash_salt     = 0,   /* 基準カーネル/解析モジュールはソルト無し */
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
				err = thNEW(pigDataError,("demo: missing op name"));
				return rDO|ACT_dematsAgent_ERROR;
			}
			argv.length(0);
			outCache = thNULL;
			break;
		}
		case C_ARG_DATA: {
			if ( op == thNULL ) {
				err = thNEW(pigDataError,("arg before C_OP"));
				return rDO|ACT_dematsAgent_ERROR;
			}
			int idx = (int)mpkt->idx;
			sPtr<pigData> d = mpkt->data;
			if ( d == thNULL || d->is_error() ) {
				err = ( d != thNULL ) ? d : sPtr<pigData>(thNEW(pigDataError,("inline arg decode error")));
				return rDO|ACT_dematsAgent_ERROR;
			}
			if ( d->is_cache() ) {
				err = thNEW(pigDataError,("demo: value arguments only (got a mesh handle)"));
				return rDO|ACT_dematsAgent_ERROR;
			}
			if ( idx >= argv.length() ) argv.length(idx + 1);
			argv[idx] = d;
			break;
		}
		case C_ARG_END: {
			outCache = sPtr<pigDataCache>::d_cast(mpkt->data);
			if ( outCache == thNULL ) {
				err = thNEW(pigDataError,(
				    "dematsAgent: C_ARG_END without a target cache path"));
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
		err = thNEW(pigDataError,("aborted: agent was destroyed"));
		return rDO|ACT_dematsAgent_ERROR;
	}
	return 0;
}

TS_STATE(ACT_dematsAgent_STARTCALC)   /* EXEC_PROCESS 専用: 同期 compute でよい (ptsCalcBody 不要) */
{
	sPtr<pigData> cr = demo_compute(op->get_str(), argv);
	if ( cr != thNULL && cr->is_error() ) {
		err = cr;
		return rDO|ACT_dematsAgent_ERROR;
	}
	/* value 出力: 出力 cache へ set_body → 親 (ptsAgentApplication) が A_SAVE_BEGIN に相乗りで返す。 */
	outCache->set_body(cr);
	set_result(sPtr<pigData>::d_cast(outCache));
	return rDO|FIN_START;
}

TS_STATE(ACT_dematsAgent_ERROR)
{
	set_result( ( err != thNULL ) ? err
	    : sPtr<pigData>(thNEW(pigDataError,("demo error"))) );
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
