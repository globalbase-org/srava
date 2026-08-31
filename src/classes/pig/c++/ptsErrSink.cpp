/*
 * ptsErrSink — agent (子プロセス) の stderr を吸ってテキストとして溜める tinyState 派生。
 *
 * ★なぜ要るか (2026-08-26 bench):
 *   `ts2System` は efd に `nullptr` を渡すと子の stderr を **`ts2IOdevNull` へ自動排水**する。
 *   排水されているので詰まりはしないが、**中身は誰も見ない**。そのためモジュールがリンクした
 *   ライブラリが致命エラーを stderr に書いて死んでも、planner 側には
 *   `agent closed unexpectedly` としか出ず **原因が読めなかった**。
 *   (実例: geogram の arrangement が `Did not manage to sort a bundle` → runtime_error →
 *    ★並列区間のワーカースレッドから投げるのでモジュール側の catch を素通り → terminate →
 *    agent が SIGABRT。理由は stderr に全部書いてあるのに捨てていた。)
 *
 *   ⇒ devNull の代わりにこれを噛ませ、**捨てずにテキストへ溜める**。
 *
 * ★役割分担 (ひさ設計 2026-08-26):
 *   - **排水の責務はこのクラスが持つ**。pigfAgent に排水させると状態機械が I/O を抱え込んで重い
 *   - **読んで判断するのは `ptsMediatorExternal`**。子の終了 status・ここに溜まった stderr・
 *     wire (ptsWirePipe) の終了コードを総合して `compose_agent_error()` が pigDataError にする
 *   - それを TSE_RETURN の msg_obj に載せて pigfAgent へ渡す = **本来のエラー申告ルート**
 *   ⚠ pigfAgent がここのテキストを直接引きに行くのは層破り (最初そう書いて撤回した)
 * ⚠ **External だけの話**。ptsMediatorInternal は子プロセスを持たないので無関係。
 *
 * 実装は `ts2IOdevNull` (tinyState) と同じ骨格 — 違いは read したバイトを捨てるか溜めるか。
 *   INI  : parent の TSE_DESTROY を listen
 *   PREV : io の生存を確認して listen
 *   START: io->read() を回す。<=0 (EOF/エラー) で FIN
 *   FIN  : io を destroy
 *
 * ⚠ **上限を持つ** (ERRSINK_KEEP)。stderr は行儀の悪いライブラリだといくらでも出てくるので、
 *   無制限に溜めるとメモリを食う。溢れたら **末尾を残す** (死因は最後に書かれることが多い)。
 *   捨てた事実は `truncated` で分かるようにし、取り出し時に印を付ける。
 */
#include	"pig/c++/ptsErrSink.h"
#include	"ts2/c++/ts2IO.h"
#include	"ts2/c++/stdString.h"
#include	"_ts2/c++/ptsErrSink_.h"

#include	<string.h>

CLASS_TINYSTATE(pig/c++/ptsErrSink,ts2/c++/tinyState)

#if 0

TS_BEGIN_IMPLEMENT

#define ERRSINK_READ	1000	/* 1 回の read で受けるバイト数 */
#define ERRSINK_KEEP	8192	/* 保持する上限。超えたら古い方を捨てて末尾を残す */
#define ERRSINK_LINES	512	/* text() が畳む対象にする行数の上限 (超えた分は無視) */

/**
 * @brief 子プロセスの stderr を読み捨てずにテキストへ溜める。/ Drains a child's stderr into a text buffer.
 * @details
 * `ts2IOdevNull` と同じく `io` から `read()` を回して排水するが、読んだバイトを捨てずに
 * 内部バッファへ溜める。親は `text()` で受け取る。<br>
 * 上限 `ERRSINK_KEEP` を超えた分は **先頭から捨てて末尾を残す**。
 */
class TS_THISCLASS : public TS_BASECLASS {
public:
	/** @brief 排水対象の ts2IO を渡して作成。
	 * @param parent 親 tinyState。
	 * @param io     子の stderr (親から見て読む側)。
	 */
	ptsErrSink_(
		sPtr<tinyState> parent,
		sPtr<ts2IO> io);

	/** @brief 溜まったテキストを返す。空なら thNULL。溢れていれば先頭に印が付く。 */
	sPtr<stdString>	text();

	/** @brief ★ 今この場で残りを読み切る (text() の直前に呼ぶ)。
	 * 子が死んだ通知 (waitpid) と、この sink が最後の塊を読む event とは **順序が保証されない**。
	 * そのため理由を組み立てる時点で空のことがある。子は既に回収済みなので、
	 * パイプに残っている分は待たずに読める。read() は yield しない生読みなので同期で呼べる。 */
	void	drain_now();

protected:
	/** @brief read した n バイトを keep へ追加する (溢れたら先頭を捨てる)。 */
	void	append(const uint8_t *p, int n);

	TS_DEFARGS
	uint8_t		buffer[ERRSINK_READ];
	char		keep[ERRSINK_KEEP + 1];   /* NUL 終端して持つ (生のまま) */
	char		flat[ERRSINK_KEEP + 256]; /* text() が組む 1 行版 (keep は壊さない) */
	int		lb[ERRSINK_LINES];        /* text() の行頭 index (keep 内) */
	int		le[ERRSINK_LINES];        /* text() の行末 index */
	int		kept;                     /* keep に入っているバイト数 */
	int		truncated;                /* 捨てた分があるか */
};

TS_END_IMPLEMENT

TS_BEGIN_INTERFACE
// predefine
#include	"ts2/c++/sRptr.h"
class tinyState;
class ts2IO;
class stdString;
TS_END_INTERFACE

#endif


ptsErrSink_::ptsErrSink_(TS_ARGS0)
        : tinyState_(parent)
{
    TS_CPARGS0
	kept = 0;
	truncated = 0;
	keep[0] = '\0';
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

/* 読んだ n バイトを keep へ追加する。溢れたら **先頭を捨てて末尾を残す**。
 * ★ NUL を含みうる (ライブラリが何を書くかは分からない) ので、テキスト化のときに潰す。 */
void
ptsErrSink_::append(const uint8_t *p, int n)
{
	if ( p == 0 || n <= 0 )
		return;
	if ( n >= ERRSINK_KEEP ) {          /* 1 回の read で上限を超えた: 末尾だけ残す */
		::memcpy(keep, p + (n - ERRSINK_KEEP), ERRSINK_KEEP);
		kept = ERRSINK_KEEP;
		truncated = 1;
	} else {
		if ( kept + n > ERRSINK_KEEP ) {   /* 足りない分だけ先頭を押し出す */
			int drop = kept + n - ERRSINK_KEEP;
			::memmove(keep, keep + drop, kept - drop);
			kept -= drop;
			truncated = 1;
		}
		::memcpy(keep + kept, p, n);
		kept += n;
	}
	keep[kept] = '\0';
}

/* ★ 残っている分をその場で読み切る。呼ぶのは「子が終了したと分かった後」だけ。
 * ⚠ 上限を置く: 万一 write 端を誰か (孫プロセス等) が握っていて延々と流れてきても、
 *   ここで止まらないようにする (沈黙ハングを作らない)。 */
void
ptsErrSink_::drain_now()
{
	if ( ! io.is_notNull() )
		return;                     /* 既に FIN 済み = EOF まで読み切っている */
	for ( int guard = 0 ; guard < 64 ; ++guard ) {
		int er = io->read(buffer, ERRSINK_READ);
		if ( er <= 0 )
			return;                 /* EOF / これ以上は無い */
		append(buffer, er);
	}
}

/* 溜めた stderr を **1 行の読めるテキスト**にして返す。
 * ★ そのまま繋ぐと使い物にならない: ライブラリは同じ警告を何十回も吐く (geogram の
 *   `(E)-[RadialSort] Both triangles ...` が連発する) ので、**肝心の FATAL 行が
 *   押し出される**。⇒ **同一行を全体で畳んで (xN) を付ける**。
 * ⚠ 連続分だけ畳むのでは足りない — 複数スレッドが同時に書くので同じ行が飛び飛びに出る。
 *   初出の順を保ったまま、以降の同一行を数えて捨てる。
 * ⚠ スレッドが行の途中で混ざったもの (実測: "…length 1Did not manage…") は完全一致しないので
 *   畳めず残る。これは元データの性質で、ここでは直せない。
 * ★ keep は壊さない (何度呼んでも同じ結果になるよう flat へ組む)。 */
sPtr<stdString>
ptsErrSink_::text()
{
	if ( kept <= 0 )
		return thNULL;

	/* --- ① 行に切る (行頭行末の空白と \r を落とし、空行は捨てる) --- */
	int nb = 0;
	for ( int i = 0 ; i < kept && nb < ERRSINK_LINES ; ) {
		int b = i;
		while ( i < kept && keep[i] != '\n' ) ++i;
		int e = i;
		if ( i < kept ) ++i;
		while ( e > b && ( keep[e-1] == '\r' || keep[e-1] == ' ' || keep[e-1] == '\t' ) ) --e;
		while ( b < e && ( keep[b] == ' ' || keep[b] == '\t' ) ) ++b;
		if ( e <= b ) continue;
		lb[nb] = b; le[nb] = e; ++nb;
	}
	if ( nb <= 0 )
		return thNULL;

	/* --- ② 同一行を数える (初出だけ出す) --- */
	int w = 0;
	if ( truncated ) {
		const char *pre = "...(truncated) ";
		for ( const char *q = pre ; *q && w < (int)sizeof flat - 1 ; ++q ) flat[w++] = *q;
	}
	for ( int i = 0 ; i < nb ; ++i ) {
		int len = le[i] - lb[i];
		int first = 1, count = 1;
		for ( int j = 0 ; j < i ; ++j ) {          /* 既に出したか */
			if ( le[j] - lb[j] == len && ::memcmp(keep + lb[j], keep + lb[i], (size_t)len) == 0 ) {
				first = 0; break;
			}
		}
		if ( ! first ) continue;
		for ( int j = i + 1 ; j < nb ; ++j ) {     /* 以降の同一行を数える */
			if ( le[j] - lb[j] == len && ::memcmp(keep + lb[j], keep + lb[i], (size_t)len) == 0 )
				++count;
		}
		if ( w > 0 && w < (int)sizeof flat - 4 ) { flat[w++] = ' '; flat[w++] = '|'; flat[w++] = ' '; }
		for ( int k = 0 ; k < len && w < (int)sizeof flat - 1 ; ++k ) {
			unsigned char c = (unsigned char)keep[lb[i]+k];
			flat[w++] = ( c < 0x20 ) ? ' ' : (char)c;
		}
		if ( count > 1 ) {
			char cnt[24];
			::snprintf(cnt, sizeof cnt, " (x%d)", count);
			for ( const char *q = cnt ; *q && w < (int)sizeof flat - 1 ; ++q ) flat[w++] = *q;
		}
	}
	flat[w] = '\0';
	return ( w > 0 ) ? sPtr<stdString>(thNEW(stdString,(flat))) : sPtr<stdString>();
}


/*******************************************
	STATE MACHINE
********************************************/

TS_STATE(INI_START)
{
	listen(parent,TSE_DESTROY);
	return rDO|ACT_PREV;
}

TS_STATE(ACT_PREV)
{
	if ( io == thNULL )
		return rDO|FIN_START;
	if ( C_TEST(io->tinyState::state(),C_ZOM|C_FIN) )
		return rDO|FIN_START;
	io->listen(parent,TSE_DESTROY);
	return rDO|ACT_START;
}

TS_STATE(ACT_START)
{
	if ( is_destroyed() )
		return rDO|FIN_START;
	int er = io->read(buffer,ERRSINK_READ);
	if ( er <= 0 )                      /* EOF / エラー = 子の stderr は閉じた */
		return rDO|FIN_START;
	append(buffer, er);
	return rDO|ACT_START;
}

TS_STATE(FIN_START)
{
	/* ★ io は destroy するが **keep は残す**。親 (mediator) はこの後で text() を取りに来る。 */
	if ( io.is_notNull() )
		io->destroy();
	io = thNULL;
	return rDO|FIN_TINYSTATE_START;
}
