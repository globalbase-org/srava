/*
 * ptsGenericAgent — モジュール実行体の共通基底 (ptsAgent 派生)。
 *
 * 経緯: cgatsAgent / mfatsAgent / d2atsAgent / d3atsAgent / ppatsAgent は **状態機械
 *   (WAIT/STARTCALC/CALC/ERROR/FIN) が完全に同一**で、モジュール固有部は OPS[] 表と記述子だけ
 *   だった (~230 行 × 5 のコピペ)。その generic dispatch 状態機械をこの 1 クラスに集約する。
 *   各モジュール agent は `: ptsGenericAgent` へ派生し、**OPS[] を virtual (agent_ops/agent_n_ops)
 *   で返すだけ**にする (状態機械は書かない)。demo (同期・値のみ) は対象外で従来どおり ptsAgent 直派生。
 *
 * ★ 配置: この基底は **host 層 (pig/)** に置く (planner + srava_agent + probe にリンク)。派生クラス
 *   (mfatsAgent 等) は各 .so 内で別名なので、複数 .so 同時 dlopen でも派生シンボルは衝突しない。
 *   共通の状態機械はここ 1 個だけ = 単一の真実。派生の mkCalc 関数ポインタ (OPS[].mkCalc) は .so 内を
 *   指すが、host の -rdynamic + RTLD_GLOBAL で解決される (ptsCalcBody と同じ host インフラ扱い)。
 *
 * 流れ (mfatsAgent から踏襲):
 *   INI      : opIdx 初期化 → WAIT
 *   WAIT     : C_OP で agent_ops() を検索 (無→ERROR)。C_ARG_DATA を型と照合して収集 (狂い→ERROR)。
 *              cache 入力は get_body を先行起動。C_ARG_END で出力 cache を確定し STARTCALC へ。
 *   STARTCALC: cache 引数を get_body で実体化 (ランデブー収束点) → OPS[].mkCalc で計算本体起動。
 *   CALC     : 計算本体の TSE_RETURN を待ち、get_result → outCache へ set_body → set_result → FIN。
 *   ERROR    : err (pigDataError) を set_result → FIN (ワイヤ化は親 Mediator の役割)。
 */
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/ptsApplication.h"   /* ptsApp 値メンバの完全型 */
#include	"pig/c++/ptsAgent.h"         /* 基底 (演算実行体) */
#include	"pig/c++/pigData.h"
#include	"pig/c++/pigOpEntry.h"       /* 共通 op エントリ型 */
#include	"pig/c++/pigwire.h"
#include	"pig/c++/ptsMediatorPacket.h"     /* Internal 経路の pigData 直渡しパケット */
#include	"pig/c++/ptsDataCache.h"
#include	"pig/c++/ptsCalcBody.h"
#include	"ts2/c++/stdEvent.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ptsGenericAgent_.h"

#include	<string.h>
#include	<stdlib.h>   /* getenv */
#include	<unistd.h>   /* usleep (テスト用の計算遅延) */
#include	<stdio.h>
#include	<sys/time.h>
#include	<string>     /* P3: 消費型リストの一時文字列 */
#include	<vector>

CLASS_TINYSTATE(pig/c++/ptsGenericAgent,pig/c++/ptsAgent)

/* ★ P3 (⑤ cross-module 変換): この agent が **消費できる型のリスト** (= 自 op の sig 出力型のうち
 *   幾何型 "value" 以外・重複除去) を集める。これを入力キャッシュに渡すと、キャッシュが自分の 4CC から
 *   reader_for で読める候補を選ぶ (pigDataCache::get_body(wantTypes,n))。次元概念・型選択ロジックは
 *   ここには無い (キャッシュ側が 4CC で解決) = ptsGenericAgent は mesh/次元に非依存のまま。 */
static void pgts_consumable_types(const pigOpEntry* ops, int nops, std::vector<std::string>& out) {
	for ( int i = 0 ; i < nops ; ++i ) {
		const char* sig = ops[i].sig;
		if ( sig == 0 ) continue;
		for ( const char* p = ::strstr(sig, "->") ; p != 0 ; p = ::strstr(p, "->") ) {
			p += 2;
			const char* e = ::strchr(p, ';');
			std::string t(p, e ? (size_t)(e - p) : ::strlen(p));
			if ( t != "value" && ! t.empty() ) {
				bool seen = false;
				for ( size_t k = 0 ; k < out.size() ; ++k ) if ( out[k] == t ) { seen = true; break; }
				if ( ! seen ) out.push_back(t);
			}
			if ( e == 0 ) break;
			p = e + 1;
		}
	}
}

/* PIG_TIMING にファイルパスを設定すると各フェーズ境界の経過 ms をそこへ追記する (性能内訳計測用)。
 * agent は sh -c 経由起動で stderr が親に届かないためファイル出力 (旧 mfts_timing/cgts_timing を統一)。 */
/* ★ #3427 ④: 基準時刻 t0 は旧・関数内 static (可変) → 呼び手 (agent インスタンス) が
 * メンバ timingT0 で保持する形へ (per-agent 基準・リエントラント)。 */
static void pgts_timing(const char* tag, double& t0) {
	const char* path = ::getenv("PIG_TIMING");
	if ( path == 0 || path[0] == 0 ) return;
	struct timeval tv; ::gettimeofday(&tv, 0);
	double now = tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
	if ( t0 < 0 ) t0 = now;
	FILE* f = ::fopen(path, "a");
	if ( f ) { ::fprintf(f, "[timing pid=%d] %-14s rel=%9.1f ms abs=%.1f ms\n", (int)::getpid(), tag, now - t0, now); ::fclose(f); }
}

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	ptsGenericAgent_(
		sPtr<ptsObject> parent);

	sRptr<ptsObject,tinyState>		parent;
protected:
	/* ★ 派生 (mfatsAgent 等) が override して自分の OPS 表 / 名前を返す。基底の状態機械はこの
	 * virtual 経由で dispatch するので、派生は OPS[] とこの override だけ書けばよい (状態機械は書かない)。 */
	virtual const pigOpEntry*	agent_ops();     /* 既定 0 (基底は直接使わない) */
	virtual int			agent_n_ops();   /* 既定 0 */
	virtual const char*		agent_name();    /* エラーメッセージ用。既定 "agent" */

	int	lookup_op(const char* name);   /* agent_ops()/agent_n_ops() を検索。無=-1 */

	sPtr<ptsCalcBody>	calc;
	sArray<sPtr<pigData> >	argv;     /* arg_index で収集した入力 (cache は pigDataCache ハンドル) */
	sArray<sPtr<pigData> >	cargs;    /* 計算本体へ渡す実体化済み入力 (cache は get_body 済み) */
	sPtr<pigDataCache>	outCache;    /* C_ARG_END で渡された出力ハンドル (Internal は planner と共有) */
	sPtr<pigData>		result;      /* 計算結果 */
	sPtr<pigData>		err;         /* エラーは pigData (pigDataError) のまま運ぶ */
	int			opIdx;       /* 現 op (OPS index)。-1=未設定 */
	int			gotEnd;      /* C_ARG_END 受信済み */
private:
	double		timingT0;   /* PIG_TIMING 基準時刻 (pgts_timing・#3427 ④) */
	TS_DEFARGS
};

TS_END_IMPLEMENT

TS_BEGIN_INTERFACE
#include	"ts2/c++/sRptr.h"
#include	"ts2/c++/sArray.h"
#include	"ts2/c++/stdString.h"
#include	"pig/c++/pigOpEntry.h"
class tinyState;
class ptsObject;
class ptsCalcBody;
class pigData;
class stdString;
TS_END_INTERFACE

#endif


ptsGenericAgent_::ptsGenericAgent_(TS_ARGS0)
        : ptsAgent_(parent),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
    opIdx   = -1;
    gotEnd  = 0;
    timingT0 = -1.0;   /* PIG_TIMING の基準時刻 (旧・関数内 static → per-agent メンバ・#3427 ④) */
}

/* 基底の既定 (派生が override する)。基底単体は実行体として使わないので空でよい。 */
const pigOpEntry*	ptsGenericAgent_::agent_ops()   { return 0; }
int			ptsGenericAgent_::agent_n_ops() { return 0; }
const char*		ptsGenericAgent_::agent_name()  { return "agent"; }

int
ptsGenericAgent_::lookup_op(const char* name)
{
	const pigOpEntry* ops = agent_ops();
	int n = agent_n_ops();
	if ( ops == 0 || name == 0 ) return -1;
	for ( int i = 0 ; i < n ; ++i )
		if ( ops[i].op && ::strcmp(ops[i].op, name) == 0 ) return i;
	return -1;
}


/*******************************************
	STATE MACHINE (旧 mfatsAgent と同一・generic 化)
********************************************/

TS_STATE(INI_ptsAgent_START)   /* ptsAgent 派生: 通信は parent(Mediator)が確立済み */
{
	opIdx = -1;
	return ACT_ptsGenericAgent_WAIT;
}

TS_STATE(ACT_ptsGenericAgent_WAIT)
{
	if ( ev->type == TSE_PACKET ) {
		/* 着信は **常に ptsMediatorPacket (pigData 直渡し)** — External でも agent 側 Mediator
		 * (ptsAgentApplication) がワイヤを pigData に復号してから渡す。よって実行体は文字列も
		 * PATH/INLINE の弁別も値パースも知らない。 */
		sPtr<ptsMediatorPacket> mpkt = sPtr<ptsMediatorPacket>::d_cast(ev->msg_obj);
		if ( mpkt == thNULL )
			return 0;
		switch ( mpkt->type ) {
		case C_OP: {
			opIdx = ( mpkt->str != thNULL ) ? lookup_op(mpkt->str->get_str()) : -1;
			if ( opIdx < 0 ) {
				err = thNEW(pigDataError,(thNEW(stdString,("unknown op: "))->add(
				    mpkt->str != thNULL ? mpkt->str : sPtr<stdString>(thNEW(stdString,(""))))));
				return rDO|ACT_ptsGenericAgent_ERROR;
			}
			argv.length(0);
			outCache = thNULL;
			result   = thNULL;   /* AK_CACHE では触らないため前 op の値が残らないよう明示リセット */
			gotEnd  = 0;
			break;
		}
		case C_ARG_DATA: {
			if ( opIdx < 0 ) {
				err = thNEW(pigDataError,("arg before C_OP"));
				return rDO|ACT_ptsGenericAgent_ERROR;
			}
			const pigOpEntry& e = agent_ops()[opIdx];
			int idx = (int)mpkt->idx;
			sPtr<pigData> d = mpkt->data;
			if ( d == thNULL || d->is_error() ) {
				/* 値リテラルの復号失敗 (Mediator の pig_value_parse が pigDataError を返した)。 */
				err = ( d != thNULL ) ? d : sPtr<pigData>(thNEW(pigDataError,("inline arg decode error")));
				return rDO|ACT_ptsGenericAgent_ERROR;
			}
			pigArgKind kind = d->is_cache() ? AK_CACHE : AK_INLINE;
			if ( idx >= e.nin && ! e.variadic ) {
				char b[160];
				::snprintf(b, sizeof b, "%s: too many arguments (takes %d)", e.op, e.nin);
				err = thNEW(pigDataError,(b));
				return rDO|ACT_ptsGenericAgent_ERROR;
			}
			if ( ( idx < e.nin ? e.in[idx] : AK_CACHE ) != kind ) {
				const char* want = ( ( idx < e.nin ? e.in[idx] : AK_CACHE ) == AK_CACHE ) ? "a mesh" : "a value (number/array)";
				const char* got  = ( kind == AK_CACHE ) ? "a mesh" : "a value";
				char b[192];
				::snprintf(b, sizeof b, "%s: argument %d should be %s, got %s",
				           e.op, idx + 1, want, got);
				err = thNEW(pigDataError,(b));
				return rDO|ACT_ptsGenericAgent_ERROR;
			}
			if ( idx >= argv.length() )    argv.length(idx + 1);
			argv[idx] = d;   /* cache は planner と共有の pigDataCache ハンドルそのもの */
			/* ★ 先読みは撤去 (2026-08-12): 引数 cache が完了前 (file 未生成) に来ることがあり、
			 * eager get_body は早すぎる。実読みは STARTCALC で consumable 型を指定して 1 回行う。 */
			break;
		}
		case C_ARG_END: {
			const pigOpEntry& e = agent_ops()[opIdx];
			if ( ( e.variadic ? argv.length() < e.nin : argv.length() != e.nin ) ) {
				char b[160];
				::snprintf(b, sizeof b, "%s: expected %d argument(s), got %d",
				           e.op, e.nin, argv.length());
				err = thNEW(pigDataError,(b));
				return rDO|ACT_ptsGenericAgent_ERROR;
			}
			/* 出力先は planner が必ず指定する (mpkt->data)。欠落は指定外キャッシュへの黙り書きを招くため
			 * フォールバックせず A_ERROR で fail-fast する (agent process には診断チャネルが無い)。 */
			outCache = sPtr<pigDataCache>::d_cast(mpkt->data);
			if ( outCache == thNULL ) {
				char b[160];
				::snprintf(b, sizeof b,
				    "%s: C_ARG_END without a target cache path (planner must name the output cache)",
				    agent_name());
				err = thNEW(pigDataError,(b));
				return rDO|ACT_ptsGenericAgent_ERROR;
			}
			gotEnd = 1;
			return rDO|ACT_ptsGenericAgent_STARTCALC;
		}
		default:
			break;
		}
		return 0;
	}
	/* 計算開始前なので待つ子は無い → 中断を結果として返して畳む。入力キャッシュの読込 helper は
	 * 走っているかもしれないが、キャッシュの面倒は親 (ptsAgentApplication/ptsApplication) が見る約束。 */
	if ( is_destroyed() ) {
		err = thNEW(pigDataError,("aborted: agent was destroyed"));
		return rDO|ACT_ptsGenericAgent_ERROR;
	}
	return 0;
}

TS_STATE(ACT_ptsGenericAgent_STARTCALC)   /* 全入力が揃った → 計算本体起動 */
{
	pgts_timing("parse_done", timingT0);
	/* テスト用: 計算を遅くして planner の SIGINT が評価中に確実に届くようにする。 */
	if ( ::getenv("PIG_TEST_SLOW") != 0 )
		::usleep(200000);
	/* cache 引数を get_body で実体化 = ランデブー収束点。未ロードの引数があれば get_body が
	 * listen+sException で抜け、helper の TSE_DESTROY でこの状態が再走する (冪等)。TS_STATE で
	 * 実体化してから渡すのは「TS_THREAD(compute)内から pigDataCache に触らない」(WSM-FgT) ため。 */
	cargs.length(argv.length());
	/* ★ P3 (⑤ cross-module 変換): この agent が消費できる型リスト (自 op の sig 出力型) を 1 度だけ集める。
	 *   各入力キャッシュにこれを渡すと、キャッシュが自分の 4CC から reader_for で読める候補を選び、必要なら
	 *   file を変換読みして返す (pigDataCache::get_body(wantTypes,n))。型選択は cache 側 (4CC)・agent は
	 *   「欲しい型」を渡すだけ。次元概念なし。 */
	std::vector<std::string> wantS;
	pgts_consumable_types(agent_ops(), agent_n_ops(), wantS);
	std::vector<const char*> wantC;
	for ( size_t k = 0 ; k < wantS.size() ; ++k ) wantC.push_back(wantS[k].c_str());
	for ( int i = 0 ; i < argv.length() ; ++i ) {
		if ( argv[i] != thNULL && argv[i]->is_cache() ) {
			sPtr<pigDataCache> ic = sPtr<pigDataCache>::d_cast(argv[i]);
			/* canonical が自 module 型ならそのまま (in-memory fast path)・foreign なら 4CC から自型へ変換読み。
			 * in-proc/process 同一経路 (process は canonical reader が既に自型へ整えている・in-proc は
			 * in-memory body を bypass して file 変換)。 */
			cargs[i] = ic->get_body(wantC.empty() ? 0 : &wantC[0], (int)wantC.size());
			if ( cargs[i] == thNULL ) {
				char b[192];
				::snprintf(b, sizeof b,
				    "%s: input %d failed to read/decode its cache (possible cache race — try clearing the cache dir or rerun)",
				    agent_ops()[opIdx].op, i + 1);
				err = thNEW(pigDataError,(b));
				return rDO|ACT_ptsGenericAgent_ERROR;
			}
		} else
			cargs[i] = argv[i];
	}
	calc = agent_ops()[opIdx].mkCalc(ifThis, &cargs, outCache->get_path());
	return ACT_ptsGenericAgent_CALC;
}

TS_STATE(ACT_ptsGenericAgent_CALC)
{
	if ( ev->type == TSE_RETURN && ev->source == calc ) {
		pgts_timing("compute_done", timingT0);
		/* 計算本体の結果を get_result() 1 回で引く。エラーか本文かは cr->is_error() で分岐 (排他)。 */
		sPtr<pigData> cr = calc->get_result();
		if ( is_destroyed() ) {
			err = ( cr != thNULL && cr->is_error() ) ? cr
			    : sPtr<pigData>(thNEW(pigDataError,("aborted: agent was destroyed")));
			return rDO|ACT_ptsGenericAgent_ERROR;
		}
		if ( cr != thNULL && cr->is_error() ) {
			err = cr;   /* エラー値をそのまま運ぶ (message 抽出は Mediator の仕事) */
			return rDO|ACT_ptsGenericAgent_ERROR;
		}
		/* 結果本文を出力 pigDataCache へ set_body → 保存 helper (ptsDataCache SAVE) が即起動。
		 * Internal は planner が渡した共有ハンドル (set_body = planner の in-memory body になる)。
		 * 保存の見届け (is_valid→A_SAVE_BEGIN、is_complete→A_SAVE_DONE) は親の仕事。 */
		outCache->set_body(cr);
		set_result(sPtr<pigData>::d_cast(outCache));
		return rDO|FIN_START;
	}
	/* destroy の作法: 子へ destroy を送り TSE_RETURN が戻るのを待ち続ける (即 FIN しない)。 */
	if ( is_destroyed() ) {
		if ( calc.is_notNull() ) { calc->destroy(); return 0; }
		err = thNEW(pigDataError,("aborted: agent was destroyed"));
		return rDO|ACT_ptsGenericAgent_ERROR;
	}
	return 0;
}

TS_STATE(ACT_ptsGenericAgent_ERROR)
{
	/* エラーも pigData のまま親へ返す。ワイヤ化 (A_ERROR + 生テキスト) は親の役割。 */
	set_result( ( err != thNULL ) ? err
	    : sPtr<pigData>(thNEW(pigDataError,("agent error"))) );
	return rDO|FIN_START;
}

TS_STATE(FIN_START)
{
	calc     = thNULL;
	outCache = thNULL;
	err      = thNULL;
	result   = thNULL;
	argv.length(0);
	cargs.length(0);
	return rDO|FIN_ptsAgent_START;
}
