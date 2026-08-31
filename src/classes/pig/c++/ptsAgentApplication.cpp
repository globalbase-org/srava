/*
 * ptsAgentApplication — agent process の実態元祖 + 通信 Mediator (#3406, 2026-07-30 メモ L410)。
 *
 * ptsApplication から **agent process だけの機能**を分離したクラス (ひさ指示 2026-07-30)。
 * ptsApplication は planner (cgptsPlanner) の親でもあるため、agent 専用の部材 (自 stdin/stdout の
 * ts2IO・ptsWirePipe・実行体・ワイヤ↔pigData 変換) をそちらに置き続けるのは筋が悪い。
 *
 *   INI  : enable() で s2IOstd → ptsWirePipe → 実行体 (pigAgentRegistry から) を起こす。
 *   ACT  : pipe の着信を **pigData に復号して** 実行体へ転送 / 実行体の FIN で自分も畳む
 *          (= プロセス終了)。
 *   wire_write / wire_wend : 実行体の結果を受けて組む pigwire 応答列 (private・§2.1)。
 *
 * ★ ワイヤ↔pigData 変換の位置 (2026-07-30 メモ L410 の要): 実行体 (ptsAgent 派生 =
 *   cgatsAgent/mfatsAgent) は **常に pigData だけを受け取り・返す**。文字列との変換は
 *   「パイプが挟まっているかどうか」を知っている Mediator の役割:
 *     C_OP         → (C_OP,       str=op 名)
 *     C_ARG_PATH   → (C_ARG_DATA, idx, data=pigDataCache(path))      ← パス文字列を cache ハンドルに
 *     C_ARG_INLINE → (C_ARG_DATA, idx, data=pig_value_parse(text))   ← 値リテラルを構造値に
 *     C_ARG_END    → (C_ARG_END,  data=pigDataCache(目標パス))   ← 出力キャッシュのハンドルに (§5.2)
 *   これで実行体の状態機械は External / Internal (ptsMediatorInternal) で完全に同一になり、
 *   受信側の分岐が 1 系統 (ptsMediatorPacket) に畳まれる。
 *   値の復号に **pig 層の pig_value_parse** を使う (言語非依存): planner が inline に載せるのは
 *   常に pigData::serialize() の出力で、その文法は pigValueCodec の文法と一致するため、srava の
 *   言語パーサ (cgptsLemonParser) を agent 側にリンクする必要がなくなった。
 */
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/osglue.h"   /* osglue_env_int (#3419 §17.2) */
#include	"pig/c++/ptsApplication.h"
#include	"ts2/c++/tsApplication.h"    /* ctor の parent 型 sPtr<tsApplication> */
#include	<stdlib.h>                   /* getenv: PIG_TEST_ERR_AFTER_SAVE (テスト用注入) */
#include	"pig/c++/pigData.h"
#include	"pig/c++/pigCacheCodec.h"   /* is_stream_body: A_SAVE_BEGIN の payload 判定 */
#include	"pig/c++/pigwire.h"
#include	"pig/c++/pigValueCodec.h"    /* pig_value_parse: inline 値テキスト → pigData */
#include	"pig/c++/pigAgentRegistry.h"
#include	"pig/c++/pigModuleRegistry.h"   /* ★ #3427 ③: app 所有レジストリ (PIG_MODLOAD_* / agents / codecs) */
#include	"pig/c++/ptsAgent.h"
#include	"pig/c++/ptsWirePipe.h"
#include	"pig/c++/ptsWirePacket.h"
#include	"pig/c++/ptsMediatorPacket.h"
#include	"ts2/c++/ts2IO.h"
#include	"ts2/c++/s2IOstd.h"          /* 自 stdin/stdout を portable に ts2IO 化(MinGW 対応) */
#include	"ts2/c++/tsSignal.h"         /* SIGINT/SIGTERM/SIGHUP を TSE_SIGNAL イベント化 (§5.4) */
#include	"ts2/c++/stdEvent.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ptsAgentApplication_.h"

#include	<string.h>
#include	<signal.h>   /* SIGINT / SIGTERM / SIGHUP (§5.4) */

CLASS_TINYSTATE(pig/c++/ptsAgentApplication,pig/c++/ptsApplication)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	/* _moduleFile: ロードするカーネル .so (argv[1])。基底 ptsApplication の INI が
	 * PIG_MODLOAD_FILE (RTLD_NOW) でロードする (#3427 ③)。0 = ロードなし (テスト等)。 */
	ptsAgentApplication_(
		sPtr<tsApplication> parent,
		const char *_moduleFile = 0);

	sRptr<tsApplication,tinyState>		parent;

	int		enable_body();   /* ACT_START から呼ぶ実体 */
	/* 破棄列。**界面ではない** (§2.1: 外からの終了要求は destroy() に統一) — 自分の FIN
	 * からだけ呼ぶ。 */
	void		teardown();
	/* ★ 終了系シグナルを捕まえて実行体へ destroy を送る (2026-08-02 メモ §5.4)。 */
	virtual sPtr<stdEvent>	filter(sPtr<stdEvent> ev);
protected:
	sPtr<tsSignal>		sig_int;   /* SIGINT  (Ctrl+C。planner と同じプロセスグループに届く) */
	sPtr<tsSignal>		sig_term;  /* SIGTERM (素の kill) */
	sPtr<tsSignal>		sig_hup;   /* SIGHUP  (端末切断) */
	sPtr<ts2IO>		rio;      /* 自 stdin(読み) */
	sPtr<ts2IO>		wio;      /* 自 stdout(書き) */
	sPtr<ptsWirePipe>	pipe;     /* pigwire レコード送受信 */
	sPtr<ptsAgent>		agent;    /* 実行体(cgatsAgent/mfatsAgent)。pigAgentRegistry から生成 */
	/* ★ 実行体から TSE_RETURN で受け取った結果 (2026-08-02 メモ §5.3)。
	 *   pigDataCache なら保存を見届けて A_SAVE_BEGIN → A_SAVE_DONE → A_BYE → 番兵、
	 *   pigDataError なら A_ERROR → 番兵。実行体はワイヤを知らない。 */
	sPtr<pigData>		agentResult;
	sPtr<pigDataCache>	outCache;
	/* 受信した C_OP の op 名。テスト用フォールトインジェクション (PIG_TEST_ERR_AFTER_SAVE) を
	 * 根 (union) の agent だけに効かせるために控える。実行体は opIdx を持つが、保存の見届けが
	 * 親へ移った (§5/§6) ので注入点もここになった。 */
	sPtr<stdString>		opName;
private:
	/* 受信 pigwire レコード → ptsMediatorPacket (pigData) に復号して実行体へ投函。 */
	void	forward_packet(sPtr<ptsWirePacket> pkt);
	/* 応答 pigData → pigwire レコード。§2.1 で Mediator 界面から a_write/a_wend が消えたので、
	 * これは ptsAgentApplication だけの private な道具 (実行体はワイヤを一切知らない)。 */
	int	wire_write(int cmd, sPtr<pigData> d);
	int	wire_wend();
	TS_DEFARGS
};

TS_END_IMPLEMENT

TS_BEGIN_INTERFACE
#include	"ts2/c++/sRptr.h"
#include	"ts2/c++/ts2IO.h"        /* rio/wio(sPtr メンバ)の完全型 */
#include	"pig/c++/ptsWirePipe.h"  /* pipe(sPtr メンバ)の完全型 */
#include	"pig/c++/ptsAgent.h"     /* agent(sPtr メンバ)の完全型 */
class tsApplication;
class tinyState;
class pigData;
class stdString;
class ptsWirePacket;
class tsSignal;
TS_END_INTERFACE

#endif


ptsAgentApplication_::ptsAgentApplication_(TS_ARGS0)
        : ptsApplication_(parent,
                          ( _moduleFile != 0 ) ? PIG_MODLOAD_FILE : PIG_MODLOAD_NONE,
                          _moduleFile),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

/* ACT_START から呼ぶ実体。0=成功 / -1=失敗。 */
int
ptsAgentApplication_::enable_body()
{
	/* ★ #3427 ③: app 所有レジストリから引く (このプロセスの唯一の登録 = INI でロードした .so)。 */
	/* ★ #3419 §7.5: agent プロセス側でも同じ initialize() の流れが要る (planner だけでは
	 * process 実行のモジュールが初期化されない)。積んでいる .so は 1 本なので名前は 0 で引く。 */
	if ( module_registry != thNULL ) {
		module_registry->ensure_initialized(0);
	}
	pigAgentFactory f = ( module_registry != thNULL ) ? module_registry->agent_factory(0) : 0;
	if ( f == 0 )
		return -1;
	sPtr<tinyState> self = ifThis;
	/* 自 stdin(fd0)=rio / stdout(fd1)=wio を portable に ts2IO 化(Linux=fd を ts2IOdescriptor で
	 * 包む / Windows=GetStdHandle+GetFileType でコンソール/パイプを判定)。生 fd 直指定を排し MinGW 対応。 */
	s2IOstd::init(self, &rio, &wio);
	if ( rio == thNULL || wio == thNULL )
		return -1;
	/* ★ 応答書き込みも分割書き込みに(planner 側 wfd と同じ理由)。値返し op の結果は A_SAVE_BEGIN に
	 * 本文相乗りで pipe へ返るので、巨大な値(大きな配列等)だと >64KB になり得る。ts2IO pipe の既定は
	 * 不可分書き込み(指定 length をきっちり書く)で、length>64KB は絶対成功せず・部分空きでは CPU100%
	 * ループになる。応答は連続バイト列で不可分性不要(レコード境界=上位の長さ前置)→ set_divisible 必須。 */
	wio->set_divisible();
	pipe = thNEW(ptsWirePipe,(self, rio, wio));
	agent = f(ifThis);   /* 実行体を起こす。parent=自分(Mediator) */
	/* ★ 終了系シグナルを TSE_SIGNAL イベント化する (§5.4)。**これが無いと agent process は
	 * Ctrl+C で即死する** — 端末の SIGINT はフォアグラウンドプロセスグループ全体に届くので、
	 * planner だけでなく agent process にも来る。既定動作のまま死ぬと、キャッシュを書いている
	 * 最中なら壊れたファイルが残る。filter() で受けて実行体に destroy を送り、保存中の処理は
	 * そのまま続行させる (ひさ指示: キャッシュが壊れないように)。 */
	sig_int  = thNEW(tsSignal,(self, SIGINT));
	sig_term = thNEW(tsSignal,(self, SIGTERM));
	sig_hup  = thNEW(tsSignal,(self, SIGHUP));
	return ( agent != thNULL ) ? 0 : -1;
}

/* 応答 pigData を pigwire レコードにする。**pigData → 文字列の符号化はこの
 * 親 (ワイヤを知っている側) の判断・役割** (2026-07-30 メモ L651。実行体は常に pigData で渡してくる):
 *   - A_ERROR      : **生メッセージ** (error_message() = 多態。pigDataError なら前置なしの msg)。
 *                    get_str() は "ERROR: " を前置し serialize() は引用符を付けるため、
 *                    どちらもワイヤ形と食い違う。
 *   - それ以外の値  : **serialize()** = VALUE 往復の正準形 (get_str だと float の小数点が落ち、
 *                    planner 側の再パースで整数化してしまう)。
 * sPtr を変数で保持するのは、temporary stdString が write 前に解放されて payload が
 * ダングリングするのを防ぐため。 */
int
ptsAgentApplication_::wire_write(int cmd, sPtr<pigData> d)
{
	if ( pipe == thNULL )
		return -1;
	if ( d == thNULL ) {
		pipe->write(cmd, 0, 0);
		return 0;
	}
	sPtr<stdString> body = ( cmd == A_ERROR ) ? d->error_message() : d->serialize();
	const char *p = body->get_str();
	pipe->write(cmd, (const uint8_t*)p, (int)::strlen(p));
	return 0;
}

int
ptsAgentApplication_::wire_wend()
{
	if ( pipe == thNULL )
		return -1;
	pipe->wend();
	return 0;
}

void
ptsAgentApplication_::teardown()
{
	if ( pipe.is_notNull() ) { pipe->destroy(); pipe = thNULL; }
	if ( rio.is_notNull() )  { rio->destroy();  rio  = thNULL; }
	if ( wio.is_notNull() )  { wio->destroy();  wio  = thNULL; }
}

/* イベント前処理: 終了系シグナルを捕まえて **実行体へ destroy を送る** (§5.4)。
 * ★ 自分 (ptsAgentApplication) は destroy しない。実行体が中断を pigDataError で返し (§6.3)、
 *   通常の RESULT → A_ERROR → 番兵 → CLOSING の経路で畳むので、planner には理由が届く。
 *   自分を destroy すると保存中でも畳んでしまい、書きかけのキャッシュが残る。
 * ★ 保存の見届け中 (SAVEBEGIN/SAVEDONE) に来た場合、実行体は既に FIN しているので destroy は
 *   空振りし、**保存はそのまま続行**する (ひさ指示どおり)。 */
sPtr<stdEvent>
ptsAgentApplication_::filter(sPtr<stdEvent> ev)
{
	if ( ev == thNULL )
		return ev;
	if ( ev->type == TSE_SIGNAL &&
	     ( ev->msg_int == SIGINT || ev->msg_int == SIGTERM || ev->msg_int == SIGHUP ) ) {
		if ( agent.is_notNull() )
			agent->destroy();   /* 実行体の TSE_RETURN は ACT_START が受ける */
	}
	return TS_BASECLASS::filter(ev);
}

/* 受信レコードを pigData に復号して実行体へ投函 (クラスコメントの対応表どおり)。
 * 壊れたレコード (index 欠け・未知 type) はここで落とす = 実行体はワイヤの不整合を知らない。 */
void
ptsAgentApplication_::forward_packet(sPtr<ptsWirePacket> pkt)
{
	if ( agent == thNULL || pkt == thNULL )
		return;
	int n = pkt->payload.length();
	int type = (int)pkt->type;
	sPtr<stdString> str;
	sPtr<pigData>   data;
	uint32_t        idx = 0;

	switch ( type ) {
	case C_OP:
		str = ( n > 0 )
		    ? sPtr<stdString>(thNEW(stdString,((const char*)&pkt->payload[0], 0, n)))
		    : sPtr<stdString>(thNEW(stdString,("")));
		opName = str;
		break;
	case C_ARG_END:
		/* ★ §5.2: 目標パス文字列から **出力 pigDataCache を組んで data に載せる**。これで
		 * C_ARG_END の正本が External / Internal のどちらでも data (pigDataCache) に揃い、
		 * 実行体はパス文字列を知らずに済む (自前の thNEW が要らなくなる)。
		 * payload が空 = planner が出力キャッシュを指定しなかった → data は thNULL のまま送り、
		 * 実行体が A_ERROR にする (agent process に診断チャネルが無い事情は cgatsAgent の
		 * C_ARG_END のコメントを参照)。 */
		if ( n > 0 )
			data = thNEW(pigDataCache,((pHashKeyType)0,
			    sPtr<stdString>(thNEW(stdString,((const char*)&pkt->payload[0], 0, n)))));
		break;
	case C_ARG_PATH:
	case C_ARG_INLINE: {
		if ( n < 4 )
			return;   /* [arg_index(u32)] が無い = 壊れたレコード */
		const uint8_t *p = &pkt->payload[0];
		idx = (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
		sPtr<stdString> text = thNEW(stdString,((const char*)(p+4), 0, n-4));
		if ( type == C_ARG_PATH )
			data = thNEW(pigDataCache,((pHashKeyType)0, text));   /* 入力キャッシュのハンドル */
		else
			data = pig_value_parse(text->get_str());              /* 値リテラル → 構造値 */
		type = C_ARG_DATA;   /* PATH/INLINE の弁別は消える (実行体は is_cache() で見る) */
		break;
	}
	case C_ENV: {
		/* ★ #3441 (ひさ設計 2026-08-26): module(so,{opts}) のハッシュ全体。**実行体 (agent) へは
		 * 転送しない** — これはモジュールの初期化に相当する話 (initialize と同じ扱い) であって、
		 * 個別 op の引数ではない。ここで直接 module_registry を上書きし、その場で configure()
		 * を呼んで完結させる (「planner と同じ方法で上書きする」)。
		 * module==0 の引き方は enable_body() の ensure_initialized(0)/agent_factory(0) と同じ
		 * (このプロセスが積んでいるモジュールは 1 本だけ)。 */
		if ( n > 0 && module_registry != thNULL ) {
			sPtr<stdString> text = thNEW(stdString,((const char*)&pkt->payload[0], 0, n));
			sPtr<pigData> opts = pig_value_parse(text->get_str());   /* 失敗なら pigDataError */
			if ( opts.is_notNull() && !opts->is_error() )
				module_registry->set_and_apply_opts(0, opts);
		}
		return;   /* agent (op 実行体) へは転送しない */
	}
	default:
		return;   /* agent 側で意味を持たないレコードは無視 */
	}
	agent->eventHandler(thNEW(stdEvent,(TSE_PACKET, ifThis,
		sPtr<stdObject>(thNEW(ptsMediatorPacket,(type, idx, data, str))))));
}



/*******************************************
	STATE MACHINE
********************************************/

TS_STATE(INI_ptsApplication_START)
{
	/* ★ #3427 ③: 基底 INI の .so ロード失敗 = 実行体を起こせない。即 FIN し main が
	 * module_load_failed() を終了コードへ写す (旧 g_load_failed 相当・エラーは基底が出力済)。 */
	if ( module_load_failed() )
		return rDO|FIN_START;
	/* ★ 生成は **自分の状態関数の中** で行う (2026-08-02 メモ §5.1)。子 (ptsWirePipe / 実行体) が
	 * thNEW 中に上げるイベントはキューに入るので、代入が済んだ次のディスパッチで安全に受け取れる。
	 * (他人の状態関数から enable させると代入前に届いて取りこぼす — #3411 の教訓。) */
	if ( enable_body() != 0 )
		return rDO|FIN_START;   /* 通信も実行体も作れない = このプロセスの仕事は無い */
	return ACT_START;               /* pipe / 実行体のイベント待ち → rDO なし */
}

TS_STATE(ACT_START)
{
	/* ★ destroy の作法 (ひさ指示 2026-08-06・2026-08-02 メモ §8.3 の PIGFAGENT_SHOULD_ABORT と同型)。
	 *   ① 子の TSE_RETURN を **先に** 見る
	 *   ② destroy 要求は「子へ destroy() を送って、TSE_RETURN が戻るのを待ち続ける」だけ
	 *   ③ 待つ子が居なくなって初めて FIN へ
	 * destroy を送る側は **戻ってくる内容に関知しない**。destroy された側が自分の終了処理を
	 * 行って TSE_RETURN を返す (即終了するか完走してからかは、その子の判断)。
	 * is_destroyed() は消滅要求フラグにすぎず、FIN へ進むのは状態関数の責任 (MENTAL_MODEL §4.2)。
	 * これを怠ると「何も待っていないのに終わらない tinyState 派生」が終了時まで残る (#3414)。 */
	if ( ev.is_notNull() ) {
		/* ★ pipe から来るのは受信レコードだけを扱う。TSE_ASSERT / TSE_RETURN は **実行体へ
		 * 転送しない** (2026-08-02 メモ §1 の整理: 実行体が知るのは ptsMediatorPacket だけ)。
		 * pipe の TSE_RETURN は「送受信とも終端」= 自分の後片付けの合図で、CLOSING が受ける。 */
		if ( ev->source == pipe && pipe.is_notNull() && ev->type == TSE_PACKET )
			forward_packet(sPtr<ptsWirePacket>::d_cast(ev->msg_obj));
		/* ★ pipe の TSE_RETURN = **planner が wfd を閉じた** (Ctrl+C 等で撤収した)。
		 * 応答を返す相手がもう居ないので自分も畳む。以前ここを無視していたのは、
		 * 「登録された fwIO が無くなればイベントループが枯れてプロセスが死ぬ」に
		 * 暗黙に頼れていたから。§5.4 で tsSignal を登録した途端その前提が崩れ、
		 * agent process が終わらず **継承した stdout を握ったまま**になって
		 * ctest が EOF を待ち続けた (srava_planner_sigint 等が Timeout)。 */
		if ( ev->source == pipe && pipe.is_notNull() && ev->type == TSE_RETURN )
			pipe = thNULL;
		/* 実行体の結果 (pigDataCache / pigDataError)。ここから保存の見届けに入る。 */
		if ( ev->source == agent && ev->type == TSE_RETURN ) {
			agentResult = sPtr<pigData>::d_cast(ev->msg_obj);
			return rDO|ACT_ptsAgentApplication_RESULT;
		}
		if ( ev->source == agent && ev->type == TSE_DESTROY ) { return rDO|FIN_START; }   /* 結果を返さず畳まれた = 異常 */
	}
	/* 撤収条件は「自分が destroy された」か「相手 (planner) が居なくなった」。どちらも
	 * 実行体へ destroy を送り、その TSE_RETURN (= 上の分岐) を待つ。実行体が計算中なら
	 * 計算が終わるまで戻ってこないが、それは実行体側の判断でこちらは関知しない。 */
	if ( is_destroyed() || pipe == thNULL ) {
		if ( agent.is_notNull() ) { agent->destroy(); return 0; }
		return rDO|FIN_START;
	}
	return 0;
}

TS_STATE(ACT_ptsAgentApplication_RESULT)   /* 結果の種別で分岐 */
{
	outCache = sPtr<pigDataCache>::d_cast(agentResult);
	if ( outCache == thNULL ) {
		/* pigDataError (またはそれ以外) = エラー。ワイヤ化 (生メッセージ) は wire_write の役割。 */
		wire_write(A_ERROR, ( agentResult != thNULL ) ? agentResult
		    : sPtr<pigData>(thNEW(pigDataError,("agent returned no result"))));
		return rDO|ACT_ptsAgentApplication_WEND;
	}
	return rDO|ACT_ptsAgentApplication_SAVEBEGIN;
}

/* ★ 保存の見届け (SAVEBEGIN/SAVEDONE/BYE/WEND) には **is_destroyed() を入れない**。
 * 「エージェントはキャッシュを書き切ることが善」= 消滅要求で書込を打ち切らない (ひさ回答 2026-08-05)。
 * 中断してよいのは「まだ何も書いていない」ACT_START と「もう書き終えた」CLOSING だけ。 */
TS_STATE(ACT_ptsAgentApplication_SAVEBEGIN)   /* メタ書込済 = 下流が attach 可 になったら送る */
{
	/* is_valid() は「問い合わせ + 購読」を兼ねる (走行中なら TSE_ASSERT で起こしてくれる)。 */
	if ( ! outCache->is_valid() ) { return 0; }
	/* payload は本文の形で決める (§3.1 と対): ストリーム系 (D_CHUNK/D_REF) は空 —
	 * planner はキャッシュハンドルで受け取る。値 (D_TEXT) は serialize を相乗りさせる。
	 * 判定は保存時に選ばれた writer と同一基準 (pigCacheCodec::is_stream_body)。 */
	sPtr<pigData> body = outCache->get_body();
	if ( body != thNULL && module_registry != thNULL && ! module_registry->is_stream_body(body) )
		wire_write(A_SAVE_BEGIN, body);
	else
		wire_write(A_SAVE_BEGIN, thNULL);
	return rDO|ACT_ptsAgentApplication_SAVEDONE;
}

TS_STATE(ACT_ptsAgentApplication_SAVEDONE)   /* 本体まで書き終わったら送る */
{
	if ( ! outCache->is_complete() ) { return 0; }
	/* テスト用フォールトインジェクション: 継続 (promise) 解決済み = A_SAVE_BEGIN 送信後に
	 * agent 側がエラーを出すケース。プランナーの ptsApp->set_agentError 経路を検証する。
	 * 根 (union) のみに限定 (box まで巻き込むと cascade abort で検証点が曖昧になる)。
	 * §5/§6 で保存の見届けが実行体から親へ移ったので、注入点も cgatsAgent/mfatsAgent の
	 * SAVEWRITEWAIT からここへ移設した。 */
	if ( osglue_env_int("PIG_TEST_ERR_AFTER_SAVE", 0)
	     && opName != thNULL && ::strcmp(opName->get_str(), "union") == 0 ) {
		wire_write(A_ERROR, thNEW(pigDataError,("injected error after save (test)")));
		return rDO|ACT_ptsAgentApplication_WEND;
	}
	wire_write(A_SAVE_DONE, thNULL);
	return rDO|ACT_ptsAgentApplication_BYE;
}

TS_STATE(ACT_ptsAgentApplication_BYE)
{
	wire_write(A_BYE, thNULL);
	return rDO|ACT_ptsAgentApplication_WEND;
}

TS_STATE(ACT_ptsAgentApplication_WEND)
{
	wire_wend();   /* 番兵。これで pipe の「送信終端」が立つ (pipe が無ければ no-op) */
	if ( pipe == thNULL )
		return rDO|FIN_START;   /* 相手が先に閉じた = 待つ TSE_RETURN はもう来ない */
	return ACT_ptsAgentApplication_CLOSING;   /* pipe の TSE_RETURN 待ち → rDO なし */
}

TS_STATE(ACT_ptsAgentApplication_CLOSING)
{
	/* 送受信とも終端に達した pipe が TSE_RETURN を返してくる (§7.1)。 */
	if ( ev->type == TSE_RETURN && ev->source == pipe ) { return rDO|FIN_START; }
	if ( is_destroyed() ) {
		if ( pipe.is_notNull() ) { pipe->destroy(); return 0; }   /* pipe の TSE_RETURN を待つ */
		return rDO|FIN_START;
	}
	return 0;
}

TS_STATE(FIN_START)
{
	teardown();       /* pipe / rio / wio を destroy + thNULL */
	/* tsSignal は tsSignalCore の fwIO をイベントループに登録したまま生かすので、明示的に
	 * destroy しないと全状態が終わってもループが終わらずプロセスが残る (planner と同じ)。 */
	if ( sig_int.is_notNull() )  { sig_int->destroy();  sig_int  = thNULL; }
	if ( sig_term.is_notNull() ) { sig_term->destroy(); sig_term = thNULL; }
	if ( sig_hup.is_notNull() )  { sig_hup->destroy();  sig_hup  = thNULL; }
	agent = thNULL;
	agentResult = thNULL;
	outCache = thNULL;   /* ★ 実行体の参照も手放す (2026-08-02 メモ §9)。destroy はしない —
	                   * ここへ来る時点で実行体は畳み終えている (ACT_START の TSE_RETURN/DESTROY 判定)。 */
	return rDO|FIN_ptsApplication_START;
}
