/*
 * pigfSravaAgent — pigfAgent の srava 専用派生。状態機械は pigfAgent をそのまま継承し、
 * agent_cmd() のみ override して srava-agent(env SRAVA_AGENT で差し替え可)を供給する。
 *
 * 狙い(ひさレビュー 2026-06-05): pigfAgent は piggybackTurtle 汎用で特定 agent に非依存。
 * 「どの外部プロセスを起動するか」だけを薄い派生クラスに閉じ込めることで、将来 video 編集
 * agent / 巨大テクスチャ agent 等を別派生として足し、同一プランナ内で混在できるようにする。
 *
 * 使い方: pigDataFunction<pigfSravaAgent> ノードを作る(pigDataFunction<pigfAgent> の代わり)。
 */
#include	"pig/c++/pigfAgent.h"
#include	"pig/c++/pigData.h"
#include	"cg/c++/cgptsLemonParser.h"   /* srava VALUE パーサ(make_value_parser が生成・srava 言語固有) */
#include	"ts2/c++/stdString.h"
/* 基底 pigfAgent_ の sPtr<不完全型> メンバ(ts2System/ptsWirePipe/ts2Parallel/reader/ts2IO)を
 * 派生のデストラクタ実体化で扱うため、完全型を取り込む。 */
#include	"pig/c++/ptsWirePipe.h"
#include	"pig/c++/ptsWireCacheStreamReaderText.h"
#include	"ts2/c++/ts2System.h"
#include	"ts2/c++/ts2Parallel.h"
#include	"ts2/c++/ts2IO.h"
#include	"_ts2/c++/pigfSravaAgent_.h"

#include	<stdlib.h>   /* getenv */
#include	<stdio.h>    /* snprintf */
#include	<string.h>   /* strrchr(ファイル名の basename) */

CLASS_TINYSTATE(cg/c++/pigfSravaAgent,pig/c++/pigfAgent)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	pigfSravaAgent_(
		sPtr<ptsObject> parent,
		sPtr<pigDataOperator> _front);

	sRptr<ptsObject,tinyState>		parent;
protected:
	virtual sPtr<stdString>	agent_cmd();
	/* ★ pig/srava 境界フック(基底 pigfAgent の汎用フローから virtual で呼ばれる)。
	 *   try_shortcircuit: srava 演算子の単位元 {} 代数で CGAL を呼ばず畳む。
	 *   make_value_parser: srava 言語の VALUE パーサ(cgptsLemonParser)を子状態機械として作る。 */
	virtual int		try_shortcircuit();
	virtual sPtr<tinyState>	make_value_parser(sPtr<stdString> text);
private:
	TS_DEFARGS
};

TS_END_IMPLEMENT

TS_BEGIN_INTERFACE
#include	"ts2/c++/sRptr.h"
class ptsObject;
class pigDataOperator;
class stdString;
TS_END_INTERFACE

#endif


pigfSravaAgent_::pigfSravaAgent_(TS_ARGS0)
        : pigfAgent_(parent,_front),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

/* srava_agent を起動。テスト/差し替え用に env SRAVA_AGENT があればそれを優先。
 * 未定義なら install 先の既定 /usr/local/bin/srava_agent(cmake --install で配置)。 */
sPtr<stdString>
pigfSravaAgent_::agent_cmd()
{
	const char *cmd = ::getenv("SRAVA_AGENT");
	if ( cmd == 0 )
		cmd = "/usr/local/bin/srava_agent";
	/* 起動コマンドに op 名と元ソース行番号を **引数として** 付ける(agent は無視するが ps/top -c や
	 * agentwatch で「どの op がどの行から走っているか」が見えるようになる)。comm は "srava_agent"。
	 * ts2System は通常文字列を sh -c で起動する。
	 * NB: 先頭 '#'(直接 execvp)にすると sh 孫が消えてプロセス半減・kill 直達だが、起動が速くなり
	 *     "agent closed before handshake" のハンドシェイク race が間欠的に出た(ts2System の直接 exec 経路の
	 *     潜在不具合・sh -c は起動遅延で隠れていた)。安定優先で当面 sh -c のまま。根治は tinyState 側。 */
	const char *op = "op";
	sPtr<stdString> opn = ( front.is_notNull() ) ? front->get_op_name() : sPtr<stdString>();
	if ( opn.is_notNull() )
		op = opn->get_str();
	int line = ( front.is_notNull() && front->get_info().is_notNull() )
	         ? front->get_info()->get_lineno() : 0;
	/* 元ソースのファイル名(basename)も付ける(agentwatch で「演算名 ファイル名 行番号」表示用)。
	 * include されたファイルの op を区別できる。 */
	const char *fnsrc = "-";
	if ( front.is_notNull() && front->get_info().is_notNull()
	     && front->get_info()->get_filename().is_notNull() ) {
		fnsrc = front->get_info()->get_filename()->get_str();
		const char *slash = ::strrchr(fnsrc, '/');
		if ( slash != 0 ) fnsrc = slash + 1;   /* basename */
	}
	/* ★ ファイル名を **シェル安全文字 [A-Za-z0-9._-] だけ** に正規化(他は '_')。コマンドは sh -c で
	 *   起動されるので、env ソースの "<source>" のように '<' '>' を含むと **リダイレクトと誤解釈**され、
	 *   agent の stdin が pipe でなくなり "agent closed before handshake" で死ぬ(グロブ '?' '*' も同様)。
	 *   ps/agentwatch 表示用の飾りなので置換で十分(実 .sra 名は通常そのまま残る)。 */
	char fn[128];
	int k = 0;
	for ( const char *q = fnsrc ; *q && k < (int)sizeof(fn) - 1 ; ++q ) {
		char c = *q;
		int ok = ( (c>='A'&&c<='Z') || (c>='a'&&c<='z') || (c>='0'&&c<='9') || c=='.' || c=='_' || c=='-' );
		fn[k++] = ok ? c : '_';
	}
	if ( k == 0 ) fn[k++] = '-';
	fn[k] = 0;
	/* 引数の並び: op file line(line を末尾=数字にして agentwatch のパースを単純に保つ)。 */
	char buf[700];
#ifdef _WIN32
	/* Windows: ts2System の sh -c 経路が機能しない(native に sh が無い/CreateProcess の解釈)。
	 * 先頭 '#' で ts2System を **直接 exec(CreateProcess 直起動)** モードにする。以降は空白区切りで
	 * argv 化され argv[0]=agent パス(agent は残り引数を無視)。SRAVA_AGENT が空白を含まない前提。 */
	::snprintf(buf, sizeof buf, "#%s %s %s %d", cmd, op, fn, line);
#else
	::snprintf(buf, sizeof buf, "%s %s %s %d", cmd, op, fn, line);
#endif
	return thNEW(stdString,(buf));
}


/* fold 単位元 {}(空ハッシュ)判定。継続(mesh; car=="delayed")は car() 覗き見で非ブロッキングに弾く。
 * 空ハッシュ = 単位元(型分離: `{}`=単位元 / `[]`(配列)=コレクション)。 */
static int srava_is_identity(sPtr<pigData> a)
{
	if ( a->car()->get_str()->cmp("delayed") == 0 ) return 0;   /* mesh 継続 = 単位元でない */
	sPtr<pigDataHash> h = sPtr<pigDataHash>::d_cast(a->compact());
	return ( h.is_notNull() && h->length() == 0 ) ? 1 : 0;
}

/* ★ srava 演算子の代数的短絡(基底 pigfAgent::ACT_START の旧 1.5 から移設)。
 *   mesh ブール union/intersection/difference/combine の単位元 {}:
 *     a |||/&&&/+++ {} = a,  {} |||/&&&/+++ a = a,  a --- {} = a,  {} --- a = {}(差は左fold)。
 *   export({}) は実体化不能 → 明示エラー。値返し valid({})=0 / volume・area・perimeter({})=0。
 *   → CGAL を呼ばず畳めるので `var acc={}; for(..) acc = acc ||| x;` が書ける。
 *   戻り 0=非該当(agent 起動へ) / 1=front に結果セット済み / 2=err セット済み。 */
int
pigfSravaAgent_::try_shortcircuit()
{
	sPtr<stdString> opn = agent_op_name();
	const char *op = ( opn != thNULL ) ? opn->get_str() : "";
	int isbool = ( ::strcmp(op,"union")==0 || ::strcmp(op,"intersection")==0
	            || ::strcmp(op,"difference")==0 || ::strcmp(op,"combine")==0 );
	if ( isbool && args.length() == 2 ) {
		if ( srava_is_identity(args[1]) ) {            /* a op {} = a ({} op {} = {}) */
			front->set_result(args[0]);
			return 1;
		}
		if ( srava_is_identity(args[0]) ) {            /* {} op a */
			front->set_result( ::strcmp(op,"difference")==0 ? args[0]    /* {} --- a = {} */
			                                                : args[1] ); /* {} |||/&&&/+++ a = a */
			return 1;
		}
	}
	if ( ::strcmp(op,"export")==0 && args.length() >= 2 && srava_is_identity(args[1]) ) {
		err = thNEW(pigDataError,("export: empty mesh {} cannot be exported", front->get_info()));
		return 2;
	}
	/* 値返し op に {}: 空集合の自然な値で短絡(ガード `if(valid(acc)==1)` を書けるように)。 */
	if ( args.length() == 1 && srava_is_identity(args[0]) ) {
		if ( ::strcmp(op,"valid")==0 ) {
			front->set_result(thNEW(pigDataInteger,((INTEGER64)0)));
			return 1;
		}
		if ( ::strcmp(op,"volume")==0 || ::strcmp(op,"area")==0 || ::strcmp(op,"perimeter")==0 ) {
			front->set_result(thNEW(pigDataFloat,((double)0.0)));
			return 1;
		}
	}
	return 0;
}

/* ★ srava 言語の VALUE パーサ子(cgptsLemonParser・mode=1)。子は TSE_RETURN で構造化 pigData を返す
 *   (pig の約束)。別言語の agent なら別パーサをここで作る。 */
sPtr<tinyState>
pigfSravaAgent_::make_value_parser(sPtr<stdString> text)
{
	return thNEW(cgptsLemonParser,(ifThis, text, 1, thNULL));   /* 1=VALUE モード */
}
