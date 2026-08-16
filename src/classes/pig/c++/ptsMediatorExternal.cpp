/*
 * ptsMediatorExternal — agent process と会話する Mediator (#3406, docs/mediator_design.md §2.3)。
 *
 * 現行 pigfAgent が直接抱えていた通信部材 (ts2System / rfd / wfd / ptsWirePipe) と
 * 起動列 (LAUNCH)・破棄列 (FIN) をそのまま内包する。ワイヤは pigwire レコードで
 * byte 互換 (段階 4.1: 旧 agent バイナリのまま動くこと)。
 *
 *   enable()   : ts2System 起動 → wfd->set_divisible() → ptsWirePipe 生成。fork 失敗は
 *                同期で非0 を返す (pigfAgent がエラー処理。PIG_TEST_FORKLIMIT も呼び元)。
 *   ACT_START  : pipe のイベントを **意味ごとに処理** して parent へ渡す (§3.1):
 *                  TSE_ASSERT (handshake 完了) → そのままリレー
 *                  TSE_PACKET → **ptsWirePacket を ptsMediatorPacket へ変換** (handle_packet)
 *                  TSE_RETURN (切断) → 会話終了 → 自分も FIN (parent へ TSE_RETURN 1 回)
 *                ts2System 由来のイベント (子プロセス終了等) は扱わない (現行 pigfAgent も
 *                無視していた。TSE_RETURN を転送すると「pipe 切断」と誤認されるため)。
 *   teardown() : 自分の FIN からだけ呼ぶ破棄列。wfd destroy (=agent stdin EOF: 待機中 agent を終わらせる唯一の手段。
 *                ts2System は sh -c 経由で実 agent が孫プロセスのため pid kill は届かない、
 *                tinyState #3363) → ts2System destroy → pipe/rfd/wfd destroy。
 *                fd を明示的に閉じるのは macOS EMFILE 対策 (呼び元 pigfAgent は
 *                liveAgents に参照され続け program 終了まで生存するため)。
 *
 * ★ ワイヤ↔pigData 変換の位置 (§3.1): planner (pigfAgent) が見るのは External / Internal の
 *   どちらでも **ptsMediatorPacket だけ**。生バイト列・パス文字列・値テキストは Mediator の
 *   内側で完結する (agent 側で ptsAgentApplication が受信方向にやっているのと対称)。
 *
 * 送信 (pl_write_*) は pipe へのマッピングのみ。yield (write_c の sException / 引数の
 * compact ゲート) は呼び元状態関数の再走で再入され、pipe の pico state が再開を担保する
 * (現行 pigfAgent の SEND 系と同じ規約: 1 状態 1 write)。
 */
#include	"pig/c++/ptsMediator.h"
#include	"pig/c++/ptsApplication.h"   /* ptsApp 値メンバの完全型(ptsObject.h から移動・#3406 4.2) */
#include	"pig/c++/pigwire.h"
#include	"pig/c++/ptsWirePipe.h"
#include	"pig/c++/ptsWirePacket.h"        /* 着信レコード (変換元・§3.1) */
#include	"pig/c++/ptsMediatorPacket.h"    /* parent へ渡す pigData 直渡しパケット (変換先・§3.1) */
#include	"pig/c++/pigValueParser.h"
#include	"pig/c++/pigModuleRegistry.h"   /* ★ #3427 ③: app 所有レジストリ (vparser) */
#include	"pig/c++/pigData.h"
#include	"ts2/c++/ts2System.h"
#include	"ts2/c++/ts2IO.h"
#include	"ts2/c++/stdEvent.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ptsMediatorExternal_.h"

CLASS_TINYSTATE(pig/c++/ptsMediatorExternal,pig/c++/ptsMediator)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	/* agentCmd (起動コマンド = agent_cmd() の結果) は codegen が ctor 引数から
	 * 同名メンバを自動生成して代入する (TS_CPARGS0 の流儀)。 */
	ptsMediatorExternal_(
		sPtr<ptsObject> parent,
		sPtr<stdString> agentCmd);

	sRptr<ptsObject,tinyState>		parent;

	virtual int	enable();
	int		enable_body();   /* ACT_START から呼ぶ実体 (thNEW を自分の実行中に行う) */
	virtual int	launch_failed();  /* 起動失敗が確定したか (pigfAgent がエラー文言を作るのに使う) */
	virtual int	child_status();   /* 子 agent の waitpid 生 status (終了していれば・未終了は -1) */
	/* ★ agent (ts2System=子プロセス) の TSE_RETURN を握りつぶして agentReturnFlag に畳む。
	 * pigfAgent が med に対してやっているのと同じ作法 (§8.3)。「子が終了した」はどの状態に
	 * 居ても意味を持つ横断的事実なので、イベントではなくフラグで持つ。 */
	virtual sPtr<stdEvent>	filter(sPtr<stdEvent> ev);
	virtual int	pl_write_arg(int idx, sPtr<pigData> d);
	virtual int	pl_write_end(sPtr<pigDataCache> outCache);
protected:
	/* 破棄列。**界面ではない** (§2.1/§3.2: 外からの終了要求は destroy() に統一) — 自分の
	 * FIN からだけ呼ぶ。 */
	void		teardown();
	virtual int	pl_write_str(int cmd, sPtr<stdString> s);
	/* ★ §3.1: 着信レコードを「意味」に変換して parent へ渡す道具立て。 */
	void		handle_packet(sPtr<ptsWirePacket> pkt);   /* 1 レコードを解釈 */
	void		post_packet(int type, sPtr<pigData> d);   /* ptsMediatorPacket にして投函 */
	void		flush_held();                             /* 値パース中に溜めた分を順に処理 */
	sPtr<ts2System>		agent;    /* agent process (sh -c 経由) */
	sPtr<ts2IO>		rfd;      /* 子の stdout (読み) */
	sPtr<ts2IO>		wfd;      /* 子の stdin (書き) */
	sPtr<ptsWirePipe>	pipe;     /* pigwire レコード送受信 */
	/* pl_write_end で受け取った出力キャッシュ。A_SAVE_BEGIN の戻りに使う (§3.2)。 */
	sPtr<pigDataCache>	outCache;
	/* ★ 値 (非 mesh) の A_SAVE_BEGIN を pigData へ戻す非同期パーサ (§3.1)。走行中は後続レコードを
	 * held に溜める — 先に A_SAVE_DONE を通すと parent が結果より先に BYE へ進んでしまう。 */
	sPtr<tinyState>			vparser;
	sArray<sPtr<ptsWirePacket> >	held;
	int			endSent;  /* C_ARG_END を書き切ったか (pl_write_end の二重書き防止) */
	/* ★ retSent (parent への TSE_RETURN 二重送出の番人) は撤去 (ひさ指示 2026-08-11)。
	 * 送出を FIN_AGENTWAIT の 1 箇所だけにし、送ったら即 FIN_ptsObject_START へ抜けるので
	 * 構造として 1 回しか通らない。 */
	int			errCode;  /* TSE_RETURN の msg_int (0=正常 / -1=起動失敗) */
	int			childStatus;   /* 子 agent の waitpid 生 status (終了していれば) */
	int			agentReturnFlag;  /* ★ 子プロセス (ts2System) が終了して TSE_RETURN を返したか */
	int			retPid;
	/* ★ enable の遅延実行フラグ (#3406, 2026-08-01 ひさ指示)。
	 *   0=未要求 / 1=要求済み(ACT_START で本実行)。★2026-08-11: 「実行済み」「起動失敗」は
	 *   名前付き状態 (ACT_RUN / FIN) と errCode が表すので、このフラグから外した。
	 *   enable() は「他人(pigfAgent)の状態関数の中」から呼ばれるので、そこで子を thNEW すると
	 *   自分は実行中でないため子の TSE_ASSERT が**即時ディスパッチ**され、pipe への代入が
	 *   終わる前に ACT_START が走って取りこぼす(実測の刺さり原因)。本実行を自分の ACT_START に
	 *   移すと、自分が実行中なので子のイベントはキューに入り、次回に pipe 代入済みで受け取れる。 */
	int			enableFlag;
private:
	TS_DEFARGS
};

TS_END_IMPLEMENT

TS_BEGIN_INTERFACE
#include	"ts2/c++/sRptr.h"
#include	"ts2/c++/sArray.h"
class tinyState;
class ptsObject;
class pigData;
class pigDataCache;
class stdString;
class ts2System;
class ts2IO;
class ptsWirePipe;
class ptsWirePacket;
TS_END_INTERFACE

#endif


ptsMediatorExternal_::ptsMediatorExternal_(TS_ARGS0)
        : ptsMediator_(parent),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
    retPid = 0;
    endSent = 0;
    agentReturnFlag = 0;
    errCode = 0;
    enableFlag = 0;
}




/*******************************************
	INSTANCE FUNCTIONS
********************************************/

/* ★ 薄いインタフェース (#3406, 2026-08-01 ひさ指示): 要求フラグを立てて自分を起こすだけ。
 * 実体 (ts2System 起動 + ptsWirePipe 生成 + pipe 代入) は自分の ACT_START で行う。
 * 理由: enable() の呼び元は **他人の状態関数** (pigfAgent LAUNCH) なので、ここで子を thNEW すると
 *   自分が実行中でない → 子の TSE_ASSERT が即時ディスパッチされ、pipe 代入前の ACT_START が
 *   それを取りこぼす (planner↔agent が相互に永久待ちになる実バグだった)。
 *   pipe という 1 インスタンスを複数の並行コンテキストがノーガードで触っていた形。
 * 戻り値: 同期で分かる不備 (コマンド未設定) だけ非0。起動失敗は非同期 (下記 ACT_START) で通知。 */
int
ptsMediatorExternal_::enable()
{
	if ( agentCmd == thNULL )
		return -1;
	enableFlag = 1;
	wakeup();
	return 0;
}

/* 子 (agent) が終了していれば waitpid の生 status、まだなら -1。pigfAgent が
 * 「版が違う」等の具体的なエラー文を作るのに使う。 */
int
ptsMediatorExternal_::child_status()
{
	return ( agentReturnFlag ) ? childStatus : -1;
}

int
ptsMediatorExternal_::launch_failed()
{
	/* ★ 旧 enableFlag == -1 から errCode へ (2026-08-11)。起動失敗は errCode = -1 で表す
	 * (enableFlag は「enable() が呼ばれた」1 ビットに縮小したため)。 */
	return ( errCode == -1 ) ? 1 : 0;
}

/* ★ agent (ts2System) の TSE_RETURN = 子プロセス終了 (msg_int は waitpid の生 status) を
 * フラグに畳んで握りつぶす。従来から ts2System 由来のイベントは状態関数で扱っていない
 * (TSE_RETURN を転送すると「pipe 切断」と誤認される) ので、握りつぶす点は現行どおり。
 * 変わったのは **記録して FIN_AGENTWAIT が待てるようにした**こと。 */
sPtr<stdEvent>
ptsMediatorExternal_::filter(sPtr<stdEvent> ev)
{
	if ( ev == thNULL )
		return ev;
	if ( ev->type == TSE_RETURN && ev->source == agent ) {
		agentReturnFlag = 1;
		childStatus = (int)ev->msg_int;   /* waitpid の生 status (版不一致の判定に使う) */
		wakeup();       /* 置き換えイベントで通知 (握りつぶすと状態関数が走らないため) */
		return thNULL;  /* TSE_RETURN は握りつぶし */
	}
	return TS_BASECLASS::filter(ev);
}

/* ACT_START から呼ぶ実体。0=成功 / -1=起動失敗。 */
int
ptsMediatorExternal_::enable_body()
{
	agent = thNEW(ts2System,(ifThis, &retPid, agentCmd->get_str(), &rfd, (sPtr<ts2IO>*)0, &wfd, 0));
	if ( retPid < 0 || rfd == thNULL || wfd == thNULL ) {
		/* fork 失敗 (典型は EAGAIN=同時プロセス上限超過)。失敗オブジェクトを破棄する
		 * (ret<0 の ts2System は kill されない)。エラー方針は呼び元 (pigfAgent)。 */
		agent = thNULL; rfd = thNULL; wfd = thNULL;
		return -1;
	}
	/* ★ wfd は分割書き込みモード必須。ts2IO pipe の既定は不可分書き込みで、
	 * 64KB 超のインライン引数が絶対に書けない/空き待ちで CPU100% になり得る
	 * (詳細は旧 pigfAgent LAUNCH のコメント・repro_pipe)。 */
	wfd->set_divisible();
	pipe = thNEW(ptsWirePipe,(ifThis, rfd, wfd));
	return 0;
}

int
ptsMediatorExternal_::pl_write_str(int cmd, sPtr<stdString> s)
{
	if ( pipe == thNULL )
		return -1;
	pipe->write_str(cmd, s);   /* yield 時 ps_write_record で再開 (再入安全) */
	return 0;
}

int
ptsMediatorExternal_::pl_write_arg(int idx, sPtr<pigData> d)
{
	if ( pipe == thNULL )
		return -1;
	/* PATH/INLINE の弁別は Mediator の責務 (Internal ではこの弁別ごと消える)。
	 * is_cache/get_str/serialize は compact ゲートで yield し得る → 呼び元再走で再入。 */
	if ( d->is_cache() )
		pipe->write_arg(C_ARG_PATH, (uint32_t)idx, d->get_str());     /* 入力キャッシュパス */
	else
		pipe->write_arg(C_ARG_INLINE, (uint32_t)idx, d->serialize()); /* 値リテラル(round-trip 形) */
	return 0;
}

/* C_ARG_END (計算開始) + W_END 番兵 (送信終端) をまとめて出す (§2.1: pl_wend 廃止)。
 * ワイヤ互換のため送るのはパスだけ — agent process は自分のプロセス内で pigDataCache を作るので
 * オブジェクトを渡しても意味がない。パスは outCache 自身が持っている (旧 path 引数は冗長だった)。
 * outCache は **戻りのために保存**する (§3.2: A_SAVE_BEGIN でキャッシュが返ってきたとき、
 * 同一のものであることを確かめて planner にはこのハンドルを返す)。
 *
 * ⚠ 1 状態関数の中で write_record を 2 回呼ぶので endSent ガードが要る。ptsWirePipe の
 *   write_record は「同じ 1 レコードの再開」には冪等 (pico state) だが、**1 つ目が完走した後に
 *   2 つ目が yield して呼び元が再走する**と 1 つ目が二重に書かれる (ptsWirePipe.cpp:35 の
 *   「1 状態で複数回呼ぶ二重書きは未対応」)。wend() 自身は sentEnd で冪等なのでガード不要。 */
int
ptsMediatorExternal_::pl_write_end(sPtr<pigDataCache> outCache)
{
	if ( pipe == thNULL )
		return -1;
	this->outCache = outCache;
	if ( ! endSent ) {
		pl_write_str(C_ARG_END, outCache->get_path());
		endSent = 1;   /* ここへ来た = C_ARG_END は完走 (yield なら上で抜けている) */
	}
	pipe->wend();
	return 0;
}

void
ptsMediatorExternal_::teardown()
{
	if ( agent.is_notNull() ) {
		/* 待機中 (read ブロック) の実 agent を終わらせる唯一の手段はパイプ EOF (tinyState #3363)。 */
		if ( wfd.is_notNull() )
			wfd->destroy();
		/* ★ agent = thNULL にしない (ひさ指示 2026-08-11)。ts2System は **子プロセスが終了した
		 * とき**に TSE_RETURN (msg_int = waitpid の生 status) を返す。ここで手放すと受け手が
		 * 居なくなり、子の回収を待たずに parent へ「終わった」と返してしまう (実測: SIGTERM 後
		 * planner は 2 秒で消えるのに CGAL agent は 50 秒以上生存)。回収は FIN_AGENTWAIT が待つ。 */
		agent->destroy();
	}
	if ( pipe.is_notNull() ) { pipe->destroy(); pipe = thNULL; }
	if ( rfd.is_notNull() )  { rfd->destroy();  rfd  = thNULL; }
	if ( wfd.is_notNull() )  { wfd->destroy();  wfd  = thNULL; }
	if ( vparser.is_notNull() ) { vparser->destroy(); vparser = thNULL; }
	held.length(0);
	outCache = thNULL;   /* §9: 終了時点で手放す */
}

/* 変換済みレコードを parent (pigfAgent) へ渡す。ワイヤ由来の idx / 生テキストは外へ出さない
 * (parent が見るのは Internal と同じ ptsMediatorPacket だけ・§3.1)。 */
void
ptsMediatorExternal_::post_packet(int type, sPtr<pigData> d)
{
	if ( parent == thNULL )
		return;
	parent->eventHandler(thNEW(stdEvent,(TSE_PACKET, ifThis,
		sPtr<stdObject>(thNEW(ptsMediatorPacket,(type, 0, d, thNULL))))));
}

/* 着信 1 レコードを **意味** に変換する (2026-08-02 メモ §3.1)。
 *
 * A_SAVE_BEGIN: payload の有無で値/ストリームを弁別する (案 C・ひさ回答 2026-08-05)。
 *   - 空   = mesh 等のストリーム系。本文はキャッシュにあるので、**pl_write_end で預かった
 *            outCache をそのまま返す**。⚠ メモ §3.1 の「同一のキャッシュが戻ってきていることを
 *            確認して」は案 C 以前の記述で実行不能 — 空 payload には照合する材料がワイヤに
 *            載っていない (ひさ確認 2026-08-05)。
 *   - 非空 = 値。中身は agent 側 pigData::serialize() の出力なので、パーサで構造化 pigData に
 *            戻す。言語パーサ (srava) が登録されていればそちら (非同期・厳密有理数まで扱える)、
 *            無ければ pig 層の同期パーサ。使い分けの規約は pigValueParser.h。
 * A_ERROR    : テキストを pigDataError にする。ソース位置 (pigInfo) は planner 側の文脈なので
 *              付けない — parent が front->get_info() で付け直す (Internal と同じ扱い)。
 * A_SAVE_DONE: 中身を持たない合図なので型だけ。
 * それ以外 (A_BYE / 番兵) は planner の関心事ではないのでここで落とす。 */
void
ptsMediatorExternal_::handle_packet(sPtr<ptsWirePacket> pkt)
{
	if ( pkt == thNULL )
		return;
	int n = pkt->payload.length();
	switch ( pkt->type ) {
	case A_SAVE_BEGIN: {
		if ( n == 0 ) {
			post_packet(A_SAVE_BEGIN, sPtr<pigData>::d_cast(outCache));
			break;
		}
		sPtr<stdString> text = thNEW(stdString,((const char*)&pkt->payload[0], 0, n));
		pigValueParserFn mk = ( ptsApp != thNULL && ptsApp->module_registry != thNULL )
		    ? ptsApp->module_registry->vparser.get() : 0;
		if ( mk != 0 ) {
			vparser = mk(ifThis, text);
			if ( vparser != thNULL )
				break;   /* 子の TSE_RETURN で post する。それまで後続は held へ */
		}
		post_packet(A_SAVE_BEGIN, pigValueParser::parse_sync(text));
		break;
	}
	case A_SAVE_DONE:
		post_packet(A_SAVE_DONE, thNULL);
		break;
	case A_ERROR: {
		sPtr<stdString> msg = ( n > 0 )
		    ? sPtr<stdString>(thNEW(stdString,((const char*)&pkt->payload[0], 0, n)))
		    : sPtr<stdString>(thNEW(stdString,("agent error")));
		post_packet(A_ERROR, thNEW(pigDataError,(msg)));
		break;
	}
	default:
		break;
	}
}

/* 値パース中に溜めたレコードを順に処理する。処理の途中で新たなパースが始まることは無い
 * (A_SAVE_BEGIN は 1 回だけ) が、念のため vparser が立ったら止める。 */
void
ptsMediatorExternal_::flush_held()
{
	while ( held.length() > 0 && vparser == thNULL )
		handle_packet(held.shift());
}


/*******************************************
	STATE MACHINE
********************************************/

/* enable() 待ち。★ enableFlag を状態の代用にせず、**名前付き状態で段階を表す** (ひさ指示
 * 2026-08-11)。enableFlag は「enable() が呼ばれた」の 1 ビットだけを運び、起動が済めば用済み。
 * enable の本実行をここ (自分が実行中) で行うのは 2026-08-01 のひさ指示: そうしないと、ここで
 * thNEW した子が上げるイベントが pipe 代入前に即時ディスパッチされて取りこぼされる。 */
TS_STATE(ACT_START)
{
	if ( enableFlag == 0 ) {
		if ( is_destroyed() )
			return rDO|FIN_START;   /* enable される前に畳まれた */
		return 0;                       /* enable() の wakeup 待ち */
	}
	enableFlag = 0;
	if ( enable_body() != 0 ) {
		/* 起動失敗を親 (pigfAgent) へ非同期通知して自分は終わる。HELLO は med からの
		 * TSE_RETURN を撤収イベントとして既に扱う (launch_failed() で理由を区別する)。
		 * 通知は FIN 経路が 1 回だけ出す (§3.1)。 */
		errCode = -1;
		return rDO|FIN_START;
	}
	return rDO|ACT_RUN;
}

/* 通信確立後の定常状態 (旧 ACT_START の後半)。ここに居ること自体が「起動済み」を意味するので、
 * 旧 `enableFlag == 2` の判定は不要になった。 */
TS_STATE(ACT_RUN)
{
	/* pipe のイベントを **意味ごとに** 処理する (2026-08-02 メモ §3.1)。
	 * ts2System 由来 (子プロセス終了通知等) は扱わない (現行挙動の維持)。 */
	if ( ev->source == pipe && pipe.is_notNull() ) {
		switch ( ev->type ) {
		case TSE_ASSERT:
			/* 通信確立 (handshake 完了) の合図。中身を持たないのでそのままリレー。 */
			parent->eventHandler(thNEW(stdEvent,
				(TSE_ASSERT, ifThis, ev->seq, ev->msg_int, ev->msg_ptr, ev->msg_obj)));
			break;
		case TSE_PACKET:
			/* ★ ワイヤ (ptsWirePacket) → 意味 (ptsMediatorPacket) の変換点。値パースの
			 * 走行中は順序を守るため溜める (先に A_SAVE_DONE を通すと parent が結果より
			 * 先に BYE へ進んでしまう)。 */
			if ( vparser.is_notNull() )
				held.push(sPtr<ptsWirePacket>::d_cast(ev->msg_obj));
			else
				handle_packet(sPtr<ptsWirePacket>::d_cast(ev->msg_obj));
			break;
		case TSE_RETURN:
			/* pipe は TSE_RETURN を必ず 1 回だけ返して自分で畳む (§7)。以後は待つ子が居ない
			 * = agent との会話は終わり。parent への TSE_RETURN は **リレーせず**、自分が
			 * FIN することで 1 回だけ出す (§3.1・Internal の DONE と同じ位置づけ)。 */
			pipe = thNULL;
			break;
		default:
			break;
		}
	}
	/* 値パース完了 → 保留していた A_SAVE_BEGIN を構造化 pigData で出し、溜めた後続を流す。
	 * パース不能は agent 側のバグなので、生テキストへフォールバックせず明示エラーにする。 */
	if ( ev->type == TSE_RETURN && ev->source == vparser ) {
		sPtr<pigData> v = sPtr<pigData>::d_cast(ev->msg_obj);
		vparser = thNULL;
		post_packet(A_SAVE_BEGIN, ( v != thNULL ) ? v
		    : sPtr<pigData>(thNEW(pigDataError,("agent returned unparseable value"))));
		flush_held();
	}
	/* ★ §3.1: pipe が閉じた = agent との会話は終わり → parent へ TSE_RETURN を返して終了する。
	 * 値パースの走行中は **結果を出し切ってから** (先に TSE_RETURN が届くと planner は
	 * 「保存前に閉じた」と誤認する)。 */
	if ( pipe == thNULL && vparser == thNULL )
		return rDO|FIN_START;
	/* ★ destroy の作法 (ひさ指示 2026-08-06)。destroy を送る側は TSE_RETURN が戻るのを待つだけで、
	 * 中身に関知しない。destroy された側が自分の終了処理をする。
	 * §3.2: 待機中の実 agent を終わらせる唯一の手段は stdin の EOF なので wfd を閉じ、
	 * pipe の TSE_RETURN を待ってから畳む。**即座に殺さない**のは、agent が書きかけの
	 * キャッシュを書き切れるようにするため (ひさ回答 2026-08-05)。 */
	if ( is_destroyed() ) {
		if ( pipe.is_notNull() ) {
			if ( wfd.is_notNull() ) { wfd->destroy(); wfd = thNULL; }
			pipe->destroy();
			return 0;   /* pipe の TSE_RETURN を待つ */
		}
		if ( vparser.is_notNull() ) {
			vparser->destroy();
			return 0;   /* 値パーサ子の TSE_RETURN を待つ (同じ作法) */
		}
		return rDO|FIN_START;   /* 待つ子は居ない */
	}
	return 0;
}

TS_STATE(FIN_START)
{
	teardown();
	return rDO|FIN_AGENTWAIT;
}

/* ★ 子プロセス (ts2System) の回収待ち (ひさ指示 2026-08-11)。teardown() が agent->destroy() で
 * SIGTERM を送ってあるので、その終了通知 (filter が agentReturnFlag に畳んだ TSE_RETURN) を待つ。
 * これを待たずに parent へ TSE_RETURN を返していたのが、planner だけ先に exit して計算中の
 * agent が取り残される原因だった。pigfAgent 側の FIN_pigfAgent_MEDWAIT と対になる修正。
 * agent を起動していない (thNULL) 経路は待つものが無くそのまま通過する。
 * ★ 待ちを FIN_START に置かず別状態にしているのは、`return 0` で待つと FIN_START が再入して
 *   parent への TSE_RETURN が複数回飛ぶため。この構成なら 1 回だけ出るので retSent の番人は不要。 */
TS_STATE(FIN_AGENTWAIT)
{
	if ( ::getenv("PIG_DBG_TD") ) ::fprintf(stderr, "[td] med AGENTWAIT agent=%d flag=%d\n", (int)agent.is_notNull(), agentReturnFlag);
	if ( agent.is_notNull() && agentReturnFlag == 0 )
		return 0;   /* 子プロセスの終了待ち (filter がフラグを立てて wakeup を積む) */
	agent = thNULL;   /* ★ agent を手放すのはここ 1 箇所だけ (filter の source 照合を壊さないため) */
	/* 自分の終了は parent へ TSE_RETURN で **必ず 1 回** (ptsMediator の規約・§3.1)。
	 * pigfAgent の BYE / RESULT / HELLO がこれを撤収イベントとして待っている。 */
	if ( parent.is_notNull() )
		parent->eventHandler(thNEW(stdEvent,(TSE_RETURN, ifThis, (INTEGER64)errCode)));
	return rDO|FIN_ptsObject_START;
}
