/*
 * pigfAgent — 演算子の実体計算を別プロセス(srava-agent)へ委ねる関数 helper(pigfFunction 派生)。
 * pigDataFunction<pigfAgent> ノードが compact 時に起動する。
 *
 * 真骨頂(pigDataPair / pigDataCache / 継続):
 *   1. 演算子 + 各引数の get_hashkey() を畳んだ「結果ハッシュ」でキャッシュパスを決める。
 *      同一演算・同一引数の結果は常に同じ前提(ハッシュは「何の演算結果か」を表す)。
 *   2. 遅延引数(= 上流 pigfAgent の継続 pair)が無く、キャッシュファイルが既にあれば
 *      **agent を起動せず短絡**(データキャッシュは本文を読んで返す)。
 *   3. キャッシュミス/遅延引数ありなら、まず **3 段継続** ("delayed" . beginPromise) を _front へ即セットして
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
 *   - agent_cmd()      : 起動する外部プロセス(基底 thNULL。pigfModuleAgent が srava-agent)
 *   - agent_op_name()  : 演算子名(C_OP + ハッシュ。基底 thNULL。pigfModuleAgent が srava-op)
 *   キャッシュ dir は pigfFunction env の "CACHE_DIR" を参照(getenv 直読みしない)。
 *
 * 割り切り(TODO):
 *   - op 名は派生固定(_front 由来にするのは別途)。キャッシュ本文の型判定(データ/mesh)は
 *     「A_SAVE_BEGIN payload の有無」で代用。HIT 読みは常にデータキャッシュ(D_TEXT)前提。
 *   - pigDataCache の global dedup list は未実装。
 */
#include	"pig/c++/pigfFunction.h"
#include	"pig/c++/ptsApplication.h"   /* ptsApp->agent_enter/leave/set_agentError(全 agent 集約) */
#include	"pig/c++/pigCacheManager.h"  /* 起動時キャッシュ sweep(pig 層・cgptsPlanner から移設) */
#include	"pig/c++/pigData.h"
#include	"pig/c++/pigwire.h"
#include	"pig/c++/ptsMediator.h"           /* 通信抽象 (#3406, docs/mediator_design.md) */
#include	"pig/c++/pigExecBackend.h"        /* 起動方式→Mediator 生成 (具体クラス名を隠す・Phase1-3) */
#include	"pig/c++/pigModuleRegistry.h"     /* K4: カーネル別キャッシュキーソルト */
#include	"pig/c++/pigModule.h"             /* rev4 B-2: descriptor.codec_tags (継続の型リスト構築) */
#include	"pig/c++/ptsMediatorPacket.h"     /* Internal 経路の pigData 直渡しパケット (4.3) */
#include	"ts2/c++/ts2Parallel.h"
#include	"ts2/c++/stdEvent.h"
#include	"_ts2/c++/pigfAgent_.h"

#include	"pig/c++/osglue.h"   /* osglue_mkdir_p(mkdir -p 相当) */

#include	<stdlib.h>   /* getenv */
#include	<stdio.h>    /* snprintf */
#include	<stdint.h>
#include	<unistd.h>   /* access */
#include	<string.h>   /* strchr/strlen (rev4 B-2: codec_tags の 4CC 走査) */
#include	<sys/stat.h>   /* agent バイナリの存在確認 */
#include	<errno.h>      /* 起動失敗の理由 */

#include	<string>     /* rev4 B-2: 継続スタンプの型名リスト構築 */

CLASS_TINYSTATE(pig/c++/pigfAgent,pig/c++/pigfFunction)

/* 基底: 演算子固有の短絡なし(派生 = 言語層が override)。0=非該当。 */
int pigfAgent_::try_shortcircuit() { return 0; }
int pigfAgent_::try_decompose()    { return 0; }

/* 基底: 値パーサ無し(言語パーサは srava 固有)。thNULL = 生テキストを pigDataString として返す。 */


/* 基底: agent コマンド未設定(派生が override)。pigfAgent 自体は特定 agent に非依存。 */
sPtr<stdString>
pigfAgent_::agent_cmd()
{
	return thNULL;
}

/* 基底: in-process 実行なし (thNULL = 常に External)。言語層 (pigfModuleAgent) が override して
 * 「この op を planner 内 thread で実行するなら pigAgentRegistry のキー」を返す (#3406 4.3)。 */
sPtr<stdString>
pigfAgent_::agent_module_name()
{
	return thNULL;
}


/* 基底: カーネル非依存(単一 agent)。MODULE_NONE = 継続にスタンプせず・ハッシュにも混ぜない
 * (= 従来の振る舞いと完全に同一)。カーネルを持つ言語層(pigfModuleAgent)が override する。 */
int
pigfAgent_::decide_out_module()
{
	return MODULE_NONE;
}

/* ★ 2026-08-19: この agent が産む **型スタンプ** を出力キャッシュのハンドルへ載せる。
 *   値は decide_out_module (型ディスパッチ) が sig から決めた出力型そのもので、
 *   **継続 pair の car に載せる文字列と同一**。同じ文字列を載せることで、下流から見た型の
 *   見え方が「継続で受け取ったか / キャッシュで受け取ったか」に依存しなくなる
 *   = cold と warm で routing が変わらない。
 *   ★ 幾何型でない出力も型を持つ ("value" = 値キャッシュ / "ref" = D_REF レコード。どちらも
 *     組込モジュール "pig" が申告する型) ので、「型の無い出力」は存在しない。
 *   ★ outCache を作る箇所すべてから呼ぶ (ACT_START / 遅延引数あり経路 / A_SAVE_BEGIN の保険)。
 *     継続が解決した後は下流も生のハンドルを見るため、1 箇所だけでは漏れる。 */
void
pigfAgent_::stamp_out_cache()
{
	if ( outCache == thNULL || outTypeList == thNULL )
		return;
	outCache->set_type_stamp(thNEW(stdString,(outTypeList->get_str())));
}


/* ★ P2e (⑤ 型軸化): 旧 arg_module (入力の「所属 module」を継続スタンプ/HIT キャッシュから引く) は撤去。
 *   routing は入力の **型** (arg_type_set) を読み、型から executor を決める (decide_executor / 型不一致時は
 *   module_of_type で「型を産む home module」へ配送・pigfModuleAgent)。「値に module が属す」概念は消えた。 */

/* 演算子名は _front(ノード)由来(C_OP + 結果ハッシュ + cgatsAgent の dispatch キー)。
 * 未設定なら thNULL(空扱い)。agent 種別ではなくノード演算ごとに決まるので派生で固定しない。 */
sPtr<stdString>
pigfAgent_::agent_op_name()
{
	return _front->get_op_name();
}

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	pigfAgent_(
		sPtr<ptsObject> parent,
		sPtr<pigDataOperator> _front);

	sRptr<ptsObject,tinyState>		parent;
	/* ★ #3419 (2026-08-23) ゲート入場順序: ワーカーゲートの待ち行列を並べ替えるキー。
	 * **値が小さいほど先に入場**する (tinyState の規約。既定 TS_DEFAULT_PRIORITY=10000)。
	 * ここでは「生成通し番号の負値」を返す = **後から生まれた agent ほど先に入場** (LIFO 近似)。
	 * DAG を幅優先に広げず深さ優先寄りに掘るので、同時に生きる中間結果が減る、というのが狙い。
	 * 有効になるのは gateSem->enablePriority=1 のとき (SRAVA_GATE_ORDER=lifo) だけで、
	 * 既定 (先着順) では参照されない = この override を置いても既定の挙動は変わらない。
	 * ⚠⚠ この宣言は **public セクションに置くこと**。protected だと tscpp2 が interface 側に
	 *   override を生成せず、glue が impl->tinyState_::priority() を **修飾付き(非仮想)** で呼ぶため、
	 *   エラーも警告も出ないまま既定値 10000 が返り続ける (tinyState example/semaphore-priority-test)。 */
	virtual int	priority(sPtr<tinyState> caller=thNULL);
protected:
	/* 起動する agent コマンド。pigfAgent は piggybackTurtle 汎用で特定 agent を知らない
	 * → 基底は thNULL(未設定=エラー)。派生クラス(例 pigfModuleAgent)が override して供給する。
	 * 将来 video 編集 agent / 巨大テクスチャ agent 等を別派生で混在可能にするためのフック。 */
	virtual sPtr<stdString>	agent_cmd();
	/* ★ in-process 実行の opt-in (#3406 4.3): この op を planner 内 thread で実行するなら
	 * pigAgentRegistry のキー ("manifold" 等) を返す。thNULL = External (agent process)。
	 * 基底は thNULL。pigfModuleAgent が override (MODULE_MANIFOLD + env SRAVA_INPROC)。 */
	virtual sPtr<stdString>	agent_module_name();
	/* 演算子名(C_OP で送る + 結果ハッシュに混ぜる)。これも agent 依存なので派生が供給。
	 * 基底は thNULL(空扱い)。 */
	virtual sPtr<stdString>	agent_op_name();
	/* ★ pig / 言語層(srava)の境界フック。pigfAgent は piggybackTurtle 汎用で特定演算子も特定言語の
	 * 値構文も知らない。言語固有の知識は派生(pigfModuleAgent)が override して供給する:
	 *   try_shortcircuit: 演算子固有の代数的短絡(srava の単位元 {} 等で CGAL を呼ばず畳む)。
	 *     戻り 0=非該当(agent 起動へ) / 1=_front に結果セット済み(FIN) / 2=err セット済み(ERROR)。基底=0。
	 * ★ 値テキスト → pigData の復元フック (旧 make_value_parser / parse_value_text) は廃止した
	 *   (§3.1/§8.1)。agent の結果は ptsMediatorExternal が、キャッシュ本文は pigDataCache が
	 *   それぞれ pigValueParser レジストリ経由で戻すので、pigfAgent は値構文を一切知らない。 */
	virtual int		try_shortcircuit();
	/* ★ #3436 P4: n 項ノードの評価時分解 (docs/sig_grammar_design.md §5)。戻り値の約束は
	 *   try_shortcircuit と同じ (0=非該当 / 1=_front に結果セット済み / 2=err セット済み)。
	 *   基底は 0 (分解しない)。pigfModuleAgent が override。 */
	virtual int		try_decompose();
	/* ★ med の TSE_RETURN を握りつぶして mediator_return_flag に畳む (§8.3)。 */
	virtual sPtr<stdEvent>	filter(sPtr<stdEvent> ev);
	/* ★ カーネル選択フック(#3404): この agent 起動で使う幾何カーネル(MODULE_CGAL/MANIFOLD)を
	 *   入力引数の型と DEFAULT_OUTPUT から決める。基底=MODULE_NONE(カーネル非依存 = 従来の単一 agent)。
	 *   pigfModuleAgent が override(入力伝播 + DEFAULT_OUTPUT)。ACT_START で 1 度呼ばれ outModule に memo。 */
	virtual int		decide_out_module();
	/* ★ 2026-08-19: 出力キャッシュハンドルへ型スタンプを載せる (継続 pair と同じ文字列)。
	 *   outCache を作る箇所すべてから呼ぶ。 */
	void			stamp_out_cache();
	/* ★ P2e: 旧 arg_module (入力の所属 module を引く補助) は撤去。routing は入力の型 (arg_type_set) を読む。 */
	/* planner↔agent 通信は Mediator に集約 (#3406)。旧 agent/pipe/rfd/wfd の 4 部材は
	 * ptsMediatorExternal が内包する。pigfAgent は種別 (process/thread) を知らない。 */
	sPtr<ptsMediator>	med;
	sPtr<ts2Parallel>	par;
	sPtr<pigDataPromise>	promise;       /* 結果(cdr->cdr)。A_SAVE_BEGIN で hashCache に解決 */
	sPtr<pigDataPromise>	beginPromise;  /* 計算開始(cdr)。SENDEND(C_ARG_END)で ("begin".promise) に解決。
	                                        * 親はこれで「子が計算を始めた=admitted」を知り、全子 begin で gate を取る */
	int			beginResolved;  /* beginPromise->set_result 済みか(撤収時の二重解決防止) */
	sPtr<pigData>		err;     /* is_error() な値そのもの(pigDataError とは限らない)。d_cast 不要 */
	sPtr<stdString>		cachePath;
	/* 出力キャッシュのハンドル (#3406 4.3)。SENDEND で作って pl_write_end で agent へ渡し
	 * (Internal は同一オブジェクトへ set_body される = 共有)、A_SAVE_BEGIN の promise 解決に使う。
	 * External でも同じ流れ (オブジェクトは agent と共有されないだけで挙動等価)。 */
	sPtr<pigDataCache>	outCache;
	pHashKeyType		hashVal;
	int			sendIdx;
	int			promiseLive;     /* _front=("delayed".promise) を返済み(継続あり) */
	int			promiseResolved; /* promise->set_result 済み(= 結果が呼び元へ渡った) */
	uint32_t		loadPid;         /* ★ #3419: agent プロセスの pid (in-proc は 0) */
	int			inflightClaimed; /* ★ #3419: 自分が in-flight 台帳へ登録した (FIN で外す) */
	int			gateCredited;    /* ワーカーゲートの credit を取得済み(FIN で gate_release。取得/解放を 1:1 に保つ) */
	int			gateSeq;         /* ★ #3419: agent の生成通し番号(INI で ptsApp から 1 度だけ貰う)。priority() の元 */
	/* ★ #3419 §16.13 (ひさ指摘 2026-08-24): **ゲートを取る位置**。
	 * 0 = first: 「1 引数が完了 + 残りが begin」で入場 (従来)。上流の計算とストリーム読みを重ねられる
	 *            → **外部プロセス実行 (CGAL 等) ではこれが正しい**: ディスクから読む時間を隠せる。
	 * 1 = all  : **全引数が解決してから**入場。⚠ in-proc は重ねる相手が無い (データはメモリ上) ので、
	 *            従来だと **枠を握ったまま残りの上流を待つ agent** が生まれ、cap が小さいと実効枠が減る。
	 * 実体は「最初の 1 つで par を cancel するかどうか」だけ (下の lambda 参照)。 */
	int			gateWhenAll;
	/* ★ #3419 §16.14: **枠を握ったまま入力を待っていた時間** の計測 (ひさ依頼 2026-08-24)。
	 * gate_get() の時刻を控え、ACT_SEND で par が返った時点 (= **全引数が解決**) との差を取る。
	 * ⚠ この差には agent の起動 (fork / thread 生成) と C_OP 送信も含まれる。in-proc では小さいが、
	 *   process 実行では fork のぶん過大に出る。「純粋な待ち」ではなく「握ってから使えるまで」。
	 * gateHadDelay=0 (遅延引数なしの葉) は待ちようがないので集計から外す。 */
	long long		gateT0;
	int			gateHadDelay;
	int			forkFails;       /* fork EAGAIN の連続失敗回数(上限超でようやくエラー) */
	int			outModule;       /* ★ この agent が起動するカーネル(#3404)。ACT_START で decide_out_module() が設定 */
	/* ★ rev4 Phase B-2b: この agent の **出力型リスト** (CSV)。型ディスパッチ (decide_executor) が絞った
	 *   単一出力型を継続スタンプに載せる。thNULL = 未設定 (未注釈 op) → 継続は outModule の全型へフォールバック。 */
	sPtr<stdString>		outTypeList;
	/* ★ med からの TSE_RETURN は状態関数ではなく filter で受けてこのフラグに畳む (§8.3)。
	 * par (ts2Parallel) は TSE_RETURN まで無言なのに対し、med とは多数のやりとりがあり、
	 * 「med が終わった」は全状態に効く横断的事実なので、イベントではなくフラグが自然。 */
	int			mediator_return_flag;
	sPtr<stdObject>		mediator_error;   /* その TSE_RETURN の msg_obj (現状 External は使わない) */
	int			i;
private:
	pHashKeyType    compute_arg_hash();
	sPtr<stdString> make_cache_path(pHashKeyType h);
};

TS_END_IMPLEMENT

TS_BEGIN_INTERFACE
#include	"ts2/c++/sRptr.h"
#include	"pig/c++/pigData.h"       /* pHashKeyType をメンバ型に */
#include	"pig/c++/ptsMediator.h"   /* sPtr<ptsMediator> med の完全型(派生 .cpp が dtor を実体化) */
class ptsObject;
class pigDataOperator;
class ts2Parallel;
class ptsWireCacheStreamReaderText;
class pigDataPromise;
class pigDataError;
class stdString;
class stdEvent;
TS_END_INTERFACE

#endif


pigfAgent_::pigfAgent_(TS_ARGS0)
        : pigfFunction_(parent,_front),
	  parent(tinyState_::parent)
{
    hashVal         = 0;
    mediator_return_flag = 0;
    sendIdx         = 0;
    promiseLive     = 0;
    promiseResolved = 0;
    beginResolved   = 0;
    gateCredited    = 0;
    gateSeq         = 0;
    gateWhenAll     = 0;
    gateT0          = 0;
    gateHadDelay    = 0;
    loadPid         = 0;
    forkFails       = 0;
    outModule       = MODULE_NONE;
    i               = 0;
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

/* ★ #3419 (2026-08-23): ワーカーゲート(stdLimitSemaphore)の待ち行列を並べる優先度。
 * 小さいほど先。生成通し番号の**負値**を返すので、後発 agent ほど先に入場する = LIFO 近似。
 * gateSeq==0 (app 無しの単体テスト) は 0 を返し、全員同値 → 到着順のまま(挙動不変)。
 * ⚠ この値は **ゲートが enablePriority=1 のときだけ**使われる。他に priority() を見る生きた経路は
 *   tinyState には無い (tsGC の priority キューは exe() に呼び出し元が無く未使用)。 */
int
pigfAgent_::priority(sPtr<tinyState> caller)
{
	return -gateSeq;
}

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
	/* ★ #3404 / K4: カーネルをキャッシュキーに弁別として混ぜる。同一 op でも異カーネルは byte 内容が
	 *   異なる(取り違えると誤り)ため。混ぜ値 (salt) は **モジュールレジストリが持つ** — 基準カーネル
	 *   (cgal) は salt 無しで従来キーを byte 不変に保ち、非基準 (manifold="\x01MFM") だけ混ぜる。
	 *   これで pigfAgent から MODULE_MANIFOLD の名指しが消える (.so 化 Phase1・byte 完全互換)。 */
	{
		const char *salt = ( ptsApp != thNULL && ptsApp->module_registry != thNULL )
		    ? ptsApp->module_registry->hash_salt(outModule) : 0;
		if ( salt != 0 )
			for (const char *p = salt; *p; ++p) { h ^= (unsigned char)*p; h *= prime; }
	}
	/* ★ 可換 op (union/intersection) は各引数の get_hashkey() を畳む前に昇順ソートし、
	 *   a op b と b op a を同一キャッシュキーにする(旧 pigDataOperator::normalize() の parse 時
	 *   ソートが担っていたが、#3452 でモジュール登録が eval 時の module() 呼び出しへ移ったため
	 *   parse 直後は 1 本もロードされておらず op_commutative() が常に false を返す回帰になった。
	 *   ここは eval 時 = 先行する module() 呼び出しが既に実行済みなので正しく判定できる)。
	 *   HIT 判定はハッシュの一致だけを見る(2.は既に compute 済みの結果を指すファイルの有無)ので、
	 *   実際にどちらの引数順で計算されたかは問わない — ソートは compute_arg_hash 側だけで足りる。 */
	int commutative = ( op != thNULL && ptsApp != thNULL && ptsApp->module_registry != thNULL )
	    ? ptsApp->module_registry->op_commutative(opn) : 0;
	sArray<uint64_t> argHashes;
	argHashes.length(args.length());
	for (int k = 0; k < args.length(); ++k) {
		sPtr<pigData> v = args[k];
		if (pig_is_delayed(v))   /* 遅延継続 → 実値(cdr->cdr: "begin" 段を飛ばす) */
			v = v->cdr()->cdr();
		argHashes[k] = (uint64_t)v->get_hashkey();
	}
	if ( commutative )
		for (int i = 1; i < argHashes.length(); ++i)
			for (int j = i; j > 0 && argHashes[j-1] > argHashes[j]; --j) {
				uint64_t t = argHashes[j-1]; argHashes[j-1] = argHashes[j]; argHashes[j] = t;
			}
	for (int k = 0; k < argHashes.length(); ++k) {
		uint64_t a = argHashes[k];
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


/* ★ イベント前処理 (§8.3): med からの TSE_RETURN をフラグに畳んで握りつぶす。
 * par (ts2Parallel) は TSE_RETURN が来るまで pigfAgent と何もやりとりが無いのでいままで通り
 * 状態関数で受けるのに対し、med は TSE_RETURN まで多数のやりとりが発生する = 「med が
 * 終わった」はどの状態に居ても意味を持つ横断的事実なので、filter で受けるのがエレガント
 * (ひさ設計 2026-08-02 §8.3)。
 * NB: ひさのサンプルは `return TS_BASECLASS::filter(thNULL)` だが、基底 filter(thNULL) は
 *   throw sException(0) で、dispatch ループ (tinyState.cpp) の filter 呼び出しは try の外。
 *   握りつぶしの正規の作法は **thNULL を返す** (ret==thNULL で dispatch が break) なので
 *   そちらに従う。wakeup() で置き換えイベントを積み、状態関数にフラグを見せる。 */
sPtr<stdEvent>
pigfAgent_::filter(sPtr<stdEvent> ev)
{
	if ( ev == thNULL )
		return ev;
	/* ★ med.is_notNull() の条件は付けない (ひさ指示 2026-08-11)。FIN が med を手放してから
	 * TSE_RETURN が来ると照合できず、FIN_pigfAgent_MEDWAIT が永久に待つため。med は
	 * **TSE_RETURN を受けるまで手放さない** (手放すのは MEDWAIT の 1 箇所だけ)。 */
	if ( ev->type == TSE_RETURN && ev->source == med ) {
		mediator_return_flag = 1;
		mediator_error = ev->msg_obj;
		if ( osglue_env_int("PIG_DBG_SIG", 0) ) {
			sPtr<stdString> _dop = agent_op_name();
			sPtr<stdString> _dc = agent_cmd();
			::fprintf(stderr, "[sigdbg] MEDRET op=%s st=%d appErr=%d destroyed=%d cmd=%s\n",
				( _dop != thNULL ) ? _dop->get_str() : "(none)",
				( med.is_notNull() ) ? med->child_status() : -1,
				(int)( ptsApp.is_notNull() && ptsApp->get_agentError() != thNULL ),
				(int)is_destroyed(),
				( _dc != thNULL ) ? _dc->get_str() : "(none)");
		}
		wakeup();       /* 置き換えイベントで通知 (握りつぶすと状態関数が走らないため) */
		return thNULL;  /* TSE_RETURN は握りつぶし */
	}
	return TS_BASECLASS::filter(ev);
}


/*******************************************
	STATE MACHINE
********************************************/

/* 撤収すべきか: 他 agent がエラー集約した / 自分が destroy された / med が終わった (§8.3)。
 * イベント待ち状態の先頭で呼び、真なら ABORT (原因の振り分けは ABORT が行う)。 */
#define PIGFAGENT_SHOULD_ABORT() \
	( ( ptsApp.is_notNull() && ptsApp->get_agentError() != thNULL ) || is_destroyed() \
	  || mediator_return_flag )

TS_STATE(INI_pigfFunction_START)
{
	if ( ptsApp.is_notNull() ) {
		ptsApp->agent_enter(ifThis);   /* 生存数 ++ + 登録(FIN で leave)。全 agent 完了判定 + 撤収用 */
		gateSeq = ptsApp->agent_next_seq();   /* ★ #3419: 生成順を確定(priority() の元。以後不変) */
	}
	return rDO|ACT_START;
}


TS_STATE(ACT_START)
{
	/* 既に他 agent がエラー集約済みなら、ここで起動準備に入らず即撤収(無駄な promise/fork を避ける)。
	 * abort 経路では呼び元も同時に撤収するので _front 未解決でハングしない(set_agentError の wake-all)。 */
	if ( PIGFAGENT_SHOULD_ABORT() ) return rDO|ACT_pigfAgent_ABORT;

	/* 1) 引数エラー検査(is_error() は遅延ノードでも compact ゲートで解決。継続は delayed pair を即返す
	 *    ので非ブロッキング)。is_error() が真なら args[i] 自体がエラー値 → そのまま使う(d_cast 不要)。 */
	for ( i = 0 ; i < args.length() ; i++ )
		if ( args[i]->is_error() ) {
			err = args[i];
			return rDO|ACT_pigfAgent_ERROR;
		}

	/* 1.5) ★ 演算子固有の代数的短絡(言語層フック)。srava なら単位元 {} で CGAL を呼ばず畳む等を
	 *   pigfModuleAgent::try_shortcircuit が行う。pigfAgent 自体は特定演算子を知らない(基底=0)。 */
	{
		int sc = try_shortcircuit();
		if ( sc == 1 ) return rDO|FIN_START;              /* _front に結果セット済み */
		if ( sc == 2 ) return rDO|ACT_pigfAgent_ERROR;    /* err セット済み */
	}

	/* 1.6) ★ #3436 P4: n 項ノードの分解 (docs/sig_grammar_design.md §5.5)。パーサは n 項のまま
	 *   残すので、ここで「n 項のまま受けられるか / 何項ずつの木にするか」を決める。
	 *   ⚠ 1) の is_error() で引数は compact ゲートを通っており (継続は delayed pair)、型スタンプが
	 *   読める = 分解に必要な型が非ブロッキングに取れる。1.7 の routing より前に置く。 */
	{
		int dc = try_decompose();
		if ( dc == 1 ) return rDO|FIN_START;              /* _front を木の根に解決済み */
		if ( dc == 2 ) return rDO|ACT_pigfAgent_ERROR;
	}

	/* 1.7) ★ カーネル選択(#3404): 入力引数の型 + DEFAULT_OUTPUT からこの agent のカーネルを決める
	 *   (pigfModuleAgent が override。基底は MODULE_NONE)。ここで 1 度だけ決めて outModule に memo し、
	 *   継続 pair へのスタンプ(下流への型前送り)・ハッシュ弁別(3/SEND)・agent_cmd(LAUNCH)で使う。
	 *   引数のカーネルは継続 pair のスタンプ / HIT キャッシュファイル先頭から非ブロッキングに読める。 */
	outModule = decide_out_module();
	/* ★ rev4 Phase C 最終: decide_out_module が routing 不能を検知して err をセットしたら明示エラーへ
	 *   (旧 cgal 万能フォールバックの撤去)。err は :360/:369 で return 済みなのでここでは decide_out_module
	 *   由来のみ。カーネル routing しない agent (基底=MODULE_NONE・err 未設定) は素通し。 */
	if ( err.is_notNull() )
		return rDO|ACT_pigfAgent_ERROR;

	/* 2) 遅延継続検出。継続は ("delayed" . promise) なので car=="delayed" で判別(d_cast 不要)。
	 *    あれば今はハッシュ不能 → キャッシュ判定を飛ばす。 */
	int hasDelay = 0;
	for ( i = 0 ; i < args.length() ; i++ )
		if ( pig_is_delayed(args[i]) ) { hasDelay = 1; break; }

	/* 3) 遅延無し → 結果ハッシュ確定 → キャッシュファイルがあれば agent 起動せず短絡 */
	gateHadDelay = hasDelay;   /* ★ #3419 §16.14: 待ちうる agent かどうか (葉は除外) */
	if ( !hasDelay ) {
		hashVal   = compute_arg_hash();
		cachePath = make_cache_path(hashVal);
		/* HIT 判定はハンドルを先に作って is_valid で (2026-07-29 メモ 2.: ::access 直叩きを
		 * pigDataCache へ吸収)。このハンドルは HIT 時の結果 (CACHEREAD)・MISS 時の出力
		 * (SENDEND → agent が set_body) にそのまま使う。 */
		outCache = thNEW(pigDataCache,(hashVal, cachePath, _front->get_info()));
		stamp_out_cache();   /* ★ 型スタンプを載せる (HIT ならこのハンドルが下流へ渡る) */
		if ( outCache->is_valid() ) {
			if ( ptsApp.is_notNull() ) ptsApp->cache_hit();   /* HIT 計上 */
			return rDO|ACT_pigfAgent_CACHEREAD;   /* HIT */
		}
		/* ★ in-flight dedup(最も綺麗な位置・ユーザ案): hashVal 確定 & ファイル未 HIT。継続(/ * 4))を
		 * **まだ作っていない=_front 未解決**なので、先行 pigfAgent があれば `_front->set_result(その _front)`
		 * の **統一形**(out_cache 0/1 を区別しない)で受け売りして撤収できる。呼び元は dupFront→firstFront
		 * を不動点で辿る。自分が最初なら _front を登録してこのまま計算へ。 */
		if ( ptsApp.is_notNull() ) {
			sPtr<pigData> firstFront = ptsApp->inflight_claim(hashVal, _front);
			if ( firstFront == thNULL ) inflightClaimed = 1;   /* ★ 自分が最初 */
			if ( firstFront.is_notNull() ) {       /* 先行あり = 次点 */
				_front->set_result(firstFront);     /* dupFront → firstFront(継続/値どちらでも不動点で解決) */
				ptsApp->cache_hit();
				return rDO|FIN_START;              /* 起動せず撤収(無駄 fork/計算なし) */
			}
		}
	}

	
	/* 4) ミス or 遅延引数あり → 継続を即返して呼び元を解放。
	 *    ただし継続 ("delayed".promise) pair は **mesh 出力(out_cache)専用**。これは下流 pigfAgent が
	 *    arg を compact せず car() で「未解決」を覗き見て起動を遅延させる mesh-DAG パイプラインの仕掛け
	 *    で、cdr を辿るのは pigfAgent だけ。値返し op(out_cache==0)は式の演算子等(pigfAgent 以外)が
	 *    消費するので pair を返さず、_front を未解決のまま残して **完了時に _front を実値へ直接解決**する
	 *    (消費側は通常の pigDataDelay compact ゲートで await→実値を得る。HIT 経路と同じ扱い)。 */
	if ( _front->get_out_cache() ) {
		promise      = thNEW(pigDataPromise,(ifThis));   /* 結果。A_SAVE_BEGIN で hashCache へ */
		beginPromise = thNEW(pigDataPromise,(ifThis));   /* 計算開始。SENDEND で ("begin".promise) へ */
		promiseLive  = 1;
		/* ★ 継続を **3段化**: ("delayed" . beginPromise)。SENDEND(C_ARG_END=計算開始)で beginPromise を
		 * ("begin" . promise) に解決し、A_SAVE_BEGIN で promise を hashCache に解決する。
		 * 親は cdr()->car()=="begin" で「子が計算を始めた(admitted)」を、cdr()->cdr() で結果を取る。 */
		/* ★ 継続の car = **型スタンプ** (出力キャッシュに載せるものと同じ文字列)。下流 pigfAgent は
		 *   これを非ブロッキングに読み、上流の完了を待たずに入力の型を知る。
		 *   ★ 2026-08-19: 出どころは **sig が決めた出力型だけ**。旧 2 段目 (outModule の全型 CSV) と
		 *   3 段目 (型を持たないモジュールはモジュール名) のフォールバックは撤去した。値も参照も
		 *   組込の型 ("value" / "ref") として sig に書かれているので、型を持たない出力は存在しない。
		 *   sig で決まらない呼び出しは decide_out_module が **明示エラー**にするので、ここへ
		 *   型なしで来ることは無い (来たら planner のバグなので黙って進まない)。 */
		if ( outTypeList == thNULL ) {
			err = thNEW(pigDataError,(
			    "internal: the planner produced no output type stamp for this node "
			    "(every op's sig must declare its output type)", _front->get_info(), 1));
			return rDO|ACT_pigfAgent_ERROR;
		}
		sPtr<pigDataPair> cont = thNEW(pigDataPair,
		    (thNEW(pigDataString,(outTypeList->get_str())), beginPromise));
		_front->set_result(cont);
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

	/* ★ #3419 §16.13: ゲートを取る位置を決める。既定 (env 無指定) は **auto**:
	 *   in-proc (agent_module_name() != thNULL) なら all / 外部プロセスなら first。
	 *   ⚠ outModule は上の decide_out_module() で確定済みなのでここで判定できる。
	 *   env SRAVA_GATE_WHEN=first|all|auto で上書き (対照の口・消さないこと)。 */
	{
		/* ★ #3419 §17.3: srava 変数 GATE_WHEN → 環境変数 → 既定 "auto" (設定表)。 */
		sPtr<stdString> gws = ptsApp.is_notNull() ? ptsApp->cfg_str("GATE_WHEN") : sPtr<stdString>(thNULL);
		const char *gw = gws.is_notNull() ? gws->get_str() : 0;
		if ( gw != 0 && ::strcmp(gw, "all") == 0 )        gateWhenAll = 1;
		else if ( gw != 0 && ::strcmp(gw, "first") == 0 ) gateWhenAll = 0;
		else                                              gateWhenAll = ( agent_module_name() != thNULL );
	}

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
			if ( pig_is_delayed(v) ) {
				(void) v->cdr()->cdr()->get_hashkey();   /* 結果(cdr->cdr)。未解決なら compact ゲートで yield 再走 */
				/* ★ #3419 §16.13: cancel すると「最初の 1 つが完了」で par が終わる = 従来のゲート位置。
				 * cancel しなければ par は全 worker 完了 = **全引数が解決**してから TSE_RETURN する。 */
				if ( ! gateWhenAll )
					me->cancel();                        /* 最初に結果が出た子で全体終了 → 起動 */
				return 1;
			}
			if ( v->is_cache() ) {   /* 既に解決済みの子(キャッシュ HIT)も「終わった子」→ 即 cancel して起動 */
				if ( ! gateWhenAll )
					me->cancel();
				return 1;
			}
			return 1;   /* それ以外(非継続・非キャッシュ値)は待機対象でない */
		}));
		return ACT_pigfAgent_FIRSTWAIT;   /* par の TSE_RETURN(=最初の引数解決 or 全完了)待ち */
}

TS_STATE(ACT_pigfAgent_FIRSTWAIT)
{
	/* 最初の遅延引数が解決(cancel)→ ゲートへ。hashVal は全引数が揃う SEND(または GATE で全 ready 時)で確定。 */
	if ( ev->type == TSE_RETURN && ev->source == par )
		return rDO|ACT_pigfAgent_GATE;
	/* ★ 撤収要求が来ても par には**何も送らず** TSE_RETURN を待ち続ける (§8.3 + ひさ設計
	 * 2026-08-06)。ts2Parallel は外から destroy/cancel を撃つ設計ではない。worker が
	 * compact ゲートで待つ上流の promise は、上流 pigfAgent の ABORT/ERROR が**エラーで
	 * 解決してから死ぬ**約束 (ABORT の未返済解決) なので、worker は必ず起きて par は
	 * TSE_RETURN を返す。abort への遷移は次状態 (GATE) の頭の SHOULD_ABORT が行う。 */
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
		if ( pig_is_delayed(args[i]) )
			(void) args[i]->cdr()->car();   /* 子の begin を待つ(未 begin なら yield 再走) */

	/* ★ メモリ watermark による入場制限は当面無効(fork 同様デッドロック源)。IF は ptsApplication に残置。 */

	/* 入場(セマフォ取得)。満杯なら gate_get() が yield → release で起こされて GATE 再走。全子 admitted の
	 * 後なので、待っても必ず誰か(より深い admitted ノード)が完走して release する = 前進が保証される。 */
	ptsApp->gate_get();
	gateCredited = 1;
	gateT0 = osglue_now_ms();   /* ★ #3419 §16.14: 枠を握った時刻 */
	/* ★ #3419 (2026-08-23): 入場**順序**のトレース (SRAVA_GATE_TRACE=1)。順序を変える実験なので、
	 * 「本当に順序が変わったか」を目で確かめられる口を残す (検証できない機構は動いていないのと同じ)。
	 * seq=生成通し番号 / order=fifo|lifo。lifo なら seq が降順寄りに並ぶはず。 */
	if ( ptsApp.is_notNull() && ptsApp->cfg_int("GATE_TRACE") != 0 ) {   /* ★ #3419 §17.3 */
		sPtr<stdString> on = agent_op_name();
		::fprintf(stderr, "[gate] enter seq=%d op=%s live=%d order=%s\n",
		          gateSeq, ( on != thNULL ) ? on->get_str() : "?",
		          ptsApp->gate_live(), ptsApp->gate_order_lifo() ? "lifo" : "fifo");
	}
	/* ★ #3419 §12.8: 入場したので稼働数を更新し、L_AGENT をゲートへ反映する。 */
	{
		ptsApp->load_agent_enter();
		ptsApp->load_apply_agent_limit();
	}
	return rDO|ACT_pigfAgent_LAUNCH;
}

TS_STATE(ACT_pigfAgent_LAUNCH)
{
	/* 起動直前に再チェック: 既に撤収中なら **agent プロセスを fork しない**(死に確定のプロセスを
	 * 生んで直後に殺すレースを防ぐ)。FIRSTWAIT 経由でも !hasDelay の直行でもここを通る。 */
	if ( PIGFAGENT_SHOULD_ABORT() ) return rDO|ACT_pigfAgent_ABORT;
	sPtr<stdString> cmd = agent_cmd();   /* 派生クラスが供給(基底は thNULL) */
	if ( cmd == thNULL ) {
		err = thNEW(pigDataError,("pigfAgent: no agent command (subclass must override agent_cmd)", _front->get_info()));
		return rDO|ACT_pigfAgent_ERROR;
	}
	/* テスト用: 同時 fork 数の上限を人為的に N に設定し(PIG_TEST_FORKLIMIT=N)、それを超える fork を
	 * EAGAIN のように失敗させる。実 fork 上限(ulimit)を共有環境で再現するのは不安定なため。
	 * 実際の EAGAIN と同じく「並列度が上限を超えたときだけ失敗」する → 適応バックオフが N に収束する。 */
	/* ★ #3427 ④: 旧・関数内 static (プロセス唯一の可変 static) を廃し、都度 getenv で読む。
	 * ここは fork 直前の 1 回だけ通る低頻度パスなので getenv キャッシュは不要。 */
	int testForkLimit = -1;
	{
		const char *t = ::getenv("PIG_TEST_FORKLIMIT");
		if ( t != 0 ) testForkLimit = ::atoi(t);
	}
	int forcedFail = 0;
	if ( testForkLimit >= 0 && ptsApp.is_notNull() && ptsApp->gate_live() > testForkLimit )
		forcedFail = 1;

	/* ★ in-process 実行 (#3406 4.3): 言語層が kernel 名を返し (MODULE_MANIFOLD + opt-in)、
	 * その実行体がこのバイナリに登録されていれば ptsMediatorInternal (fork なし・文字列化なし)。
	 * enable 失敗 (実行体未リンク等) は External へフォールバック (挙動は従来どおり)。 */
	if ( ! forcedFail ) {
		sPtr<stdString> kname = agent_module_name();
		if ( kname != thNULL ) {
			med = ( ptsApp != thNULL && ptsApp->module_registry != thNULL )
			    ? ptsApp->module_registry->backends.make("thread", ifThis, kname)   /* 具体クラス名を隠す (Phase1-3) */
			    : sPtr<ptsMediator>(thNULL);
			if ( med.is_notNull() && med->enable() == 0 ) {
				if ( ptsApp.is_notNull() ) ptsApp->cache_miss();   /* MISS 計上 (fork 経路と同じ) */
				return ACT_pigfAgent_HELLO;   /* enable が積んだ TSE_ASSERT 待ち → rDO なし */
			}
			/* ★ med = thNULL にしない (2026-08-11): med の非 null が「TSE_RETURN 未返済」の印。
			 * 直後に External の med で上書きするので、この thread med の TSE_RETURN は
			 * source 不一致で基底 filter に落ちる (従来の thNULL 時と同じ扱い)。 */
			med->destroy();   /* 未登録 → External へ */
		}
	}
	/* 起動と通信確立は ptsMediatorExternal に委譲 (#3406, docs/mediator_design.md):
	 * ts2System 起動 → wfd set_divisible(64KB 超引数対策・詳細は External 側コメント) →
	 * ptsWirePipe 生成、を enable() が行う。fork 失敗は同期で非0。 */
	int launchFail = 0;
	if ( ! forcedFail ) {
		med = ( ptsApp != thNULL && ptsApp->module_registry != thNULL )
		    ? ptsApp->module_registry->backends.make("process", ifThis, cmd)   /* 具体クラス名を隠す (Phase1-3) */
		    : sPtr<ptsMediator>(thNULL);
		/* ★ #3441: module(so,{opts}) で積まれたハッシュがあれば、agent 起動直後に C_ENV で
		 * 1 回だけ渡す (稼働中の agent への再配線はしない)。outModule は ACT_START の
		 * decide_out_module() で既に確定済み。opts が無ければ thNULL のまま (enable の既定と同じ)。 */
		sPtr<pigData> modOpts = ( ptsApp != thNULL && ptsApp->module_registry != thNULL )
		    ? ptsApp->module_registry->opts_for(outModule) : sPtr<pigData>(thNULL);
		launchFail = ( med == thNULL || med->enable(modOpts) != 0 );
		/* ★ med = thNULL にしない (2026-08-11): 起動に失敗した med も destroy → TSE_RETURN を
		 * 返して畳まれるので、FIN_pigfAgent_MEDWAIT がそれを待って回収する。 */
		if ( launchFail && med.is_notNull() ) med->destroy();   /* 失敗オブジェクト破棄 */
	}
	if ( forcedFail || launchFail ) {
		/* fork 失敗(典型は EAGAIN=同時プロセス上限超過)。**limit は固定方針**なので、適応バックオフ
		 * (limit の動的縮小)も再試行もしない。fork が cap に届かない=ゲートの上限が実機の fork
		 * 上限を超えている、なので **即エラーで終了し、ユーザに手で下げて再実行してもらう**。
		 * (理由: ① 既に fork 済みの agent を殺せない以上、limit を後から縮めてもデッドロックは解けない。
		 *        ② 物理上限が後から増えるわけでもないので AIMD 回復も不合理。
		 *  メモリ容量等を含めた動的制御は将来ちゃんと整理する。) */
		if ( gateCredited && ptsApp.is_notNull() ) {
			/* ★ #3419 T4: 退場するので稼働数を戻す (**出る方も契機**・§5.3)。 */
			ptsApp->load_agent_leave();

			ptsApp->load_apply_agent_limit();
			ptsApp->gate_release(); gateCredited = 0;
		}
		/* ★ 起動失敗は「上限超過」だけではない。agent バイナリが無い/実行できないときも同じ経路に
		 * 来るので、その場合は上限の話をせずに **実際のコマンドを出す**(誤った原因表示で探し回る
		 * のを防ぐ。2026-08-15: Windows の install 済み実行でまさにこれを踏んだ)。 */
		char msg[512];
		sPtr<stdString> ac = agent_cmd();
		const char *acs = ( ac.is_notNull() ) ? ac->get_str() : "";
		const char *p = acs;
		while ( *p == '#' || *p == ' ' ) ++p;            /* 直接 exec 印と空白を飛ばす */
		char bin[256]; size_t bi = 0;
		while ( p[bi] && p[bi] != ' ' && bi + 1 < sizeof bin ) { bin[bi] = p[bi]; ++bi; }
		bin[bi] = '\0';
		struct stat abst;
		if ( bin[0] != '\0' && ::stat(bin, &abst) != 0 )
			::snprintf(msg, sizeof msg,
				"cannot start the agent: %s (%s). SRAVA_AGENT で正しいパスを指すか、"
				"cmake --install で配置してください。",
				bin, ::strerror(errno));
		else
			::snprintf(msg, sizeof msg,
				"fork failed (process limit): the worker gate is open to %d agents, which exceeds "
				"this machine's fork/process limit. Lower it with SRAVA_LOAD_CPU=<percent> "
				"(or SRAVA_LOAD_CPU=0 SRAVA_LOAD_AGENT=<count>) and re-run.",
				ptsApp.is_notNull() ? ptsApp->gate_cap_dyn() : 0);
		err = thNEW(pigDataError,(msg, _front->get_info(), 1));   /* fatal: 回復不能=即終了 */
		return rDO|ACT_pigfAgent_ERROR;
	}
	if ( ptsApp.is_notNull() ) ptsApp->cache_miss();   /* fork 成功 → MISS 計上(再試行で二重計上しない) */
	return ACT_pigfAgent_HELLO;   /* handshake(Mediator 経由 TSE_ASSERT)待ち → rDO なし */
}

TS_STATE(ACT_pigfAgent_CACHEREAD)
{
	if ( PIGFAGENT_SHOULD_ABORT() ) return rDO|ACT_pigfAgent_ABORT;
	/* HIT したキャッシュも「使った」→ 登録(再入で呼ばれるが cache_use は重複無視)。 */
	if ( ptsApp.is_notNull() )
		ptsApp->cache_use(hashVal);
	/* キャッシュヒット(agent 起動なし)。出力種別で分岐(_front->get_out_cache):
	 *  - cache(mesh 等): 中身を読まず pigDataCache ハンドルを返す(下流が必要時に reader で読む)
	 *  - 値(インライン): **本文を get_body() で取る** (2026-08-02 メモ §8.1)。読み出し helper の
	 *    起動も D_TEXT 本文の値パース(言語パーサ or 同期パーサ)も pigDataCache/ptsDataCache が
	 *    面倒を見るので、pigfAgent が reader と値パーサ子を自前で持つ必要はない
	 *    (これで parse_value_text / make_value_parser の virtual ごと消えた)。
	 * ⚠ get_body() は未ロードなら listen+sException で yield し、この状態関数は**先頭から再走**
	 *   する → get_body 地点まで冪等であること (cache_use は重複無視なので ok)。 */
	if ( _front->get_out_cache() ) {
		_front->set_result(outCache);   /* HIT ハンドル = ACT_START で is_valid 判定に使ったもの */
		return rDO|FIN_START;
	}
	sPtr<pigData> v = outCache->get_body();
	/* thNULL = 読込/decode 失敗 (is_valid 偽)。想定外本文は agent 側のバグなので
	 * 生テキストへフォールバックせず明示エラー。 */
	if ( v == thNULL ) {
		/* ★ #3433: 断定しない (cache race 以外に「型が変換できない」もある)。cache に自己記述させる。 */
		sPtr<stdString> what = outCache->describe();
		char eb[224];
		::snprintf(eb, sizeof eb,
		    "cached value (%s) を読み込み/decode できない (cache 破損/競合 か、その型では読めない値)",
		    what->get_str());
		v = thNEW(pigDataError,(eb, _front->get_info()));
	}
	_front->set_result(v);
	return rDO|FIN_START;
}

/* NB: write 系は **event でガードした分岐の中で呼ばない**。write_c が yield(sException)すると
 * 状態関数は先頭から再走するが、その時 ev は I/O 準備イベントに変わっており event 分岐が
 * 成立せず write が再開できない(かつ 1 状態で複数 write すると先頭 write が二重実行される)。
 * → イベント検出状態は「検出して rDO で遷移」だけ。実 write は **event 非依存・1 状態 1 write_record**
 *   の状態で行い、yield 再走は write_record 自身の pico(ps_write_record)が安全に再開する。
 *   (ptsWirePipe が read_c を ev 非依存で呼ぶのと同じ作法) */

/* ★ agent が「planner と版が違う」と判断して終了したときの説明文 (2026-08-15 bench 報告)。
 * planner と agent が別ビルドだと、症状が「両版が持つ素の式が `volume: needs a mesh` で落ちる」
 * 「引数が増えた op で agent が落ちて planner が待ち続ける」など**原因の見当がつかない形**で出る。
 * agent は起動時に版を突き合わせて、違えば exit 3 で即終了する (srava_agent_main.cpp)。ここで
 * それを名指しに変換する。該当しなければ 0。 */
static int pigf_version_mismatch_msg(sPtr<ptsMediator> med, sPtr<stdString> cmd, char *out, size_t outsz)
{
	int st = ( med.is_notNull() ) ? med->child_status() : -1;
	if ( st < 0 || (st & 0x7f) != 0 ) return 0;        /* 未終了 / シグナル死 */
	if ( ((st >> 8) & 0xff) != 3 )     return 0;        /* 版不一致の合図 (exit 3) ではない */
	::snprintf(out, outsz,
		"planner and agent are from different builds; use a matching pair "
		"(when running from a build tree, point SRAVA_AGENT at that tree's srava_agent). "
		"launch command: %s",
		( cmd.is_notNull() ) ? cmd->get_str() : "(unknown)");
	return 1;
}

TS_STATE(ACT_pigfAgent_HELLO)
{
	/* ★ med の終了 (filter が flag に畳んだ TSE_RETURN・§8.3) = handshake 前に閉じた。
	 * 起動失敗 (非同期で来る: #3406, 2026-08-01 enable() を薄くしたため) かどうかは
	 * Mediator に聞く。汎用の SHOULD_ABORT より先に見て具体的なエラーにする。 */
	if ( mediator_return_flag ) {
		if ( med.is_notNull() && med->launch_failed() ) {
			if ( gateCredited && ptsApp.is_notNull() ) {
			/* ★ #3419 T4: 退場するので稼働数を戻す (**出る方も契機**・§5.3)。 */
			ptsApp->load_agent_leave();
			
			ptsApp->load_apply_agent_limit();
			ptsApp->gate_release(); gateCredited = 0;
		}
			char msg[256];
			::snprintf(msg, sizeof msg,
				"fork failed (process limit): the worker gate is open to %d agents, which exceeds "
				"this machine's fork/process limit. Lower it with SRAVA_LOAD_CPU=<percent> "
				"(or SRAVA_LOAD_CPU=0 SRAVA_LOAD_AGENT=<count>) and re-run.",
				ptsApp.is_notNull() ? ptsApp->gate_cap_dyn() : 0);
			err = thNEW(pigDataError,(msg, _front->get_info(), 1));   /* fatal: 回復不能=即終了 */
			return rDO|ACT_pigfAgent_ERROR;
		}
		char vmsg[400];
		if ( pigf_version_mismatch_msg(med, agent_cmd(), vmsg, sizeof vmsg) ) {
			err = thNEW(pigDataError,(vmsg, _front->get_info(), 1));   /* fatal: 混在は続行不能 */
			return rDO|ACT_pigfAgent_ERROR;
		}
		/* ★ mediator が組み立てた理由 (子の status + agent の stderr + wire の状態を
		 * 総合したもの) があればそれを使う。無ければ従来の汎用文言 (2026-08-26)。 */
		if ( mediator_error.is_notNull() && sPtr<pigData>::d_cast(mediator_error) != thNULL &&
		     sPtr<pigData>::d_cast(mediator_error)->is_error() ) {
			err = thNEW(pigDataError,(sPtr<pigData>::d_cast(mediator_error)->error_message(),
				_front->get_info()));
			return rDO|ACT_pigfAgent_ERROR;
		}
		err = thNEW(pigDataError,("agent closed before handshake", _front->get_info()));
		return rDO|ACT_pigfAgent_ERROR;
	}
	if ( PIGFAGENT_SHOULD_ABORT() ) return rDO|ACT_pigfAgent_ABORT;
	/* handshake 完了 → 送信へ (write はしない)。TSE_ASSERT は必ず来る (来ないケースは
	 * 上の flag = med 終了で拾う) ので、この状態の出口はこの 2 つで全て (§8.3)。 */
	if ( ev->type == TSE_ASSERT && ev->source == med )
		return rDO|ACT_pigfAgent_SENDOP;
	return 0;
}

TS_STATE(ACT_pigfAgent_SENDOP)   /* event 非依存: C_OP を 1 回送り、引数 worker を起こす */
{
	if ( PIGFAGENT_SHOULD_ABORT() ) return rDO|ACT_pigfAgent_ABORT;   /* 送信前に撤収判定(無駄書込回避) */
	sPtr<stdString> op = agent_op_name();
	med->pl_write_op(op != thNULL ? op : thNEW(stdString,("")));   /* yield 時 ps_write_record で再開 */
	/* ★ インライン引数(非継続=値)を **この pigfAgent 文脈で先に解決**しておく。SEND worker は
	 * ts2Parallel コルーチン(ptsObject ではない)なので、そこで初めて varref を compact すると caller
	 * の env が引けず誤解決になりキャッシュされてしまう。get_hashkey を良い env(pigfAgent)で先に
	 * 呼べば varref が正しく解決・キャッシュされ、SEND の serialize はその実値を使う(配列内 varref
	 * `translate(m,[x, var])` 等。継続(mesh; car=="delayed")はブロックするので除外)。 */
	for ( int i = 0 ; i < args.length() ; ++i ) {
		sPtr<pigData> av = args[i];
		if ( ! pig_is_delayed(av) )
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
				if ( pig_is_delayed(v) )
					v = v->cdr()->cdr();   /* 継続の実値(結果 promise。"begin" 段を飛ばす)。is_cache/get_str が compact ゲートで待つ */
				if ( v->is_error() ) {   /* 上流エラー伝播(調査中) */
					err = v;
					phase = 2;
					return 1;
				}
				/* PATH/INLINE の弁別 (is_cache) は Mediator の責務 (Internal では弁別ごと消える)。 */
				med->pl_write_arg(idx, v);
				phase = 2;   /* 送信済。root は兄弟完了後 body 再走するが二重送信しない */
			}
			return 1;
		}));
	return ACT_pigfAgent_SEND;   /* 引数送信 par の TSE_RETURN 待ち → rDO なし */
}

TS_STATE(ACT_pigfAgent_SEND)   /* イベント検出のみ(write なし)。par 完了でハッシュ確定 */
{
	if ( ev->type == TSE_RETURN && ev->source == par ) {
		/* ★ par が返った時点で撤収要求済みなら即 ABORT (§8.3)。この下のハッシュ確定は
		 * 遅延引数の compact ゲートを踏む = 解決されない promise で永久 yield し得るので、
		 * abort 経路では踏まない。 */
		if ( PIGFAGENT_SHOULD_ABORT() ) return rDO|ACT_pigfAgent_ABORT;
		/* ★ #3419 §16.14: ここで par が返った = **全引数が解決**。枠を握ってからここまでが
		 * 「握ったまま入力を待っていた時間」。遅延引数を持っていた agent だけ集計する。 */
		if ( gateT0 != 0 && gateHadDelay && ptsApp.is_notNull() ) {
			ptsApp->gate_note_idle(osglue_now_ms() - gateT0);
			gateT0 = 0;
		}
		if ( err != thNULL )   /* SEND worker が上流エラーを検出 → 伝播(調査中) */
			return rDO|ACT_pigfAgent_ERROR;
		if ( cachePath == thNULL ) {   /* 遅延引数ケース: 全引数が揃った今ハッシュ確定 */
			hashVal   = compute_arg_hash();
			cachePath = make_cache_path(hashVal);
			/* ★ in-flight dedup(hasDelay 経路): hashVal は **今初めて**確定する(全引数解決後)。
			 * 同一キャッシュを計算中/済みの先行 pigfAgent があれば受け売りして撤収。agent は起動済みだが
			 * C_ARG_END をまだ送っていない=計算は始まっていないので、FIN(wfd close→agent EOF→グレースフル
			 * 終了)で無駄計算なしに撤収できる(無駄なのは fork だけ。ワーカーゲートを保つ代償)。
			 * _front を登録(0/1 統一)。次点は: out_cache=1=継続返済みなので dupPromise を firstPromise
			 * (firstFront の継続 cdr)に解決(pigDataPair で包まない)/ out_cache=0=_front 未解決なので _front 直接。 */
			if ( ptsApp.is_notNull() ) {
				sPtr<pigData> firstFront = ptsApp->inflight_claim(hashVal, _front);
				if ( firstFront == thNULL ) inflightClaimed = 1;   /* ★ 自分が最初 */
				if ( firstFront.is_notNull() ) {       /* 先行あり = 次点 */
					if ( promise.is_notNull() ) {
						/* out_cache 継続: 自分の begin-level(cdr)を **先行の begin-level(cdr)** へ別名解決。
						 * 以後 cdr()->car()(begin)も cdr()->cdr()(結果)も先行を追う。 */
						beginPromise->set_result(firstFront->compact()->cdr());
						beginResolved   = 1;
						promiseResolved = 1;
					} else {
						_front->set_result(firstFront);
					}
					ptsApp->cache_hit();
					return rDO|FIN_START;          /* 自分の agent を閉じて(EOF)撤収 */
				}
			}
		}
		return rDO|ACT_pigfAgent_SENDEND;
	}
	/* ★ 撤収要求 (med の突然死も flag 経由でここに畳まれる・§8.3) が来ても par には何も
	 * 送らず TSE_RETURN を待ち続ける (FIRSTWAIT のコメント参照): worker が待つ上流の
	 * promise は上流の ABORT/ERROR がエラーで解決するので、worker は is_error() で拾って
	 * 完了する。par が返ったら上の分岐の SHOULD_ABORT → ABORT が原因を振り分ける
	 * (旧 "agent closed during arg send")。 */
	return 0;
}

TS_STATE(ACT_pigfAgent_SENDEND)   /* event 非依存: 計算開始 + 送信終端を 1 回送る */
{
	if ( PIGFAGENT_SHOULD_ABORT() ) return rDO|ACT_pigfAgent_ABORT;
	/* 出力キャッシュのハンドルをここで作る (#3406 4.3)。Internal は agent がこの同一オブジェクトへ
	 * set_body する (= 計算結果の in-memory body を planner がそのまま下流へ渡せる)。
	 * yield 再走に備えて冪等 (再走で作り直さない)。 */
	if ( outCache == thNULL ) {
		outCache = thNEW(pigDataCache,(hashVal, cachePath, _front->get_info()));
		stamp_out_cache();   /* ★ 遅延引数ありの経路 (ACT_START ではまだ作っていない) */
	}
	/* ★ 番兵 (旧 SENDWEND の pl_wend) はここに畳まれた (2026-08-02 メモ §2.1)。「送信の終わり」は
	 * ワイヤの都合であって pigfAgent の関心ではないので、Mediator の中で面倒を見る。
	 * 目標パスも outCache 自身が持っているので渡さない。yield 可(満杯時)・再走に対して冪等。 */
	med->pl_write_end(outCache);
	/* ★ C_ARG_END 送信 = この agent が計算を開始(admitted・必ず完走する)。beginPromise を
	 * ("begin" . promise) に解決して親へ「計算を始めた」を通知する。親は全子の begin を見てから
	 * gate を取りに行く(= admitted 順の admission → 構造的デッドロックフリー)。 */
	if ( beginPromise.is_notNull() && ! beginResolved ) {
		beginPromise->set_result(thNEW(pigDataPair,(thNEW(pigDataString,("begin")), promise)));
		beginResolved = 1;
	}
	return ACT_pigfAgent_RESULT;   /* A_SAVE_* (TSE_PACKET)待ち → rDO なし */
}

TS_STATE(ACT_pigfAgent_RESULT)
{
	if ( PIGFAGENT_SHOULD_ABORT() ) return rDO|ACT_pigfAgent_ABORT;
	if ( ev->type == TSE_PACKET && ev->source == med ) {
		/* ★ 着信は External / Internal のどちらでも ptsMediatorPacket (2026-08-02 メモ §3.1)。
		 * 値返し op の結果は構造化 pigData がそのまま乗っている → ここでのパースは不要
		 * (External はワイヤのテキストを ptsMediatorExternal が既に pigData へ戻している)。
		 * mesh は data=出力キャッシュ (本文はキャッシュ側にある)。 */
		sPtr<ptsMediatorPacket> mpkt = sPtr<ptsMediatorPacket>::d_cast(ev->msg_obj);
		if ( mpkt.is_notNull() ) {
			switch ( mpkt->type ) {
			case A_SAVE_BEGIN:
				if ( ptsApp.is_notNull() )
					ptsApp->cache_use(hashVal);
				if ( _front->get_out_cache() ) {
					/* ★ A_SAVE_BEGIN = 生産者の「メタ書込済」宣言を planner 側ハンドルへ反映
					 * (mark_valid) **してから** promise を解決する。External 生産者では
					 * ACT_START の HIT 判定が MISS 時に焼き込んだ CV_INVALID がこのハンドルに
					 * 残っており、癒さずに下流へ渡すと消費者の get_body が panic する
					 * (leaf-gating・2026-08-12。Internal は ptsDataCache SAVE が validState を
					 * 直接立てるので冪等な上書きになるだけ)。 */
					if ( outCache != thNULL )
						outCache->mark_valid();
					if ( outCache == thNULL ) {   /* 通常来ないが、来たらスタンプ込みで作る */
						outCache = thNEW(pigDataCache,(hashVal, cachePath, _front->get_info()));
						stamp_out_cache();
					}
					promise->set_result(sPtr<pigData>(outCache));
					promiseResolved = 1;
				} else {
					_front->set_result(mpkt->data != thNULL ? mpkt->data
					    : sPtr<pigData>(thNEW(pigDataString,(thNEW(stdString,(""))))));
					promiseResolved = 1;
				}
				/* ★ #3419 (ひさ 2026-08-22): **promise を解決した直後**に in-flight 台帳から外す。
				 * 登録した agent 自身の責務。⚠ この時点でキャッシュはまだ書き込み中だが、
				 * 書きかけを読む形は **process agent では常態** (単一 writer/複数 reader の
				 * ストリーミング読み) なので、ここで外して壊れるなら process 実行が成立しない。
				 * ⚠ 値返し op (out_cache==0) や error/abort 経路はここを通らないので、
				 * FIN 側の解除は取りこぼし用に残す (フラグで冪等)。 */
				if ( inflightClaimed && ptsApp.is_notNull() ) {
					ptsApp->inflight_release(hashVal); inflightClaimed = 0;
				}
				return 0;
			case A_SAVE_DONE:
				return ACT_pigfAgent_BYE;   /* 値パースは Mediator 内で完了済み (§3.1) */
			case A_ERROR:
				/* エラーは **pigData (pigDataError) のまま**届く (2026-07-30 メモ L651。External は
				 * ptsMediatorExternal が payload テキストから作る・§3.1)。生メッセージと
				 * fatal を多態で取り、**ソース位置は呼び元 (_front) の pigInfo を付け直す** —
				 * 位置情報は planner 側の文脈で、agent は知らない (Mediator は位置を付けずに
				 * 作る。これが無いと "ERROR[file,line]" が落ちる)。
				 * fatal ビットは Internal では文字列化を経ないので保たれる (External はワイヤが
				 * テキストしか運ばないので落ちる — ワイヤ仕様の限界)。 */
				err = ( mpkt->data != thNULL && mpkt->data->is_error() )
				    ? sPtr<pigData>(thNEW(pigDataError,(mpkt->data->error_message(),
				        _front->get_info(), mpkt->data->is_fatal())))
				    : sPtr<pigData>(thNEW(pigDataError,(mpkt->str != thNULL ? mpkt->str
				        : sPtr<stdString>(thNEW(stdString,("agent error"))), _front->get_info())));
				return rDO|ACT_pigfAgent_ERROR;
			default:
				return 0;
			}
		}
		return 0;   /* ptsMediatorPacket 以外は届かない (§3.1) */
	}
	/* med の突然死 (旧 "agent closed before save done") は flag 経由で頭の SHOULD_ABORT が
	 * 拾い、ABORT が原因を振り分ける (§8.3)。 */
	return 0;
}

TS_STATE(ACT_pigfAgent_BYE)
{
	/* ★ 正常系でも med からの TSE_RETURN 到達 (= flag) を確認してから終わる (ひさ回答 6・
	 * 2026-08-05: planner プロセスは pigfAgent 全完了を待って終了する必要があり、将来の
	 * 巻き戻し型 (Ctrl+C → agent へ通知 → 書きかけキャッシュを削除) の余地のためにも
	 * この待ちが要る)。SHOULD_ABORT より先に見る (flag は正常終了の合図でもあるため)。 */
	if ( mediator_return_flag )
		return rDO|ACT_pigfAgent_FINISH;
	if ( PIGFAGENT_SHOULD_ABORT() ) return rDO|ACT_pigfAgent_ABORT;
	return 0;
}

TS_STATE(ACT_pigfAgent_FINISH)
{
	/* _front は ("delayed" . promise)、promise は解決済み。後片付けへ。 */
	return rDO|FIN_START;
}

TS_STATE(ACT_pigfAgent_ERROR)
{
	if ( err == thNULL )
		err = thNEW(pigDataError,("agent aborted", _front->get_info()));
	/* ★ どの経路で返すにせよ **理由は必ず記録**しておく (2026-08-26・ひさ提案)。
	 * 下の 3 分岐のうち promise 連鎖へ返す 2 つは ptsApp に何も残さないので、
	 * 「落ちた本人の理由」が傍観者の汎用エラーに負けて消えていた。record は起こさない
	 * (撤収トリガは従来どおり set_agentError だけ)。planner が末尾で列挙する。 */
	if ( ptsApp.is_notNull() )
		ptsApp->record_agentError(err);
	if ( promiseResolved ) {
		/* 継続は既に解決済み(結果は呼び元へ渡り先へ進んだ)→ promise では返せない。
		 * アプリ全体のエラーとして ptsApp に集約(プランナーが countAgent==0 後に拾う)。 */
		if ( ptsApp.is_notNull() )
			ptsApp->set_agentError(err);
	} else if ( promiseLive ) {
		/* _front=pair は返したが promise 未解決 → 継続を error で解決(呼び元の遅延参照が error に)。
		 * ★ begin 未通知なら先に解決して親の GATE begin 待ち(cdr()->car())を解く。
		 *   begin=("begin".promise)・結果 promise=err。親は begin を見て進み、SEND で結果=err を拾い伝播。 */
		if ( beginPromise.is_notNull() && ! beginResolved ) {
			beginPromise->set_result(thNEW(pigDataPair,(thNEW(pigDataString,("begin")), promise)));
			beginResolved = 1;
		}
		if ( promise.is_notNull() )
			promise->set_result(err, 1);
	} else {
		/* まだ何も返していない(同期エラー)→ _front を error で直接解決。 */
		_front->set_result(err, 1);
	}
	return rDO|FIN_START;
}

/* 他 agent のエラー集約 or 自分の destroy で撤収。エラーは既に ptsApp 側にあるので再集約しない
 * (set_agentError を呼ばない = wakeup/invoke_listen の連鎖嵐を避ける)。FIN で agent を kill。
 * ★ §8.3: med の突然死 (mediator_return_flag) **だけ** が理由でここへ来た場合は、誰も
 * エラーを集約していないので、ここでエラーに変換して ERROR へ振る (_front 未解決のまま
 * FIN すると planner が永久待ちになる)。旧 per-state メッセージ ("agent closed during
 * arg send" / "before save done") はこの 1 本に集約 (どの状態で死んだかより「agent が
 * 途中で閉じた」が本質のため)。正常系の flag は BYE が先に拾うのでここへは来ない。 */
TS_STATE(ACT_pigfAgent_ABORT)
{
	if ( osglue_env_int("PIG_DBG_SIG", 0) ) {
		sPtr<stdString> _dop = agent_op_name();
		sPtr<stdString> _dc2 = agent_cmd();
		::fprintf(stderr, "[sigdbg] ABORT cmd=%s op=%s medflag=%d err=%d destroyed=%d appErr=%d st=%d\n",
			( _dc2 != thNULL ) ? _dc2->get_str() : "(none)", ( _dop != thNULL ) ? _dop->get_str() : "(none)",
			mediator_return_flag, (int)(err != thNULL), (int)is_destroyed(),
			(int)( ptsApp.is_notNull() && ptsApp->get_agentError() != thNULL ),
			( med.is_notNull() ) ? med->child_status() : -1);
	}
	if ( mediator_return_flag && err == thNULL && ! is_destroyed() &&
	     ! ( ptsApp.is_notNull() && ptsApp->get_agentError() != thNULL ) ) {
		/* ★ mediator_error = ptsMediatorExternal が **子の終了 status・agent の stderr・
		 * wire の状態を総合して**組み立てた理由 (2026-08-26)。ここは位置情報を付けて
		 * 再包装するだけ。理由が立たなかったときだけ従来の汎用文言になる。 */
		err = ( mediator_error.is_notNull() && sPtr<pigData>::d_cast(mediator_error) != thNULL &&
		        sPtr<pigData>::d_cast(mediator_error)->is_error() )
		    ? sPtr<pigData>(thNEW(pigDataError,(sPtr<pigData>::d_cast(mediator_error)->error_message(),
		        _front->get_info())))
		    : sPtr<pigData>(thNEW(pigDataError,("agent closed unexpectedly", _front->get_info())));
		{	/* ★ 版不一致 (agent が exit 3) なら、名指しの説明に差し替える。 */
			char vmsg[900];   /* ★ 異常終了の説明は agent の stderr を載せるので長い */
			if ( pigf_version_mismatch_msg(med, agent_cmd(), vmsg, sizeof vmsg) )
				err = thNEW(pigDataError,(vmsg, _front->get_info(), 1));
		}
		return rDO|ACT_pigfAgent_ERROR;   /* ERROR が _front/promise を解決する */
	}
	/* ★ 未返済の _front/promise を**エラーで解決してから**死ぬ (ひさ指摘 2026-08-06)。
	 * 黙って FIN すると、この promise を compact ゲートで待つ下流の par worker が永久に
	 * 起きない (§8.3 で FIRSTWAIT/SEND が par の TSE_RETURN を必ず待つ形になって顕在化
	 * した既存バグ — 従来は「呼び元 = planner は sig_abort で自力撤収する」場合しか
	 * 考えられていなかった)。最上流の ABORT がここで解決すると、下流の worker が順に
	 * 起きて par → pigfAgent → さらに下流、とエラーが伝播して全体が畳まれる。
	 * エラー値は集約済みのもの (interrupted by SIGINT 等) を使い回す = 原因が読める。
	 * set_agentError は呼ばない (再集約しない: wake-all の連鎖嵐を避ける・従来どおり)。 */
	if ( ! promiseResolved ) {
		sPtr<pigData> ae = ( ptsApp.is_notNull() && ptsApp->get_agentError() != thNULL )
		    ? ptsApp->get_agentError()
		    : sPtr<pigData>(thNEW(pigDataError,("aborted", _front->get_info())));
		if ( promiseLive ) {
			/* begin 未通知なら先に解決して親の GATE begin 待ちを解く (ERROR と同じ作法) */
			if ( beginPromise.is_notNull() && ! beginResolved ) {
				beginPromise->set_result(thNEW(pigDataPair,(thNEW(pigDataString,("begin")), promise)));
				beginResolved = 1;
			}
			if ( promise.is_notNull() )
				promise->set_result(ae, 1);
			promiseResolved = 1;
		} else
			_front->set_result(ae, 1);
	}
	return rDO|FIN_START;
}

TS_STATE(FIN_START)
{
	return rDO|FIN_pigfAgent_START;
}

TS_STATE(FIN_pigfAgent_START)
{
	/* 通信の破棄は Mediator に委譲 (#3406)。★ 要求は destroy() だけ (§2.1/§3.2: shutdown() は
	 * 廃止)。External は is_destroyed() を見て wfd を閉じ (=パイプ EOF で実 agent を
	 * 終わらせる・tinyState #3363)、pipe の TSE_RETURN を待ってから破棄列 (teardown:
	 * ts2System destroy → pipe/rfd/wfd destroy = fd リーク対策・macOS EMFILE の真因) を回す。
	 * **即座に殺さない**のは agent が書きかけのキャッシュを書き切れるようにするため。
	 * 正常系では med は pipe が閉じた時点で既に自分で畳んでいる (§3.1) ので、ここは空振りする。
	 * ★ destroy を送るのはここ 1 回だけ。回収 (TSE_RETURN) は次状態 MEDWAIT が待つ。 */
	if ( med.is_notNull() )
		med->destroy();
	return rDO|FIN_pigfAgent_MEDWAIT;
}

/* ★ med の回収待ち (ひさ指示 2026-08-11)。これが無いと **子プロセスを回収し終える前に
 * agent_leave() してしまい**、planner の WAITAGENTS (countAgent==0) が先に満たされて
 * planner だけ先に exit し、計算中の agent が取り残される (SIGTERM 後、planner が先に消えても
 * 重い agent は計算を続けてしまう)。ptsMediatorExternal.cpp:383-391 の
 * 「destroy を送る側は TSE_RETURN が戻るのを待つ」作法 (2026-08-06) に pigfAgent も揃える。
 * med を起動していない (thNULL) 経路は待つものが無いのでそのまま通過する。 */
TS_STATE(FIN_pigfAgent_MEDWAIT)
{
	if ( osglue_env_int("PIG_DBG_TD", 0) ) ::fprintf(stderr, "[td] agent MEDWAIT med=%d flag=%d\n", (int)med.is_notNull(), mediator_return_flag);
	if ( med.is_notNull() && mediator_return_flag == 0 )
		return 0;   /* med の TSE_RETURN 待ち (filter がフラグを立てて wakeup を積む) */
	med = thNULL;   /* ★ med を手放すのはここ 1 箇所だけ (filter の source 照合を壊さないため) */
	if ( ptsApp.is_notNull() ) {
		if ( gateCredited ) {               /* ワーカーゲートの credit を返却 → 待機 agent を起こす */
			/* ★ #3419 T4: 退場も再配分の契機 (§5.3)。 */
			ptsApp->load_agent_leave();
			
			ptsApp->load_apply_agent_limit();
			ptsApp->gate_release();
			gateCredited = 0;
		}
		ptsApp->agent_leave(ifThis);   /* 生存数 --。0 でプランナーを起こす */
	}
	/* ★ §9: 終了時点で手放す。pigfAgent は liveAgents (dedup 台帳) に参照され続け program
	 * 終了まで生存するため、ここで切らないと promise 連鎖や outCache (mesh 本体を抱え得る)
	 * が planner 終了まで解放されない。promise/beginPromise は解決済み (ERROR/ABORT が保証)
	 * なので、値は受け手側の sPtr が保持している — ここは自分の参照を切るだけ。 */
	par            = thNULL;
	promise        = thNULL;
	beginPromise   = thNULL;
	err            = thNULL;
	cachePath      = thNULL;
	outCache       = thNULL;
	mediator_error = thNULL;
	/* ★ #3419: in-flight 台帳から外す (登録したのが自分のときだけ)。 */
	if ( inflightClaimed && ptsApp.is_notNull() ) { ptsApp->inflight_release(hashVal); inflightClaimed = 0; }
	return rDO|FIN_pigfFunction_START;
}
