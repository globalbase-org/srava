/*
 * pigfPluginAgent — プラグイン op 用の薄い agent ノード(pig 層)。pigfAgent を継承し:
 *   - agent_cmd()       : pig プラグインレジストリで op→bin を解決して起動コマンドを作る。
 *   - parse_value_text(): pig 値パーサ(pig_value_parse・同期)で結果を構造化 pigData に復元。
 *   - try_shortcircuit() / make_value_parser() は override しない(={} 単位元短絡も srava 言語パーサも
 *     使わない)。→ srava も CGAL も知らない純 pig の agent。
 *
 * srava の parser(mk_call)が「登録済みプラグイン op」を見つけたとき pigDataFunction<pigfPluginAgent>
 * ノードを作る。これにより本体(プラグインバイナリ)が pigwire で計算する。
 */
#include	"pig/c++/pigfAgent.h"
#include	"pig/c++/pigData.h"
#include	"pig/c++/pigValueCodec.h"      /* parse_value_text の同期パーサ */
#include	"pig/c++/pigPluginRegistry.h"  /* op→bin */
#include	"ts2/c++/stdString.h"
/* 基底 pigfAgent_ の sPtr<不完全型> メンバ(ts2System/ptsWirePipe/ts2Parallel/reader/ts2IO)を
 * 派生のデストラクタ実体化で扱うため完全型を取り込む(pigfSravaAgent と同じ作法)。 */
#include	"pig/c++/ptsWirePipe.h"
#include	"pig/c++/ptsWireCacheStreamReaderText.h"
#include	"ts2/c++/ts2System.h"
#include	"ts2/c++/ts2Parallel.h"
#include	"ts2/c++/ts2IO.h"
#include	"_ts2/c++/pigfPluginAgent_.h"

#include	<stdlib.h>   /* getenv */
#include	<stdio.h>    /* snprintf */
#include	<string.h>   /* strrchr */

CLASS_TINYSTATE(pig/c++/pigfPluginAgent,pig/c++/pigfAgent)

#if 0

TS_BEGIN_IMPLEMENT

class TS_THISCLASS : public TS_BASECLASS {
public:
	pigfPluginAgent_(
		sPtr<ptsObject> parent,
		sPtr<pigDataOperator> _front);

	sRptr<ptsObject,tinyState>		parent;
protected:
	virtual sPtr<stdString>	agent_cmd();
	virtual sPtr<pigData>	parse_value_text(sPtr<stdString> text);
private:
	TS_DEFARGS
};

TS_END_IMPLEMENT

TS_BEGIN_INTERFACE
#include	"ts2/c++/sRptr.h"
class ptsObject;
class pigDataOperator;
class stdString;
class pigData;
TS_END_INTERFACE

#endif


pigfPluginAgent_::pigfPluginAgent_(TS_ARGS0)
        : pigfAgent_(parent,_front),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

/* 登録済みプラグイン op の起動バイナリを起動コマンドに。op/file/line を **引数**として付ける
 * (agentwatch 表示用・agent は無視)。ファイル名は sh -c 安全文字に正規化(pigfSravaAgent と同様)。 */
sPtr<stdString>
pigfPluginAgent_::agent_cmd()
{
	sPtr<stdString> opn = ( front.is_notNull() ) ? front->get_op_name() : sPtr<stdString>();
	const char *op = ( opn.is_notNull() ) ? opn->get_str() : "op";
	const char *bin = pigplugin_op_bin(op);
	if ( bin == 0 )
		return thNULL;   /* 未登録 → 基底の「no agent command」エラーに落ちる */

	int line = ( front.is_notNull() && front->get_info().is_notNull() )
	         ? front->get_info()->get_lineno() : 0;
	const char *fnsrc = "-";
	if ( front.is_notNull() && front->get_info().is_notNull()
	     && front->get_info()->get_filename().is_notNull() ) {
		fnsrc = front->get_info()->get_filename()->get_str();
		const char *slash = ::strrchr(fnsrc, '/');
		if ( slash != 0 ) fnsrc = slash + 1;
	}
	char fn[128];
	int k = 0;
	for ( const char *q = fnsrc ; *q && k < (int)sizeof(fn) - 1 ; ++q ) {
		char c = *q;
		int ok = ( (c>='A'&&c<='Z') || (c>='a'&&c<='z') || (c>='0'&&c<='9') || c=='.' || c=='_' || c=='-' );
		fn[k++] = ok ? c : '_';
	}
	if ( k == 0 ) fn[k++] = '-';
	fn[k] = 0;

	char buf[1100];
#ifdef _WIN32
	/* MinGW ts2System は '#' 直接 exec のみ対応(sh -c 不可)。Windows は '#' 前置。bin は空白なし前提。 */
	::snprintf(buf, sizeof buf, "#%s %s %s %d", bin, op, fn, line);
#else
	::snprintf(buf, sizeof buf, "%s %s %s %d", bin, op, fn, line);
#endif
	return thNEW(stdString,(buf));
}

/* 結果テキスト → 構造化 pigData(pig 値パーサ・同期)。malformed は pigDataError(基底が ERROR 伝播)。 */
sPtr<pigData>
pigfPluginAgent_::parse_value_text(sPtr<stdString> text)
{
	const char *t = ( text != thNULL ) ? text->get_str() : "";
	return pig_value_parse(t);
}
