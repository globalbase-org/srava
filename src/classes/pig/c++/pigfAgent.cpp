/*
 * pigfAgent — 演算子の実体計算を別プロセス(srava-agent)へ委ねる関数 helper(pigfFunction 派生)。
 * pigDataFunction<pigfAgent> ノードが compact 時に起動する。
 *
 * 真骨頂(pigDataPair / pigDataCache / 継続):
 *   1. 演算子 + 各引数の get_hashkey() を畳んだ「結果ハッシュ」でキャッシュパスを決める。
 *      同一演算・同一引数の結果は常に同じ前提(ハッシュは「何の演算結果か」を表す)。
 *   2. 遅延引数(= 上流 pigfAgent の継続 pair)が無く、キャッシュファイルが既にあれば
 *      **agent を起動せず短絡**(データキャッシュは本文を読んで返す)。
 *   3. キャッシュミス/遅延引数ありなら、まず **3 段継続** ("delayed" . beginPromise) を front へ即セットして
 *      呼び元を解放(非ブロッキング)。C_ARG_END 送信(=計算開始=admitted)で beginPromise→("begin".promise)、
 *      A_SAVE_BEGIN(保存開始)で promise→結果(mesh は pigDataCache・値は pigDataString)を解決。
 *      親は cdr()->car()=="begin" で「子が計算を始めた」を、cdr()->cdr() で結果を取る。
 *      ★ ゲートは「全子が begin した後に親が cap を取る」admission 順 → 構造的デッドロックフリー(GATE 参照)。
 *
 * プロトコル(pigwire.h, このスケルトンでの取り決め):
 *   plan→agent : C_OP(op 名), [C_ARG_PATH(入力キャッシュパス)|C_ARG_INLINE(テキスト)]*,
 *                C_ARG_END(payload=**目標キャッシュパス**), W_END
 *   agent→plan : A_SAVE_BEGIN(payload=データキャッシュ本文 or 空), A_SAVE_DONE, A_BYE, W_END
 *
 * 引数の値による分岐:
 *   - pigDataCache(上流結果の mesh ハンドル) → C_ARG_PATH(get_str()=キャッシュパス)
 *   - それ以外(pigDataString 等)            → C_ARG_INLINE(get_str()=テキスト)
 *   - pigDataPair(遅延継続)                  → cdr を compact 待ち(ts2Parallel worker が yield)して上記へ
 *
 * agent 依存の供給(派生クラスが override):
 *   - agent_cmd()      : 起動する外部プロセス(基底 thNULL。pigfSravaAgent が srava-agent)
 *   - agent_op_name()  : 演算子名(C_OP + ハッシュ。基底 thNULL。pigfSravaAgent が srava-op)
 *   キャッシュ dir は pigfFunction env の "CACHE_DIR" を参照(getenv 直読みしない)。
 *
 * 割り切り(TODO):
 *   - op 名は派生固定(front 由来にするのは別途)。キャッシュ本文の型判定(データ/mesh)は
 *     「A_SAVE_BEGIN payload の有無」で代用。HIT 読みは常にデータキャッシュ(D_TEXT)前提。
 *   - pigDataCache の global dedup list は未実装。
 */
#include	"pig/c++/pigfFunction.h"
#include	"pig/c++/ptsApplication.h"   /* ptsApp->agent_enter/leave/set_agentError(全 agent 集約) */
#include	"pig/c++/pigCacheManager.h"  /* 起動時キャッシュ sweep(pig 層・cgptsPlanner から移設) */
#include	"pig/c++/pigData.h"
#include	"pig/c++/pigwire.h"
#include	"pig/c++/ptsWirePipe.h"
#include	"pig/c++/ptsWirePacket.h"
#include	"pig/c++/ptsWireCacheStreamReaderText.h"
#include	"ts2/c++/ts2System.h"
#include	"ts2/c++/ts2Parallel.h"
#include	"ts2/c++/ts2IO.h"
#include	"ts2/c++/stdEvent.h"
#include	"_ts2/c++/pigfAgent_.h"

#include	"pig/c++/osglue.h"   /* osglue_mkdir_p(mkdir -p 相当) */

#include	<stdlib.h>   /* getenv */
#include	<stdio.h>    /* snprintf */
#include	<stdint.h>
#include	<unistd.h>   /* access */

CLASS_TINYSTATE(pig/c++/pigfAgent,pig/c++/pigfFunction)

/* 基底: 演算子固有の短絡なし(派生 = 言語層が override)。0=非該当。 */
int pigfAgent_::try_shortcircuit() { return 0; }

/* 基底: 値パーサ無し(言語パーサは srava 固有)。thNULL = 生テキストを pigDataString として返す。 */
sPtr<tinyState> pigfAgent_::make_value_parser(sPtr<stdString>) { return thNULL; }

/* 基底: 同期の値パース無し。thNULL = 非対応(make_value_parser か生テキストへフォールバック)。
 * pigfPluginAgent は pig_value_parse(同期)を override する(子状態機械が要らない軽量経路)。 */
sPtr<pigData> pigfAgent_::parse_value_text(sPtr<stdString>) { return thNULL; }

/* 基底: agent コマンド未設定(派生が override)。pigfAgent 自体は特定 agent に非依存。 */
sPtr<stdString>
pigfAgent_::agent_cmd()
{
	return thNULL;
}

/* 演算子名は front(ノード)由来(C_OP + 結果ハッシュ + cgatsAgent の dispatch キー)。
 * 未設定なら thNULL(空扱い)。agent 種別ではなくノード演算ごとに決まるので派生で固定しない。 */
sPtr<stdString>
pigfAgent_::agent_op_name()
{
	return front->get_op_name();
}

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	pigfAgent_(
		sPtr<ptsObject> parent,
		sPtr<pigDataOperator> _front);

	sRptr<ptsObject,tinyState>		parent;
protected:
	/* 起動する agent コマンド。pigfAgent は piggybackTurtle 汎用で特定 agent を知らない
	 * → 基底は thNULL(未設定=エラー)。派生クラス(例 pigfSravaAgent)が override して供給する。
	 * 将来 video 編集 agent / 巨大テクスチャ agent 等を別派生で混在可能にするためのフック。 */
	virtual sPtr<stdString>	agent_cmd();
	/* 演算子名(C_OP で送る + 結果ハッシュに混ぜる)。これも agent 依存なので派生が供給。
	 * 基底は thNULL(空扱い)。 */
	virtual sPtr<stdString>	agent_op_name();
	/* ★ pig / 言語層(srava)の境界フック。pigfAgent は piggybackTurtle 汎用で特定演算子も特定言語の
	 * 値構文も知らない。言語固有の知識は派生(pigfSravaAgent)が override して供給する:
	 *   try_shortcircuit: 演算子固有の代数的短絡(srava の単位元 {} 等で CGAL を呼ばず畳む)。
	 *     戻り 0=非該当(agent 起動へ) / 1=front に結果セット済み(FIN) / 2=err セット済み(ERROR)。基底=0。
	 *   make_value_parser: agent の値(非 mesh)結果テキスト → 構造化 pigData へ復元する子状態機械。
	 *     子は **TSE_RETURN で pigData を返す**(pig の約束)。基底=thNULL(値パーサ無し=生テキストを返す)。 */
	virtual int		try_shortcircuit();
	virtual sPtr<tinyState>	make_value_parser(sPtr<stdString> text);
	/* 同期版の値パース(子状態機械が不要な軽量経路)。基底=thNULL(非対応)。pigfPluginAgent が
	 * pig_value_parse を override。make_value_parser(async)より先に試す。 */
	virtual sPtr<pigData>	parse_value_text(sPtr<stdString> text);
	sPtr<ts2System>		agent;
	sPtr<ptsWirePipe>	pipe;
	sPtr<ts2Parallel>	par;
	sPtr<ptsWireCacheStreamReaderText>	reader;
	/* 値(インライン)結果を構造化 pigData に復元する子状態機械(make_value_parser が生成)。
	 * 型は汎用 tinyState(言語パーサ cgptsLemonParser は srava 固有なので派生が作る)。HIT/MISS
	 * 両経路で spawn し、子の TSE_RETURN(ev->msg_obj=pigData)で結果を取る。 */
	sPtr<tinyState>		vparser;
	sPtr<stdString>		valText;   /* パース失敗時の生テキストフォールバック用に保持 */
	sPtr<ts2IO>		rfd;
	sPtr<ts2IO>		wfd;
	sPtr<pigDataPromise>	promise;       /* 結果(cdr->cdr)。A_SAVE_BEGIN で hashCache に解決 */
	sPtr<pigDataPromise>	beginPromise;  /* 計算開始(cdr)。SENDEND(C_ARG_END)で ("begin".promise) に解決。
	                                        * 親はこれで「子が計算を始めた=admitted」を知り、全子 begin で gate を取る */
	int			beginResolved;  /* beginPromise->set_result 済みか(撤収時の二重解決防止) */
	sPtr<pigData>		err;     /* is_error() な値そのもの(pigDataError とは限らない)。d_cast 不要 */
	sPtr<stdString>		cachePath;
	pHashKeyType		hashVal;
	int			retPid;
	int			sendIdx;
	int			promiseLive;     /* front=("delayed".promise) を返済み(継続あり) */
	int			promiseResolved; /* promise->set_result 済み(= 結果が呼び元へ渡った) */
	int			gateCredited;    /* ワーカーゲートの credit を取得済み(FIN で gate_release。取得/解放を 1:1 に保つ) */
	int			forkFails;       /* fork EAGAIN の連続失敗回数(上限超でようやくエラー) */
	int			i;
private:
	pHashKeyType    compute_arg_hash();
	sPtr<stdString> make_cache_path(pHashKeyType h);
	TS_DEFARGS
};

TS_END_IMPLEMENT

TS_BEGIN_INTERFACE
#include	"ts2/c++/sRptr.h"
#include	"pig/c++/pigData.h"   /* pHashKeyType をメンバ型に */
class ptsObject;
class pigDataOperator;
class ts2System;
class ptsWirePipe;
class ts2Parallel;
class ptsWireCacheStreamReaderText;
class ts2IO;
class pigDataPromise;
class pigDataError;
class stdString;
TS_END_INTERFACE

#endif


pigfAgent_::pigfAgent_(TS_ARGS0)
        : pigfFunction_(parent,_front),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
    hashVal         = 0;
    retPid          = 0;
    sendIdx         = 0;
    promiseLive     = 0;
    promiseResolved = 0;
    beginResolved   = 0;
    gateCredited    = 0;
    forkFails       = 0;
    i               = 0;
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

/* 演算子名 + 各引数の get_hashkey() を FNV-1a で畳む。この関数が呼ばれる時点で引数は
 * 解決済み(compact 済)。遅延継続 ("delayed" . promise) は car=="delayed" で判別し、実値
 * (cdr の promise。get_hashkey は compact ゲートで実値の hash)を使う。d_cast/明示 compact は使わない。 */
pHashKeyType
pigfAgent_::compute_arg_hash()
{
	uint64_t h = 1469598103934665603ULL;
	const uint64_t prime = 1099511628211ULL;
	sPtr<stdString> op = agent_op_name();
	const char *opn = ( op != thNULL ) ? op->get_str() : "";
	for (const char *p = opn; *p; ++p) { h ^= (unsigned char)*p; h *= prime; }
	for (int k = 0; k < args.length(); ++k) {
		sPtr<pigData> v = args[k];
		if (v->car()->get_str()->cmp("delayed") == 0)   /* 遅延継続 → 実値(cdr->cdr: "begin" 段を飛ばす) */
			v = v->cdr()->cdr();
		uint64_t a = (uint64_t)v->get_hashkey();
		for (int b = 0; b < 8; ++b) { h ^= (a >> (8*b)) & 0xff; h *= prime; }
	}
	return (pHashKeyType)h;
}

sPtr<stdString>
pigfAgent_::make_cache_path(pHashKeyType h)
{
	/* キャッシュ dir は pigfFunction の env 変数 "CACHE_DIR" を参照(getenv 直読みしない)。
	 * srava 起動時に getenv で既定がセットされ、実行中に set_var で変更も可能。
	 * 未定義/エラー(get_var が pigDataError)なら安全な既定にフォールバック。 */
	sPtr<pigData> cv = env->get_var(thNEW(stdString,("CACHE_DIR")));
	const char *dir = ( cv != thNULL && !cv->is_error() ) ? cv->get_str()->get_str()
	                                                       : "/tmp/srava-cache";
	/* mkdir -p 相当で中間 dir も作る(::mkdir は親が無いと失敗)。毎回呼ぶが既存なら no-op。 */
	osglue_mkdir_p(dir, 0777);
	/* キャッシュ dir 初期化(起動時スイープ)を **最初に動いた agent でこの位置から** 1 度だけ実行する。
	 * ここまでにプログラムが CACHE_DIR を set_var で自由に変えられる(planner INI で先食いしない)。
	 * once フラグ・版数指紋は ptsApp(=planner)が保持(per-planner)→ 同一プロセスで複数 planner でも
	 * 各々が自分の dir を 1 度だけ sweep。機構は pig 層の pigCacheManager。 */
	if ( ptsApp.is_notNull() && ptsApp->cache_take_startup() )
		pigCacheManager::startup_sweep(dir, ptsApp->cache_fingerprint());
	char buf[600];
	pigCacheManager::make_path(buf, sizeof buf, dir, h);
	return thNEW(stdString,(buf));
}


/*******************************************
	STATE MACHINE
********************************************/

/* 撤収すべきか: 他 agent がエラー集約した / 自分が destroy された。
 * イベント待ち状態の先頭で呼び、真なら ABORT(agent を kill して FIN)。 */
#define PIGFAGENT_SHOULD_ABORT() \
	( ( ptsApp.is_notNull() && ptsApp->get_agentError() != thNULL ) || is_destroyed() )

TS_STATE(INI_pigfFunction_START)
{
	if ( ptsApp.is_notNull() )
		ptsApp->agent_enter(ifThis);   /* 生存数 ++ + 登録(FIN で leave)。全 agent 完了判定 + 撤収用 */
	return rDO|ACT_START;
}

TS_STATE(ACT_START)
{
	/* 既に他 agent がエラー集約済みなら、ここで起動準備に入らず即撤収(無駄な promise/fork を避ける)。
	 * abort 経路では呼び元も同時に撤収するので front 未解決でハングしない(set_agentError の wake-all)。 */
	if ( PIGFAGENT_SHOULD_ABORT() ) return rDO|ACT_pigfAgent_ABORT;

	/* 1) 引数エラー検査(is_error() は遅延ノードでも compact ゲートで解決。継続は delayed pair を即返す
	 *    ので非ブロッキング)。is_error() が真なら args[i] 自体がエラー値 → そのまま使う(d_cast 不要)。 */
	for ( i = 0 ; i < args.length() ; i++ )
		if ( args[i]->is_error() ) {
			err = args[i];
			return rDO|ACT_pigfAgent_ERROR;
		}

	/* 1.5) ★ 演算子固有の代数的短絡(言語層フック)。srava なら単位元 {} で CGAL を呼ばず畳む等を
	 *   pigfSravaAgent::try_shortcircuit が行う。pigfAgent 自体は特定演算子を知らない(基底=0)。 */
	{
		int sc = try_shortcircuit();
		if ( sc == 1 ) return rDO|FIN_START;              /* front に結果セット済み */
		if ( sc == 2 ) return rDO|ACT_pigfAgent_ERROR;    /* err セット済み */
	}

	/* 2) 遅延継続検出。継続は ("delayed" . promise) なので car=="delayed" で判別(d_cast 不要)。
	 *    あれば今はハッシュ不能 → キャッシュ判定を飛ばす。 */
	int hasDelay = 0;
	for ( i = 0 ; i < args.length() ; i++ )
		if ( args[i]->car()->get_str()->cmp("delayed") == 0 ) { hasDelay = 1; break; }

	/* 3) 遅延無し → 結果ハッシュ確定 → キャッシュファイルがあれば agent 起動せず短絡 */
	if ( !hasDelay ) {
		hashVal   = compute_arg_hash();
		cachePath = make_cache_path(hashVal);
		if ( ::access(cachePath->get_str(), F_OK) == 0 ) {
			if ( ptsApp.is_notNull() ) ptsApp->cache_hit();   /* HIT 計上 */
			return rDO|ACT_pigfAgent_CACHEREAD;   /* HIT */
		}
		/* ★ in-flight dedup(最も綺麗な位置・ユーザ案): hashVal 確定 & ファイル未 HIT。継続(/ * 4))を
		 * **まだ作っていない=front 未解決**なので、先行 pigfAgent があれば `front->set_result(その front)`
		 * の **統一形**(out_cache 0/1 を区別しない)で受け売りして撤収できる。呼び元は dupFront→firstFront
		 * を不動点で辿る。自分が最初なら front を登録してこのまま計算へ。 */
		if ( ptsApp.is_notNull() ) {
			sPtr<pigData> firstFront = ptsApp->inflight_claim(hashVal, front);
			if ( firstFront.is_notNull() ) {       /* 先行あり = 次点 */
				front->set_result(firstFront);     /* dupFront → firstFront(継続/値どちらでも不動点で解決) */
				ptsApp->cache_hit();
				return rDO|FIN_START;              /* 起動せず撤収(無駄 fork/計算なし) */
			}
		}
	}

	
	/* 4) ミス or 遅延引数あり → 継続を即返して呼び元を解放。
	 *    ただし継続 ("delayed".promise) pair は **mesh 出力(out_cache)専用**。これは下流 pigfAgent が
	 *    arg を compact せず car() で「未解決」を覗き見て起動を遅延させる mesh-DAG パイプラインの仕掛け
	 *    で、cdr を辿るのは pigfAgent だけ。値返し op(out_cache==0)は式の演算子等(pigfAgent 以外)が
	 *    消費するので pair を返さず、front を未解決のまま残して **完了時に front を実値へ直接解決**する
	 *    (消費側は通常の pigDataDelay compact ゲートで await→実値を得る。HIT 経路と同じ扱い)。 */
	if ( front->get_out_cache() ) {
		promise      = thNEW(pigDataPromise,(ifThis));   /* 結果。A_SAVE_BEGIN で hashCache へ */
		beginPromise = thNEW(pigDataPromise,(ifThis));   /* 計算開始。SENDEND で ("begin".promise) へ */
		promiseLive  = 1;
		/* ★ 継続を **3段化**: ("delayed" . beginPromise)。SENDEND(C_ARG_END=計算開始)で beginPromise を
		 * ("begin" . promise) に解決し、A_SAVE_BEGIN で promise を hashCache に解決する。
		 * 親は cdr()->car()=="begin" で「子が計算を始めた(admitted)」を、cdr()->cdr() で結果を取る。 */
		front->set_result(thNEW(pigDataPair,(thNEW(pigDataString,("delayed")), beginPromise)));
	}

	/* 5) agent の起動タイミング(★ワーカーゲート):
	 *    - 遅延引数なし(全 ready) → 即 LAUNCH(hashVal は 3) で確定済み・dedup 済み)。
	 *    - 遅延引数あり → 「**最初の1つ**の遅延引数が解決するまで」待ってから起動する。引数が1つも
	 *      解決していない段階で fork すると、引数を全く得られずアイドルする agent を深い式木で大量に
	 *      生んでしまう(ワーカーゲート)。最初の解決で ts2Parallel を cancel → 直後に LAUNCH。
	 *      この時点では **全引数は揃わない=hashVal は出せない** ので、dedup は SEND(全引数送信後・
	 *      C_ARG_END 送信前=計算開始前)で行う。 */
	if ( !hasDelay )
		return rDO|ACT_pigfAgent_GATE;   /* 全引数 ready の leaf → ゲートへ(fork 絞り) */

	sendIdx = 0;
	par = thNEW(ts2Parallel,(ifThis, 0,
		[this, idx=-1, phase=0](sPtr<ts2Parallel> me, sPtr<stdEvent> wev) mutable -> int {
			if ( phase == 0 ) {
				if ( sendIdx >= args.length() )
					return 1;
				idx = sendIdx++;
				if ( sendIdx < args.length() )
					me->spawn();
				phase = 1;
			}

			if ( me->is_destroyed() )
				return 1;

			/* 遅延引数だけが待機対象。最初に **結果まで** 解決した遅延引数で全体を cancel → GATE へ。
			 * 「1つの子の計算終了」を見ておく(親が fork 後にデータ来ず cap を握って待ちぼうけるのを防ぐ)。 */
			sPtr<pigData> v = args[idx];
			if ( v->car()->get_str()->cmp("delayed") == 0 ) {
				(void) v->cdr()->cdr()->get_hashkey();   /* 結果(cdr->cdr)。未解決なら compact ゲートで yield 再走 */
				me->cancel();                            /* 最初に結果が出た子で全体終了 → 起動 */
				return 1;
			}
			if ( v->is_cache() ) {   /* 既に解決済みの子(キャッシュ HIT)も「終わった子」→ 即 cancel して起動 */
				me->cancel();
				return 1;
			}
			return 1;   /* それ以外(非継続・非キャッシュ値)は待機対象でない */
		}));
		return ACT_pigfAgent_FIRSTWAIT;   /* par の TSE_RETURN(=最初の引数解決 or 全完了)待ち */
}

TS_STATE(ACT_pigfAgent_FIRSTWAIT)
{
	if ( PIGFAGENT_SHOULD_ABORT() ) return rDO|ACT_pigfAgent_ABORT;
		/* 最初の遅延引数が解決(cancel)→ ゲートへ。hashVal は全引数が揃う SEND(または GATE で全 ready 時)で確定。 */
	if ( ev->type == TSE_RETURN && ev->source == par )
		return rDO|ACT_pigfAgent_GATE;
	return 0;
}

/* ★ ワーカーゲート(admission 順・stdLimitSemaphore 版): fork(LAUNCH)直前の入場判定。生きている agent
 *   プロセス数(N=gate_live=セマフォ count)を cap に制限して fork EAGAIN/OOM を防ぐ。
 *   入場条件 = **全ての継続引数(子)が begin(計算開始=admitted)した後** に gate_get()(セマフォ取得)。
 *   - 子が begin = その子は既に fork 済みで必ず完走する。よって親が fork する時には全子が computing →
 *     スロットは順次空く → 「親がスロットを握って子の fork を待つ」デッドロックが構造的に起きない。
 *   - admission がボトムアップに揃うので、N<cap/2 のレジーム分け・gate_listen・progressive fork は不要。
 *   - 満杯なら gate_get() 内で yield → release で起こされて GATE 再走(待ち行列はセマフォ内部 stdQueue)。
 *   ⇒ 最深の admitted ノード(子が解決済み or 葉)は能動計算→完走→release → フロンティア常に前進 =
 *     構造的デッドロックフリー(cap=1 でも完走)。将来 cap 動的制御も「admitted は必ず終わる」上に乗る。 */
TS_STATE(ACT_pigfAgent_GATE)
{
	if ( PIGFAGENT_SHOULD_ABORT() ) return rDO|ACT_pigfAgent_ABORT;
	if ( ! ptsApp.is_notNull() )                 /* app 無し(単体テスト) → ゲート無効 */
		return rDO|ACT_pigfAgent_LAUNCH;

	/* ★ admission 順制御(構造的デッドロックフリー): **全ての継続引数(子)が begin(計算開始)するまで**
	 * 待ってから gate を取る。子が begin した = その子は既に admitted(fork 済み)で必ず完走する。
	 * よって親が gate を取る時点で全子は computing → スロットは順次空く → 「親がスロットを握って子の
	 * fork を待つ」デッドロックが構造的に起きない。N<cap/2 のレジーム分けも gate_listen も不要。
	 * - 未 begin の子: cdr()->car()(="begin")が compact ゲートで yield → 子の SENDEND(begin)で GATE 再走。
	 * - 非継続引数(HIT 済み/単位元 {}/エラー)は car()!="delayed" でスキップ(cdr で error にしない)。
	 * - FIRSTWAIT で「1 つの子の結果」は既に得ているので、親は fork 後すぐ streaming 読みできる。 */
	for ( i = 0 ; i < args.length() ; i++ )
		if ( args[i]->car()->get_str()->cmp("delayed") == 0 )
			(void) args[i]->cdr()->car();   /* 子の begin を待つ(未 begin なら yield 再走) */

	/* ★ メモリ watermark による入場制限は当面無効(fork 同様デッドロック源)。IF は ptsApplication に残置。 */

	/* 入場(セマフォ取得)。満杯なら gate_get() が yield → release で起こされて GATE 再走。全子 admitted の
	 * 後なので、待っても必ず誰か(より深い admitted ノード)が完走して release する = 前進が保証される。 */
	ptsApp->gate_get();
	gateCredited = 1;
	return rDO|ACT_pigfAgent_LAUNCH;
}

TS_STATE(ACT_pigfAgent_LAUNCH)
{
	/* 起動直前に再チェック: 既に撤収中なら **agent プロセスを fork しない**(死に確定のプロセスを
	 * 生んで直後に殺すレースを防ぐ)。FIRSTWAIT 経由でも !hasDelay の直行でもここを通る。 */
	if ( PIGFAGENT_SHOULD_ABORT() ) return rDO|ACT_pigfAgent_ABORT;
	sPtr<stdString> cmd = agent_cmd();   /* 派生クラスが供給(基底は thNULL) */
	if ( cmd == thNULL ) {
		err = thNEW(pigDataError,("pigfAgent: no agent command (subclass must override agent_cmd)", front->get_info()));
		return rDO|ACT_pigfAgent_ERROR;
	}
	/* テスト用: 同時 fork 数の上限を人為的に N に設定し(PIG_TEST_FORKLIMIT=N)、それを超える fork を
	 * EAGAIN のように失敗させる。実 fork 上限(ulimit)を共有環境で再現するのは不安定なため。
	 * 実際の EAGAIN と同じく「並列度が上限を超えたときだけ失敗」する → 適応バックオフが N に収束する。 */
	static int g_testForkLimit = -2;
	if ( g_testForkLimit == -2 ) {
		const char *t = ::getenv("PIG_TEST_FORKLIMIT");
		g_testForkLimit = ( t != 0 ) ? ::atoi(t) : -1;
	}
	int forcedFail = 0;
	if ( g_testForkLimit >= 0 && ptsApp.is_notNull() && ptsApp->gate_live() > g_testForkLimit )
		forcedFail = 1;

	if ( ! forcedFail )
		agent = thNEW(ts2System,(ifThis, &retPid, cmd->get_str(), &rfd, (sPtr<ts2IO>*)0, &wfd, 0));
	if ( forcedFail || retPid < 0 || rfd == thNULL || wfd == thNULL ) {
		/* fork 失敗(典型は EAGAIN=同時プロセス上限超過)。**limit は固定方針**なので、適応バックオフ
		 * (limit の動的縮小)も再試行もしない。fork が cap に届かない=PIG_MAX_WORKERS が実機の fork
		 * 上限を超えている、なので **即エラーで終了し、ユーザに手で下げて再実行してもらう**。
		 * (理由: ① 既に fork 済みの agent を殺せない以上、limit を後から縮めてもデッドロックは解けない。
		 *        ② 物理上限が後から増えるわけでもないので AIMD 回復も不合理。
		 *  メモリ容量等を含めた動的制御は将来ちゃんと整理する。) */
		agent = thNULL; rfd = thNULL; wfd = thNULL;   /* 失敗オブジェクト破棄(ret<0 は kill されない) */
		if ( gateCredited && ptsApp.is_notNull() ) { ptsApp->gate_release(); gateCredited = 0; }
		char msg[256];
		::snprintf(msg, sizeof msg,
			"fork failed (process limit): PIG_MAX_WORKERS=%d exceeds this machine's fork/process limit. "
			"Lower PIG_MAX_WORKERS and re-run.",
			ptsApp.is_notNull() ? ptsApp->gate_cap() : 0);
		err = thNEW(pigDataError,(msg, front->get_info(), 1));   /* fatal: 回復不能=即終了 */
		return rDO|ACT_pigfAgent_ERROR;
	}
	if ( ptsApp.is_notNull() ) ptsApp->cache_miss();   /* fork 成功 → MISS 計上(再試行で二重計上しない) */
	/* ★ wfd は **分割書き込みモード**にする(我々のケースでは必須)。
	 * ts2IO pipe の既定は **不可分書き込み**=指定した length をきっちり書こうとする。
	 *   - length > pipe バッファ(Linux 既定 64KB)だと write は**絶対に成功しない**(64KB 超を一度に
	 *     収められない)→ 巨大インライン引数(serialize 後 >64KB の path 配列等)で停止。
	 *   - length < 64KB でも、空きが length 未満の状態が続くと「イベントは来るが書けない」= **CPU100%
	 *     ループ**になり得る。
	 * リクエストは連続バイト列でメッセージ境界の不可分性を要しない(レコード境界は上位の長さ前置で扱う)
	 * → set_divisible() で「書けるだけ書く」のが安全かつ正しい。
	 * NB(#4): 以前の送信ストールは別件で、write_c の EAGAIN resume が writefds に積まれない不具合
	 * (tinyState #3365)も重なっていた(修正済)。暫定の F_SETPIPE_SZ 1MB 拡張は撤去。再現は repro_pipe。 */
	wfd->set_divisible();
	pipe = thNEW(ptsWirePipe,(ifThis, rfd, wfd));
	return ACT_pigfAgent_HELLO;   /* pipe handshake(TSE_ASSERT)待ち → rDO なし */
}

TS_STATE(ACT_pigfAgent_CACHEREAD)
{
	if ( PIGFAGENT_SHOULD_ABORT() ) return rDO|ACT_pigfAgent_ABORT;
	/* HIT したキャッシュも「使った」→ 登録(再入で呼ばれるが cache_use は重複無視)。 */
	if ( ptsApp.is_notNull() )
		ptsApp->cache_use(hashVal);
	/* キャッシュヒット(agent 起動なし)。出力種別で分岐(front->get_out_cache):
	 *  - cache(mesh 等): 中身を読まず pigDataCache ハンドルを返す(下流が必要時に reader で読む)
	 *  - 値(インライン): キャッシュ本文を読んで pigDataString に。 */
	if ( front->get_out_cache() ) {
		front->set_result(thNEW(pigDataCache,(hashVal, cachePath, front->get_info())));
		return rDO|FIN_START;
	}
	if ( reader == thNULL && vparser == thNULL ) {
		reader = thNEW(ptsWireCacheStreamReaderText,(ifThis, cachePath));
		return 0;
	}
	/* reader 完了 → 本文テキストを言語層の値パーサ(派生供給)で構造化。子 TSE_RETURN を待つ。
	 * 派生が値パーサを持たない(基底=thNULL)なら生テキストを pigDataString として返す。 */
	if ( ev->type == TSE_RETURN && ev->source == reader ) {
		valText = sPtr<stdString>::d_cast(ev->msg_obj);
		reader  = thNULL;
		sPtr<pigData> pv = parse_value_text(valText);   /* 同期パース(plugin) */
		if ( pv != thNULL ) { front->set_result(pv); return rDO|FIN_START; }
		vparser = make_value_parser(valText);           /* 非同期パース(srava) */
		if ( vparser == thNULL ) {
			front->set_result(thNEW(pigDataString,(valText)));
			return rDO|FIN_START;
		}
		return 0;
	}
	if ( ev->type == TSE_RETURN && ev->source == vparser ) {
		sPtr<pigData> v = sPtr<pigData>::d_cast(ev->msg_obj);
		vparser = thNULL;
		/* 想定外本文(パース不能)は agent 側のバグ → フォールバックせず明示エラー。 */
		if ( v == thNULL )
			v = thNEW(pigDataError,("agent returned unparseable value", front->get_info()));
		front->set_result(v);
		return rDO|FIN_START;
	}
	return 0;
}

/* NB: write 系は **event でガードした分岐の中で呼ばない**。write_c が yield(sException)すると
 * 状態関数は先頭から再走するが、その時 ev は I/O 準備イベントに変わっており event 分岐が
 * 成立せず write が再開できない(かつ 1 状態で複数 write すると先頭 write が二重実行される)。
 * → イベント検出状態は「検出して rDO で遷移」だけ。実 write は **event 非依存・1 状態 1 write_record**
 *   の状態で行い、yield 再走は write_record 自身の pico(ps_write_record)が安全に再開する。
 *   (ptsWirePipe が read_c を ev 非依存で呼ぶのと同じ作法) */

TS_STATE(ACT_pigfAgent_HELLO)
{
	if ( PIGFAGENT_SHOULD_ABORT() ) return rDO|ACT_pigfAgent_ABORT;
	if ( ev->type == TSE_ASSERT && ev->source == pipe )
		return rDO|ACT_pigfAgent_SENDOP;   /* handshake 完了 → 送信へ(write はしない) */
	if ( ev->type == TSE_RETURN && ev->source == pipe ) {
		err = thNEW(pigDataError,("agent closed before handshake", front->get_info()));
		return rDO|ACT_pigfAgent_ERROR;
	}
	return 0;
}

TS_STATE(ACT_pigfAgent_SENDOP)   /* event 非依存: C_OP を 1 回送り、引数 worker を起こす */
{
	if ( PIGFAGENT_SHOULD_ABORT() ) return rDO|ACT_pigfAgent_ABORT;   /* 送信前に撤収判定(無駄書込回避) */
	sPtr<stdString> op = agent_op_name();
	pipe->write_str(C_OP, op != thNULL ? op : thNEW(stdString,("")));   /* yield 時 ps_write_record で再開 */
	/* ★ インライン引数(非継続=値)を **この pigfAgent 文脈で先に解決**しておく。SEND worker は
	 * ts2Parallel コルーチン(ptsObject ではない)なので、そこで初めて varref を compact すると caller
	 * の env が引けず誤解決になりキャッシュされてしまう。get_hashkey を良い env(pigfAgent)で先に
	 * 呼べば varref が正しく解決・キャッシュされ、SEND の serialize はその実値を使う(配列内 varref
	 * `translate(m,[x, var])` 等。継続(mesh; car=="delayed")はブロックするので除外)。 */
	for ( int i = 0 ; i < args.length() ; ++i ) {
		sPtr<pigData> av = args[i];
		if ( av->car()->get_str()->cmp("delayed") != 0 )
			(void) av->get_hashkey();
	}
	sendIdx = 0;
	/* 引数を fan-out で**パイプライン送信**する。各 worker は:
	 *   phase0: 担当 idx を確定 → 次の兄弟 worker を「待たずに」即 spawn(揃った引数から送れる)
	 *   phase1: 担当引数を解決して送る(継続なら cdr の実値を待つ。未解決なら yield 再走)
	 * ts2Parallel は _fn を worker 毎に複製するので idx/phase は per-worker。sException(C++ 例外)で
	 * 巻き戻っても mutable キャプチャは保持されるため、再入で phase0(spawn)は再実行されない
	 * (= 無駄な二重 spawn を防ぐ。pigData 操作は全て phase1 = spawn 後に置く)。 */
	par = thNEW(ts2Parallel,(ifThis, 0,
		[this, idx=-1, phase=0](sPtr<ts2Parallel> me, sPtr<stdEvent> wev) mutable -> int {
			if ( phase == 0 ) {
				if ( sendIdx >= args.length() )
					return 1;                  /* 送る引数なし */
				idx = sendIdx++;
				if ( sendIdx < args.length() )
					me->spawn();               /* 次の引数 worker を先に起こす */
				phase = 1;
			}
			if ( phase == 1 ) {
				sPtr<pigData> v = args[idx];
				if ( v->car()->get_str()->cmp("delayed") == 0 )
					v = v->cdr()->cdr();   /* 継続の実値(結果 promise。"begin" 段を飛ばす)。is_cache/get_str が compact ゲートで待つ */
				if ( v->is_error() ) {   /* 上流エラー伝播(調査中) */
					err = v;
					phase = 2;
					return 1;
				}
				if ( v->is_cache() )
					pipe->write_arg(C_ARG_PATH, (uint32_t)idx, v->get_str());     /* 入力キャッシュパス */
				else
					pipe->write_arg(C_ARG_INLINE, (uint32_t)idx, v->serialize()); /* 値リテラル(round-trip 形)。agent が value-parse で構造復元 */
				phase = 2;   /* 送信済。root は兄弟完了後 body 再走するが二重送信しない */
			}
			return 1;
		}));
	return ACT_pigfAgent_SEND;   /* 引数送信 par の TSE_RETURN 待ち → rDO なし */
}

TS_STATE(ACT_pigfAgent_SEND)   /* イベント検出のみ(write なし)。par 完了でハッシュ確定 */
{
	if ( PIGFAGENT_SHOULD_ABORT() ) return rDO|ACT_pigfAgent_ABORT;
	if ( ev->type == TSE_RETURN && ev->source == par ) {
		if ( err != thNULL )   /* SEND worker が上流エラーを検出 → 伝播(調査中) */
			return rDO|ACT_pigfAgent_ERROR;
		if ( cachePath == thNULL ) {   /* 遅延引数ケース: 全引数が揃った今ハッシュ確定 */
			hashVal   = compute_arg_hash();
			cachePath = make_cache_path(hashVal);
			/* ★ in-flight dedup(hasDelay 経路): hashVal は **今初めて**確定する(全引数解決後)。
			 * 同一キャッシュを計算中/済みの先行 pigfAgent があれば受け売りして撤収。agent は起動済みだが
			 * C_ARG_END をまだ送っていない=計算は始まっていないので、FIN(wfd close→agent EOF→グレースフル
			 * 終了)で無駄計算なしに撤収できる(無駄なのは fork だけ。ワーカーゲートを保つ代償)。
			 * front を登録(0/1 統一)。次点は: out_cache=1=継続返済みなので dupPromise を firstPromise
			 * (firstFront の継続 cdr)に解決(pigDataPair で包まない)/ out_cache=0=front 未解決なので front 直接。 */
			if ( ptsApp.is_notNull() ) {
				sPtr<pigData> firstFront = ptsApp->inflight_claim(hashVal, front);
				if ( firstFront.is_notNull() ) {       /* 先行あり = 次点 */
					if ( promise.is_notNull() ) {
						/* out_cache 継続: 自分の begin-level(cdr)を **先行の begin-level(cdr)** へ別名解決。
						 * 以後 cdr()->car()(begin)も cdr()->cdr()(結果)も先行を追う。 */
						beginPromise->set_result(firstFront->compact()->cdr());
						beginResolved   = 1;
						promiseResolved = 1;
					} else {
						front->set_result(firstFront);
					}
					ptsApp->cache_hit();
					return rDO|FIN_START;          /* 自分の agent を閉じて(EOF)撤収 */
				}
			}
		}
		return rDO|ACT_pigfAgent_SENDEND;
	}
	if ( ev->type == TSE_RETURN && ev->source == pipe ) {
		err = thNEW(pigDataError,("agent closed during arg send", front->get_info()));
		return rDO|ACT_pigfAgent_ERROR;
	}
	return 0;
}

TS_STATE(ACT_pigfAgent_SENDEND)   /* event 非依存: C_ARG_END(目標キャッシュパス)を 1 回送る = 計算開始 */
{
	if ( PIGFAGENT_SHOULD_ABORT() ) return rDO|ACT_pigfAgent_ABORT;
	pipe->write_str(C_ARG_END, cachePath);   /* yield 可(満杯時)。再走しても ps_write_record で冪等 */
	/* ★ C_ARG_END 送信 = この agent が計算を開始(admitted・必ず完走する)。beginPromise を
	 * ("begin" . promise) に解決して親へ「計算を始めた」を通知する。親は全子の begin を見てから
	 * gate を取りに行く(= admitted 順の admission → 構造的デッドロックフリー)。 */
	if ( beginPromise.is_notNull() && ! beginResolved ) {
		beginPromise->set_result(thNEW(pigDataPair,(thNEW(pigDataString,("begin")), promise)));
		beginResolved = 1;
	}
	return rDO|ACT_pigfAgent_SENDWEND;
}

TS_STATE(ACT_pigfAgent_SENDWEND)  /* event 非依存: W_END 番兵を 1 回送る */
{
	if ( PIGFAGENT_SHOULD_ABORT() ) return rDO|ACT_pigfAgent_ABORT;
	pipe->wend();
	return ACT_pigfAgent_RESULT;   /* A_SAVE_* (TSE_PACKET)待ち → rDO なし */
}

TS_STATE(ACT_pigfAgent_RESULT)
{
	if ( PIGFAGENT_SHOULD_ABORT() ) return rDO|ACT_pigfAgent_ABORT;
	if ( ev->type == TSE_PACKET && ev->source == pipe ) {
		sPtr<ptsWirePacket> pkt = sPtr<ptsWirePacket>::d_cast(ev->msg_obj);
		switch ( pkt->type ) {
		case A_SAVE_BEGIN: {
			/* 保存開始 → 継続を解決(呼び元の遅延参照が起きる)。出力種別で分岐:
			 *  - cache(mesh 等): pigDataCache ハンドル(本文はキャッシュにある。A_SAVE_BEGIN は空)
			 *  - 値(インライン): 相乗り本文を pigDataString に(TODO: パーサ導入後は parse→一般 pigData)。 */
			/* このキャッシュは「使った」→ ptsApp に登録(終了時 cleanup で未使用判定に使う)。 */
			if ( ptsApp.is_notNull() )
				ptsApp->cache_use(hashVal);
			if ( front->get_out_cache() ) {
				promise->set_result(thNEW(pigDataCache,(hashVal, cachePath, front->get_info())));
				/* 結果は呼び元へ渡った。以後の agent エラーは promise では返せない(解決済み)ので
				 * ptsApp に上げる(ACT_pigfAgent_ERROR の振分)。撤収の起こしは ptsApp の登録簿
				 * (set_agentError の wake-all)が担うので、ここで個別 listen はしない。 */
				promiseResolved = 1;
			} else {
				/* 値(インライン): 相乗り本文を言語層の値パーサ(派生供給)で構造化 pigData に。
				 * 子 TSE_RETURN まで promise を解決せず保留(A_SAVE_DONE は vparser 保留中は待たせる)。
				 * 派生が値パーサを持たない(thNULL)なら生テキストを pigDataString で即解決。 */
				int n = pkt->payload.length();
				valText = ( n > 0 )
				    ? sPtr<stdString>(thNEW(stdString,((const char*)&pkt->payload[0], 0, n)))
				    : sPtr<stdString>(thNEW(stdString,("")));
				sPtr<pigData> pv = parse_value_text(valText);   /* 同期パース(plugin) */
				if ( pv != thNULL ) {
					front->set_result(pv);
					promiseResolved = 1;
				} else {
					vparser = make_value_parser(valText);       /* 非同期パース(srava) */
					if ( vparser == thNULL ) {
						front->set_result(thNEW(pigDataString,(valText)));
						promiseResolved = 1;
					}
				}
			}
			return 0;
		}
		case A_SAVE_DONE:
			if ( vparser != thNULL )
				return 0;            /* 値パース完了待ち → A_SAVE_DONE は保留(BYE は parse 後) */
			return ACT_pigfAgent_BYE;   /* A_BYE→pipe 閉じの TSE_RETURN 待ち → rDO なし */
		case A_ERROR: {
			int n = pkt->payload.length();
			sPtr<stdString> msg = ( n > 0 )
			    ? sPtr<stdString>(thNEW(stdString,((const char*)&pkt->payload[0], 0, n)))
			    : sPtr<stdString>(thNEW(stdString,("agent error")));
			err = thNEW(pigDataError,(msg, front->get_info()));
			return rDO|ACT_pigfAgent_ERROR;
		}
		default:
			return 0;
		}
	}
	/* 値パーサ子の完了 → 構造化 pigData で promise を解決し BYE へ(pipe 閉じ待ち)。
	 * 以後 A_SAVE_DONE が来ても BYE は TSE_PACKET を無視するので無害。 */
	if ( ev->type == TSE_RETURN && ev->source == vparser ) {
		sPtr<pigData> v = sPtr<pigData>::d_cast(ev->msg_obj);
		vparser = thNULL;
		if ( v == thNULL )   /* 想定外本文(パース不能)は agent バグ → 明示エラー */
			v = thNEW(pigDataError,("agent returned unparseable value", front->get_info()));
		/* 値返し op は pair を返していない → front を実値へ直接解決(await 中の式が起きる)。 */
		front->set_result(v);
		promiseResolved = 1;   /* 配達済み(以後の agent エラーは ptsApp 経由) */
		return rDO|ACT_pigfAgent_BYE;
	}
	if ( ev->type == TSE_RETURN && ev->source == pipe ) {
		err = thNEW(pigDataError,("agent closed before save done", front->get_info()));
		return rDO|ACT_pigfAgent_ERROR;
	}
	return 0;
}

TS_STATE(ACT_pigfAgent_BYE)
{
	if ( PIGFAGENT_SHOULD_ABORT() ) return rDO|ACT_pigfAgent_ABORT;
	if ( ev->type == TSE_RETURN && ev->source == pipe )
		return rDO|ACT_pigfAgent_FINISH;
	return 0;
}

TS_STATE(ACT_pigfAgent_FINISH)
{
	/* front は ("delayed" . promise)、promise は解決済み。後片付けへ。 */
	return rDO|FIN_START;
}

TS_STATE(ACT_pigfAgent_ERROR)
{
	if ( err == thNULL )
		err = thNEW(pigDataError,("agent aborted", front->get_info()));
	if ( promiseResolved ) {
		/* 継続は既に解決済み(結果は呼び元へ渡り先へ進んだ)→ promise では返せない。
		 * アプリ全体のエラーとして ptsApp に集約(プランナーが countAgent==0 後に拾う)。 */
		if ( ptsApp.is_notNull() )
			ptsApp->set_agentError(err);
	} else if ( promiseLive ) {
		/* front=pair は返したが promise 未解決 → 継続を error で解決(呼び元の遅延参照が error に)。
		 * ★ begin 未通知なら先に解決して親の GATE begin 待ち(cdr()->car())を解く。
		 *   begin=("begin".promise)・結果 promise=err。親は begin を見て進み、SEND で結果=err を拾い伝播。 */
		if ( beginPromise.is_notNull() && ! beginResolved ) {
			beginPromise->set_result(thNEW(pigDataPair,(thNEW(pigDataString,("begin")), promise)));
			beginResolved = 1;
		}
		if ( promise.is_notNull() )
			promise->set_result(err, 1);
	} else {
		/* まだ何も返していない(同期エラー)→ front を error で直接解決。 */
		front->set_result(err, 1);
	}
	return rDO|FIN_START;
}

/* 他 agent のエラー集約 or 自分の destroy で撤収。エラーは既に ptsApp 側にあるので再集約しない
 * (set_agentError を呼ばない = wakeup/invoke_listen の連鎖嵐を避ける)。FIN で agent を kill。 */
TS_STATE(ACT_pigfAgent_ABORT)
{
	return rDO|FIN_START;
}

TS_STATE(FIN_START)
{
	return rDO|FIN_pigfAgent_START;
}

TS_STATE(FIN_pigfAgent_START)
{
	if ( agent.is_notNull() ) {
		/* 待機中(read ブロック)の実 agent を終わらせる唯一の手段はパイプ EOF。ts2System は
		 * `sh -c <cmd>` 経由で起動し、sh が実コマンドを fork するため実 agent は孫プロセス
		 * (retPid は sh を指す)。pid kill は実 agent に届かない(tinyState #3363)。
		 * 書き込み側 wfd を閉じる → agent stdin に EOF → cgatsAgent pipe-closed → FIN → _exit。
		 * これで「特定 agent 1 個だけ」を planner 終了なしに止められる(将来の部分中断にも対応)。 */
		if ( wfd.is_notNull() )
			wfd->destroy();
		agent->destroy();
	}
	/* ★ fd を明示的に閉じる(重要・macOS の fork EAGAIN/EMFILE の真因)。
	 * pigfAgent オブジェクトは ptsApplication::liveAgents(append-only・set_agentError 用)に
	 * 参照され続けるため program 終了まで生存する。ここで閉じないと **rfd(読み fd)と pipe が
	 * 終了まで開きっぱなし**になり fd がリークする。macOS は ulimit -n=256 が既定なので ~256 agent で
	 * 枯渇し pipe()/fork が失敗("failed to launch agent")。Linux は ulimit -n が大きく顕在化しにくいだけ。 */
	if ( pipe.is_notNull() ) { pipe->destroy(); pipe = thNULL; }
	if ( rfd.is_notNull() )  { rfd->destroy();  rfd  = thNULL; }
	if ( wfd.is_notNull() )  { wfd->destroy();  wfd  = thNULL; }
	if ( reader != thNULL )  { reader->destroy(); reader = thNULL; }
	if ( ptsApp.is_notNull() ) {
		if ( gateCredited ) {               /* ワーカーゲートの credit を返却 → 待機 agent を起こす */
			ptsApp->gate_release();
			gateCredited = 0;
		}
		ptsApp->agent_leave(ifThis);   /* 生存数 --。0 でプランナーを起こす */
	}
	return rDO|FIN_pigfFunction_START;
}
