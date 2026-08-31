/*
 * ppatsAgent — pipe_proximity の in-proc 実行体 (ptsAgent 派生・.so 化 Phase 5・docs §5)。
 *   mfatsAgent (Manifold カーネルの in-proc 実行体) のミラー・値返し専用の簡約版。
 *
 * 位置づけ: pipe_proximity は解析モジュール (out=value・mesh 入出力なし) だが、実行方式 (thread/process)
 *   の抽象化は mesh カーネルと同じ (同じ記述子・同じ generic 経路)。★ Plan A (2026-08-10): 旧「plugin」
 *   レイヤ (pigfPluginAgent/pigPluginRegistry/.plugin) を廃止し、demo.so と同様に **pigfModuleAgent の
 *   generic 経路**で受理する。planner が exec_default=THREAD のとき ptsMediatorInternal 経由でこの実行体を
 *   planner 内 thread として起こし、process (opt-out) のときは srava_agent が dlopen して別プロセス実行する。
 *
 * mfatsAgent との差 (簡約点):
 *   - 入力は全て AK_INLINE (値/配列/ハッシュ)。cache (mesh) 入力なし → get_body ランデブー不要。
 *   - op ごとの OPS[] dispatch を持たず、単一 calc body (ppaCompute) が op 名で pp_compute へ分岐。
 *   - 出力は値のみ (mesh cache 出力なし)。set_body(値) → 親が A_SAVE_BEGIN に相乗りで返す。
 *
 * 流れ:
 *   INI      : WAIT へ (通信は parent=Mediator が確立済み)
 *   WAIT     : C_OP で op 記憶。C_ARG_DATA を収集 (cache 引数はエラー=値のみ)。C_ARG_END で計算起動。
 *   STARTCALC: ppaCompute を起こす (基底 ptsCalcBody が専用 thread で pp_compute を回す)
 *   CALC     : calc の TSE_RETURN を待ち結果を引く。値を出力 cache へ set_body → FIN
 *   ERROR    : pigDataError を set_result → FIN
 */
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/ptsApplication.h"   /* ptsApp 値メンバの完全型 */
#include	"pig/c++/ptsAgent.h"         /* 基底 (演算実行体) */
#include	"pig/c++/pigAgentRegistry.h" /* 自分を実行体として登録 */
#include	"pig/c++/pigModuleRegistry.h" /* 自己申告記述子の登録 */
#include	"pig/c++/pigModule.h"
#include	"pig/c++/pigOpEntry.h"       /* descriptor.ops 用の共通 op エントリ型 */
#include	"pig/c++/pigData.h"          /* pigDataCache / pigDataError */
#include	"pig/c++/pigwire.h"          /* C_OP / C_ARG_DATA / C_ARG_END */
#include	"pig/c++/ptsMediatorPacket.h"
#include	"pig/c++/ptsCalcBody.h"
#include	"ts2/c++/stdEvent.h"
#include	"ts2/c++/stdString.h"
#include	"pipe/c++/ppaCompute.h"      /* 計算本体 (pp_compute を thread 実行) */
#include	"_ts2/c++/ppatsAgent_.h"

#include	<string.h>

CLASS_TINYSTATE(pipe/c++/ppatsAgent,pig/c++/ptsAgent)

/* ---- descriptor.ops (レジストリ照会用: supports_op / op_out_is_mesh)。全 op out=value。 ----
 * in/nin/mkCalc は使わない (ppatsAgent は OPS dispatch を持たず単一 ppaCompute へ流す)。
 * variadic=1 で任意 arity を許容 (実 arity 検査は pp_compute 内)。 */
static const pigOpEntry PP_OPS[] = {
	{ "pipe_proximity",       0, 0, AK_INLINE, 0, 1, "->value", 0, 1 /* ★可変部は値 */ },
	{ "pipe_adjust",          0, 0, AK_INLINE, 0, 1, "->value", 0, 1 /* ★可変部は値 */ },
	{ "pipe_scene_proximity", 0, 0, AK_INLINE, 0, 1, "->value", 0, 1 /* ★可変部は値 */ },
	{ "pipe_scene_adjust",    0, 0, AK_INLINE, 0, 1, "->value", 0, 1 /* ★可変部は値 */ },
	{ "pipe_sample",          0, 0, AK_INLINE, 0, 1, "->value", 0, 1 /* ★可変部は値 */ },
};
static const int PP_N_OPS = (int)(sizeof(PP_OPS) / sizeof(PP_OPS[0]));

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ppatsAgent_(
		sPtr<ptsObject> parent);

	sRptr<ptsObject,tinyState>		parent;
protected:
	sPtr<ptsCalcBody>	calc;
	sArray<sPtr<pigData> >	argv;     /* C_ARG_DATA で収集した入力 (全て inline 値) */
	sPtr<pigDataCache>	outCache;    /* C_ARG_END で渡る出力先 (planner と共有) */
	sPtr<pigData>		err;
	sPtr<stdString>		op;          /* 現 op 名。thNULL=未設定 */
	int			gotEnd;
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
class ptsCalcBody;
class pigData;
class pigDataCache;
class stdString;
TS_END_INTERFACE

#endif


ppatsAgent_::ppatsAgent_(TS_ARGS0)
        : ptsAgent_(parent),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
    gotEnd = 0;
}

/* この実行体を "pipe_proximity" として登録する。ptsMediatorInternal::enable() が kernel 名
 * (= 記述子 name) でこの生成子を引いて in-proc 実行体を起こす。 */
static sPtr<ptsAgent>
mk_ppatsAgent(sPtr<ptsObject> med)
{
	return thNEW(ppatsAgent,(med));
}

/* 自己申告記述子。codec/exts なし。exec_caps=THREAD|PROCESS・既定 THREAD。priority は下の値を参照
 * (2026-08-18 `439a16b` で 0 → 4。同梱モジュールの同点を避けるため)。
 * namespace scope の const は既定で内部リンケージなので manifest.cpp から extern 参照するため
 * extern を明示する (mfatsAgent と同じトラップ回避)。 */
extern const srava_module_descriptor ppatsAgent_descriptor;
extern const srava_module_descriptor ppatsAgent_descriptor = {
	.abi_version   = SRAVA_MODULE_ABI,
	.name          = "pipe_proximity",
	.priority      = 4,
	/* 解析モジュール (幾何カーネルではない)。★priority は
	 *  同点を避けて全モジュールで別値にしてある = 同点の
	 *  勝敗はロード順 (走査順) 任せ = 不定になるため。 */
	.make_agent    = &mk_ppatsAgent,
	.exec_caps     = (unsigned)(EXEC_THREAD | EXEC_PROCESS),
	.exec_default  = EXEC_THREAD,
	.ops           = PP_OPS,
	.n_ops         = PP_N_OPS,
	.import_exts   = 0,
	.export_exts   = 0,
	.provides      = 0,   /* 無し */
	.hash_salt     = 0,   /* 基準カーネル/解析モジュールはソルト無し */
	/* ★ v7 (#3419): op 内並列の方式と σ (docs/srava_load_control_design.md §5.5/§5.6)。
	 *   raw pthread。⚠ ロード済みライブラリからは検出できない */
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
	return ACT_ppatsAgent_WAIT;
}

TS_STATE(ACT_ppatsAgent_WAIT)
{
	if ( ev->type == TSE_PACKET ) {
		sPtr<ptsMediatorPacket> mpkt = sPtr<ptsMediatorPacket>::d_cast(ev->msg_obj);
		if ( mpkt == thNULL )
			return 0;
		switch ( mpkt->type ) {
		case C_OP: {
			op = mpkt->str;
			if ( op == thNULL ) {
				err = thNEW(pigDataError,("pipe plugin: missing op name"));
				return rDO|ACT_ppatsAgent_ERROR;
			}
			argv.length(0);
			outCache = thNULL;
			gotEnd   = 0;
			break;
		}
		case C_ARG_DATA: {
			if ( op == thNULL ) {
				err = thNEW(pigDataError,("arg before C_OP"));
				return rDO|ACT_ppatsAgent_ERROR;
			}
			int idx = (int)mpkt->idx;
			sPtr<pigData> d = mpkt->data;
			if ( d == thNULL || d->is_error() ) {
				err = ( d != thNULL ) ? d : sPtr<pigData>(thNEW(pigDataError,("inline arg decode error")));
				return rDO|ACT_ppatsAgent_ERROR;
			}
			if ( d->is_cache() ) {
				/* プラグインは値引数のみ (process 版 serve の C_ARG_PATH 拒否と対称)。 */
				err = thNEW(pigDataError,("pipe plugin: value arguments only (got a mesh handle)"));
				return rDO|ACT_ppatsAgent_ERROR;
			}
			if ( idx >= argv.length() ) argv.length(idx + 1);
			argv[idx] = d;
			break;
		}
		case C_ARG_END: {
			outCache = sPtr<pigDataCache>::d_cast(mpkt->data);
			if ( outCache == thNULL ) {
				err = thNEW(pigDataError,(
				    "ppatsAgent: C_ARG_END without a target cache path"
				    " (planner must name the output cache)"));
				return rDO|ACT_ppatsAgent_ERROR;
			}
			gotEnd = 1;
			return rDO|ACT_ppatsAgent_STARTCALC;
		}
		default:
			break;
		}
		return 0;
	}
	/* 計算開始前なので待つ子は無い → 中断を結果に畳む (§6.3)。 */
	if ( is_destroyed() ) {
		err = thNEW(pigDataError,("aborted: agent was destroyed"));
		return rDO|ACT_ppatsAgent_ERROR;
	}
	return 0;
}

TS_STATE(ACT_ppatsAgent_STARTCALC)   /* 全入力 inline なので待ちなく計算 thread を起こす */
{
	/* argv はメンバ (計算本体がポインタで保持・寿命中生存)。目標 cache パスも渡す (未使用でも API 一致)。 */
	calc = thNEW(ppaCompute,(ifThis, &argv, outCache->get_path(), op));
	return ACT_ppatsAgent_CALC;   /* calc の TSE_RETURN 待ち */
}

TS_STATE(ACT_ppatsAgent_CALC)
{
	if ( ev->type == TSE_RETURN && ev->source == calc ) {
		sPtr<pigData> cr = calc->get_result();
		/* destroy されていたら結果を捨てて中断 (calc のエラーがあれば優先リレー)。 */
		if ( is_destroyed() ) {
			err = ( cr != thNULL && cr->is_error() ) ? cr
			    : sPtr<pigData>(thNEW(pigDataError,("aborted: agent was destroyed")));
			return rDO|ACT_ppatsAgent_ERROR;
		}
		if ( cr != thNULL && cr->is_error() ) {
			err = cr;   /* エラー値をそのまま運ぶ (message 抽出は Mediator の仕事) */
			return rDO|ACT_ppatsAgent_ERROR;
		}
		/* 値出力: 結果を出力 cache へ set_body。親 (ptsAgentApplication / planner) が
		 * A_SAVE_BEGIN に相乗りで値を返す (mfatsAgent の値返し op と同一の一本道)。 */
		outCache->set_body(cr);
		set_result(sPtr<pigData>::d_cast(outCache));
		return rDO|FIN_START;
	}
	/* destroy の作法 (ひさ指示): 子へ destroy を送り TSE_RETURN を待つ。即 FIN しない。 */
	if ( is_destroyed() ) {
		if ( calc.is_notNull() ) { calc->destroy(); return 0; }
		err = thNEW(pigDataError,("aborted: agent was destroyed"));
		return rDO|ACT_ppatsAgent_ERROR;
	}
	return 0;
}

TS_STATE(ACT_ppatsAgent_ERROR)
{
	set_result( ( err != thNULL ) ? err
	    : sPtr<pigData>(thNEW(pigDataError,("pipe plugin error"))) );
	return rDO|FIN_START;
}

TS_STATE(FIN_START)
{
	calc     = thNULL;
	outCache = thNULL;
	err      = thNULL;
	op       = thNULL;
	argv.length(0);
	return rDO|FIN_ptsAgent_START;
}
