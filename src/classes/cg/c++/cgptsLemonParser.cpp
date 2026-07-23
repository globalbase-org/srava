/*
 * cgptsLemonParser — srava 文法のレキサ + lemon 生成パーサ駆動(ptsObject 派生 tinyState)。
 * ctor(parent, source) で文字列ソースを受け取り、INI で ParseAlloc → 手書き get_token で
 * トークン化して ns_sravaParser::Parse に流し込み、EOF(tid=0)で確定。文法アクションが
 * pigData ツリーを構築し parseAccept で result に格納。FIN で ParseFree → 親へ TSE_RETURN(tree)。
 * 構文エラーは parseFailure が result に pigDataError を立て、それを返す。
 *
 * トークン定数は生成ヘッダ ns_sravaParser.h(VAR/IDENT/INT/.../OR3/AND3/SUB3 等)。
 */
#include	"pig/c++/ptsObject.h"
#include	"pig/c++/pigData.h"
#include	"ts2/c++/stdEvent.h"
#include	"ns_sravaParser.h"          /* トークン #define(生成物。build/gen を include path に) */
#include	"_ts2/c++/cgptsLemonParser_.h"

#include	<stdlib.h>
#include	<string.h>
#include	<stdio.h>     /* include: ファイル読み込み */
#include	<limits.h>    /* PATH_MAX */
#include	"pig/c++/osglue.h"   /* osglue_realpath(MinGW 対応) */

CLASS_TINYSTATE(cg/c++/cgptsLemonParser,pig/c++/ptsObject)

/* 生成パーサの公開関数(namespace ns_sravaParser に置かれる)。 */
namespace ns_sravaParser {
	void * ParseAlloc(void *(*mallocProc)(size_t));
	void   ParseFree(void *p, void (*freeProc)(void*));
	void   Parse(void *yyp, int yymajor, sPtr<pigData> yyminor, sPtr<cgptsLemonParser>);
};

static void *c_malloc(size_t size)
{
	void *ret = ::malloc(size);
	if ( ret )
		::memset(ret, 0, size);
	return ret;
}


#if 0

TS_BEGIN_IMPLEMENT


class TS_THISCLASS : public TS_BASECLASS {
public:
	cgptsLemonParser_(
		sPtr<ptsObject> parent,
		sPtr<stdString> _source,
		int _mode,                      /* 0=PROGRAM(ソース) / 1=VALUE(値リテラル) */
		sPtr<stdString> _srcName);      /* エラー表示用ファイル名(NULL なら "<source>")。呼び元が渡す */

	sRptr<ptsObject,tinyState>		parent;

	void			parseAccept(sPtr<pigData> r);   /* 文法アクションから(成功) */
	void			parseFailure();                 /* 文法アクションから(構文エラー) */
protected:
	void *			parser;
	int			mode;        /* 0=PROGRAM / 1=VALUE。先頭にセンチネルを注入 */
	int			started;     /* センチネル注入済みか */
	sPtr<stdString>		input;
	const char *		input_p;
	const char *		tok_start;   /* 現トークンの先頭(構文エラー表示用)。get_token が毎回セット */
	sPtr<pigData>		result;
	sPtr<pigData>		dd;
	int			tid;
	int			line;
	sPtr<stdString>		srcName;     /* 現在レキシング中のファイル名(pigInfo に刻む。include で切替わる) */
	unsigned		parse_flag : 1;
	/* ---- include(字句インクルード)のバッファスタック ---- */
	sPtr<stdString>		cur_buf;        /* 現在レキシング中のバッファ実体(parseFailure の行抽出用) */
	int			incDepth;       /* インクルードのネスト深さ */
	const char *		incP[16];       /* 各段の親 resume 位置(input_p) */
	int			incLine[16];    /* 各段の親 line */
	sPtr<stdString>		incName[16];    /* 各段の親 srcName */
	sPtr<stdString>		incBuf[16];     /* 各段の親 cur_buf(実体保持) */
	sPtr<stdString>		incDone[128];   /* 一度 include した正規化パス(多重 include 防止) */
	int			incDoneN;
	sPtr<pigData>		get_token(int *ptid);
	sPtr<stdString>		read_file(const char *path);
	sPtr<pigData>		do_include(const char *path);   /* 解決+once+push。null=続行/skip、error=失敗 */
	sPtr<pigInfo>		tok_info() { return thNEW(pigInfo,(srcName, line)); }   /* 現トークン位置 */
private:
	TS_DEFARGS
};

TS_END_IMPLEMENT

TS_BEGIN_INTERFACE
#include	"ts2/c++/sRptr.h"
#include	"ts2/c++/stdString.h"
class ptsObject;
class pigData;
class pigInfo;
class stdString;
TS_END_INTERFACE

#endif


cgptsLemonParser_::cgptsLemonParser_(TS_ARGS0)
        : ptsObject_(parent),
	  parent(tinyState_::parent)
{
    TS_CPARGS0
    input      = _source;
    input_p    = 0;
    tok_start  = 0;
    parser     = 0;
    result     = thNULL;
    tid        = 0;
    line       = 1;
    parse_flag = 0;
    mode       = _mode;
    started    = 0;
    srcName    = _srcName.is_notNull() ? _srcName : sPtr<stdString>(thNEW(stdString,("<source>")));
    cur_buf    = _source;
    incDepth   = 0;
    incDoneN   = 0;
}


/*******************************************
	INSTANCE FUNCTIONS
********************************************/

void
cgptsLemonParser_::parseAccept(sPtr<pigData> r)
{
	result = r;
}

void
cgptsLemonParser_::parseFailure()
{
	/* 構文エラーを「どのトークンで・どの行で」分かる形にする。
	 * 該当トークン [tok_start, input_p) と、その行(前後の改行で切る)+ キャレットを出す。 */
	const char *src = cur_buf.is_notNull() ? cur_buf->get_str() : "";   /* 現在のバッファ(include 中は子) */
	const char *ts  = ( tok_start != 0 ) ? tok_start : input_p;
	const char *te  = ( input_p != 0 && ts != 0 && input_p > ts ) ? input_p : ts;

	char tok[64];
	int tlen = ( ts != 0 ) ? (int)(te - ts) : 0;
	if ( tlen <= 0 ) ::snprintf(tok, sizeof tok, "<EOF>");
	else { if ( tlen > (int)sizeof(tok)-1 ) tlen = (int)sizeof(tok)-1;
	       ::memcpy(tok, ts, tlen); tok[tlen] = 0; }

	/* 該当行 [ls, le) を切り出す(ts を含む行)。 */
	char line_buf[200]; line_buf[0] = 0;
	int col = 0;
	if ( ts != 0 && src != 0 && ts >= src ) {
		const char *ls = ts;
		while ( ls > src && ls[-1] != '\n' ) --ls;
		const char *le = ts;
		while ( *le && *le != '\n' ) ++le;
		int llen = (int)(le - ls);
		if ( llen > (int)sizeof(line_buf)-1 ) llen = (int)sizeof(line_buf)-1;
		::memcpy(line_buf, ls, llen); line_buf[llen] = 0;
		col = (int)(ts - ls);
		if ( col > llen ) col = llen;
	}

	char msg[400];
	if ( line_buf[0] )
		::snprintf(msg, sizeof msg, "parse error near '%s'\n    %s\n    %*s^", tok, line_buf, col, "");
	else
		::snprintf(msg, sizeof msg, "parse error near '%s'", tok);
	result = thNEW(pigDataError,(thNEW(stdString,(msg)), tok_info()));
}

/* ファイル全体を stdString に読む。失敗は null。 */
sPtr<stdString>
cgptsLemonParser_::read_file(const char *path)
{
	FILE *f = ::fopen(path, "rb");
	if ( ! f ) return thNULL;
	::fseek(f, 0, SEEK_END);
	long n = ::ftell(f);
	::fseek(f, 0, SEEK_SET);
	if ( n < 0 ) { ::fclose(f); return thNULL; }
	char *buf = (char*)::malloc((size_t)n + 1);
	if ( ! buf ) { ::fclose(f); return thNULL; }
	size_t got = ::fread(buf, 1, (size_t)n, f);
	buf[got] = 0;
	::fclose(f);
	sPtr<stdString> s = thNEW(stdString,(buf));
	::free(buf);
	return s;
}

/* include "path" の処理: パス解決 → 多重 include 防止 → 親状態 push して子バッファへ切替。
 * 候補: ①インクルード元ファイルの dir 相対 ②$SRAVA_PATH の各 dir ③path そのまま(絶対/cwd 相対)。
 * 戻り: null=続行(push 済 or once でスキップ) / pigDataError=失敗。 */
sPtr<pigData>
cgptsLemonParser_::do_include(const char *path)
{
	if ( incDepth >= 16 )
		return thNEW(pigDataError,(thNEW(stdString,("include: nesting too deep (cycle?)")), tok_info()));
	char cand[PATH_MAX];
	char canon[PATH_MAX];
	const char *found = 0;
	/* ① インクルード元 dir 相対 */
	if ( path[0] != '/' ) {
		const char *cur = srcName->get_str();
		const char *slash = ::strrchr(cur, '/');
		if ( slash ) {
			int dl = (int)(slash - cur) + 1;
			if ( dl + (int)::strlen(path) < PATH_MAX ) {
				::memcpy(cand, cur, dl); ::strcpy(cand + dl, path);
				if ( osglue_realpath(cand, canon) ) found = canon;
			}
		}
	}
	/* ② $SRAVA_PATH(コロン区切り) */
	if ( ! found && path[0] != '/' ) {
		const char *cp = ::getenv("SRAVA_PATH");
		while ( cp && *cp ) {
			const char *colon = ::strchr(cp, ':');
			int dl = colon ? (int)(colon - cp) : (int)::strlen(cp);
			if ( dl > 0 && dl + 1 + (int)::strlen(path) < PATH_MAX ) {
				::memcpy(cand, cp, dl); cand[dl] = '/'; ::strcpy(cand + dl + 1, path);
				if ( osglue_realpath(cand, canon) ) { found = canon; break; }
			}
			cp = colon ? colon + 1 : 0;
		}
	}
	/* ②.5 コンパイル時既定 lib dir($PREFIX/share/srava/lib)。インストール済み stdlib を環境変数なしで。 */
#ifdef SRAVA_LIBDIR
	if ( ! found && path[0] != '/' ) {
		if ( (int)::strlen(SRAVA_LIBDIR) + 1 + (int)::strlen(path) < PATH_MAX ) {
			::snprintf(cand, sizeof cand, "%s/%s", SRAVA_LIBDIR, path);
			if ( osglue_realpath(cand, canon) ) found = canon;
		}
	}
#endif
	/* ③ そのまま(絶対 or cwd 相対) */
	if ( ! found && osglue_realpath(path, canon) )
		found = canon;
	if ( ! found ) {
		char msg[PATH_MAX + 64];
		::snprintf(msg, sizeof msg, "include: cannot find \"%s\"", path);
		return thNEW(pigDataError,(thNEW(stdString,(msg)), tok_info()));
	}
	/* once-guard: 既に include 済みならスキップ(続行) */
	for ( int i = 0 ; i < incDoneN ; ++i )
		if ( incDone[i]->cmp(found) == 0 )
			return thNULL;
	sPtr<stdString> contents = read_file(found);
	if ( contents == thNULL ) {
		char msg[PATH_MAX + 64];
		::snprintf(msg, sizeof msg, "include: cannot read \"%s\"", found);
		return thNEW(pigDataError,(thNEW(stdString,(msg)), tok_info()));
	}
	if ( incDoneN < 128 )
		incDone[incDoneN++] = thNEW(stdString,(found));
	/* 親を push して子へ切替 */
	incP[incDepth]    = input_p;
	incLine[incDepth] = line;
	incName[incDepth] = srcName;
	incBuf[incDepth]  = cur_buf;
	incDepth ++;
	cur_buf = contents;
	input_p = contents->get_str();
	line    = 1;
	srcName = thNEW(stdString,(found));
	return thNULL;
}

/* 手書きレキサ。input_p を進めながら 1 トークン返す。*ptid = トークン定数(EOF=0)。
 * 値を持つトークン(IDENT/INT/FLOAT)は対応する pigData を返す。それ以外は thNULL。 */
sPtr<pigData>
cgptsLemonParser_::get_token(int *ptid)
{
	/* 先頭に 1 度だけモードセンチネルを注入(文法の 2 入口を選ぶ)。 */
	if ( ! started ) {
		started = 1;
		*ptid = ( mode == 1 ) ? MODE_VALUE : MODE_PROGRAM;
		return thNULL;
	}
	const char *p = input_p;
	/* 空白・改行・行コメント(//)を読み飛ばす。 */
	for ( ;; ) {
		while ( *p == ' ' || *p == '\t' || *p == '\r' )
			p++;
		if ( *p == '\n' ) { line++; p++; continue; }
		if ( p[0] == '/' && p[1] == '/' ) {
			while ( *p && *p != '\n' ) p++;
			continue;
		}
		/* シェバング行 (#!/path/to/srava) を読み飛ばす。スクリプト実行 (./file.sra) 用。
		 * `#` は他に用途がないので行末まで丸ごとコメント扱い(通常は先頭行のみに現れる)。 */
		if ( p[0] == '#' && p[1] == '!' ) {
			while ( *p && *p != '\n' ) p++;
			continue;
		}
		break;
	}
	tok_start = p;   /* トークン先頭(構文エラーで「near '...'」表示・行/キャレット起点に使う) */
	if ( *p == 0 ) {
		if ( incDepth > 0 ) {            /* include 末尾 → 親バッファに戻って続行 */
			incDepth --;
			input_p = incP[incDepth];
			line    = incLine[incDepth];
			srcName = incName[incDepth];
			cur_buf = incBuf[incDepth];
			incName[incDepth] = thNULL; incBuf[incDepth] = thNULL;
			return get_token(ptid);
		}
		input_p = p; *ptid = 0; return thNULL;
	}

	/* 3 文字 mesh 演算子(単文字 -/&/| より先に判定)。
	 * ★ 値として pigDataNull(tok_info) を運ぶ(演算子の行位置)。文法側でこれを束ねて二項 mesh op ノードの
	 *   info にする → エラー表示が「演算子の行」を指す(被演算子の中身行でなく)。他トークンは従来通り thNULL。 */
	if ( p[0]=='|' && p[1]=='|' && p[2]=='|' ) { input_p = p+3; *ptid = OR3;  return thNEW(pigDataNull,(tok_info())); }
	if ( p[0]=='&' && p[1]=='&' && p[2]=='&' ) { input_p = p+3; *ptid = AND3; return thNEW(pigDataNull,(tok_info())); }
	if ( p[0]=='-' && p[1]=='-' && p[2]=='-' ) { input_p = p+3; *ptid = SUB3; return thNEW(pigDataNull,(tok_info())); }
	if ( p[0]=='+' && p[1]=='+' && p[2]=='+' ) { input_p = p+3; *ptid = COMB3; return thNEW(pigDataNull,(tok_info())); }  /* +++ = combine */
	/* transform 系シュガー: >>>=translate(>= より先)、***=scale(単文字 * より先)。 */
	if ( p[0]=='>' && p[1]=='>' && p[2]=='>' ) { input_p = p+3; *ptid = XLATEOP; return thNULL; }
	if ( p[0]=='*' && p[1]=='*' && p[2]=='*' ) { input_p = p+3; *ptid = SCALEOP; return thNULL; }

	/* 2 文字論理演算子(3 文字 mesh |||/&&& の後に判定: 単文字 |/& は未使用なので 2 文字で確定)。 */
	if ( p[0]=='|' && p[1]=='|' ) { input_p = p+2; *ptid = OR2;  return thNULL; }   /* || = 論理 or */
	if ( p[0]=='&' && p[1]=='&' ) { input_p = p+2; *ptid = AND2; return thNULL; }   /* && = 論理 and */

	/* 2 文字比較演算子 + transform 系シュガー(単文字 =/</> より先に判定)。 */
	if ( p[0]=='=' && p[1]=='=' ) { input_p = p+2; *ptid = EQ; return thNULL; }
	if ( p[0]=='!' && p[1]=='=' ) { input_p = p+2; *ptid = NE; return thNULL; }
	if ( p[0]=='<' && p[1]=='=' ) { input_p = p+2; *ptid = LE; return thNULL; }
	if ( p[0]=='>' && p[1]=='=' ) { input_p = p+2; *ptid = GE; return thNULL; }
	if ( p[0]=='<' && p[1]=='>' ) { input_p = p+2; *ptid = MIRROROP; return thNULL; }  /* <> = mirror */

	switch ( *p ) {
	case '(': input_p = p+1; *ptid = LPAREN; return thNULL;
	case ')': input_p = p+1; *ptid = RPAREN; return thNULL;
	case '{': input_p = p+1; *ptid = LBRACE; return thNULL;
	case '}': input_p = p+1; *ptid = RBRACE; return thNULL;
	case '[': input_p = p+1; *ptid = LBRACK; return thNULL;
	case ']': input_p = p+1; *ptid = RBRACK; return thNULL;
	case ',': input_p = p+1; *ptid = COMMA;  return thNULL;
	case ';': input_p = p+1; *ptid = SEMI;   return thNULL;
	case ':': input_p = p+1; *ptid = COLON;  return thNULL;
	case '.': input_p = p+1; *ptid = DOT;    return thNULL;   /* 数値は下の数字スキャナが処理 */
	case '=': input_p = p+1; *ptid = ASSIGN; return thNULL;
	case '<': input_p = p+1; *ptid = LT;     return thNULL;
	case '>': input_p = p+1; *ptid = GT;     return thNULL;
	case '+': input_p = p+1; *ptid = PLUS;   return thNULL;
	case '-': input_p = p+1; *ptid = MINUS;  return thNULL;
	case '*': input_p = p+1; *ptid = STAR;   return thNULL;
	case '/': input_p = p+1; *ptid = SLASH;  return thNULL;
	case '\\': input_p = p+1; *ptid = LAMBDA; return thNULL;   /* \(a,b){...} lambda */
	case '@': input_p = p+1; *ptid = ATOP;   return thNULL;   /* m @(axis,d) = rotate */
	case '!': input_p = p+1; *ptid = BANG;   return thNULL;   /* 単項 ! = 論理否定(!= は上で処理済) */
	default:  break;
	}

	/* 文字列リテラル "..."(\n \t \" \\ のみ解釈)。 */
	if ( *p == '"' ) {
		++p;   /* 開き " をスキップ */
		char buf[1024];
		int k = 0;
		while ( *p && *p != '"' && k < (int)sizeof(buf)-1 ) {
			char c = *p++;
			if ( c == '\\' && *p ) {
				char e = *p++;
				c = ( e=='n' ) ? '\n' : ( e=='t' ) ? '\t' : e;   /* \" \\ は e そのもの */
			}
			buf[k++] = c;
		}
		buf[k] = 0;
		if ( *p == '"' ) p++;   /* 閉じ " */
		input_p = p;
		*ptid = STRING;
		return thNEW(pigDataString,(buf, tok_info()));
	}

	/* 数値(整数/実数)。指数表記 e/E も受ける(serialize の %.17g が小さい/大きい値で出す
	 * "3.06e-16" 等を round-trip するため。curve の座標は 0 近傍で指数が出る)。 */
	if ( *p >= '0' && *p <= '9' ) {
		const char *s = p;
		int isflt = 0;
		while ( *p >= '0' && *p <= '9' ) p++;
		if ( *p == '.' ) { isflt = 1; p++; while ( *p >= '0' && *p <= '9' ) p++; }
		/* 指数部 [eE][+-]?digits。e の後が数字(or 符号+数字)のときだけ消費(`3e` 等は識別子に渡さない)。 */
		if ( ( *p == 'e' || *p == 'E' ) &&
		     ( (p[1] >= '0' && p[1] <= '9') ||
		       ((p[1] == '+' || p[1] == '-') && p[2] >= '0' && p[2] <= '9') ) ) {
			isflt = 1; p++;
			if ( *p == '+' || *p == '-' ) p++;
			while ( *p >= '0' && *p <= '9' ) p++;
		}
		char buf[64];
		int n = (int)(p - s); if ( n > 63 ) n = 63;
		::memcpy(buf, s, n); buf[n] = 0;
		input_p = p;
		if ( isflt ) { *ptid = FLOAT; return thNEW(pigDataFloat,(::strtod(buf, 0), tok_info())); }
		*ptid = INT; return thNEW(pigDataInteger,((INTEGER64)::strtoll(buf, 0, 10), tok_info()));
	}

	/* 識別子 / キーワード var。 */
	if ( (*p>='a'&&*p<='z') || (*p>='A'&&*p<='Z') || *p=='_' ) {
		const char *s = p;
		while ( (*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||*p=='_' ) p++;
		char buf[256];
		int n = (int)(p - s); if ( n > 255 ) n = 255;
		::memcpy(buf, s, n); buf[n] = 0;
		input_p = p;
		/* include "path"; = 字句インクルード(C の #include 相当)。include の直後に文字列が来た時だけ
		 * ディレクティブ扱い(それ以外は通常の識別子。`var include=…` 等を壊さない)。トークンは生成せず
		 * 子バッファへ切替え、次トークンを返す(parser は include を見ない)。 */
		if ( ::strcmp(buf, "include") == 0 ) {
			const char *q = p;
			while ( *q==' '||*q=='\t'||*q=='\r'||*q=='\n' ) { if ( *q=='\n' ) line++; q++; }
			if ( *q == '"' ) {
				q++;
				char pathbuf[PATH_MAX]; int k = 0;
				while ( *q && *q != '"' && k < (int)sizeof(pathbuf)-1 ) {
					char c = *q++;
					if ( c=='\\' && *q ) { char e=*q++; c = (e=='n')?'\n':(e=='t')?'\t':e; }
					pathbuf[k++] = c;
				}
				pathbuf[k] = 0;
				if ( *q == '"' ) q++;
				while ( *q==' '||*q=='\t'||*q=='\r' ) q++;
				if ( *q == ';' ) q++;
				input_p = q;                                   /* ディレクティブを消費 */
				sPtr<pigData> e = do_include(pathbuf);
				if ( sPtr<pigDataError>::d_cast(e).is_notNull() )   /* include 失敗 = レキサエラー */
					return e;
				return get_token(ptid);                        /* 子バッファ or 親続きから */
			}
			/* 文字列が続かない → 通常の識別子 include(下へ fall-through) */
		}
		if ( ::strcmp(buf, "var")   == 0 ) { *ptid = VAR;   return thNULL; }
		if ( ::strcmp(buf, "if")    == 0 ) { *ptid = IF;    return thNULL; }
		if ( ::strcmp(buf, "else")  == 0 ) { *ptid = ELSE;  return thNULL; }
		if ( ::strcmp(buf, "while") == 0 ) { *ptid = WHILE; return thNULL; }
		if ( ::strcmp(buf, "for")   == 0 ) { *ptid = FOR;   return thNULL; }
		if ( ::strcmp(buf, "return")   == 0 ) { *ptid = RETURN;   return thNULL; }
		if ( ::strcmp(buf, "break")    == 0 ) { *ptid = BREAK;    return thNULL; }
		if ( ::strcmp(buf, "continue") == 0 ) { *ptid = CONTINUE; return thNULL; }
		if ( ::strcmp(buf, "exit")     == 0 ) { *ptid = EXIT;     return thNULL; }
		if ( ::strcmp(buf, "async")    == 0 ) { *ptid = ASYNC;    return thNULL; }
		if ( ::strcmp(buf, "sync")     == 0 ) { *ptid = SYNC;     return thNULL; }
		*ptid = IDENT; return thNEW(pigDataString,(buf, tok_info()));
	}

	/* 不正文字。 */
	input_p = p + 1;
	*ptid = 0;
	{
		char buf[64];
		::snprintf(buf, sizeof buf, "invalid character '%c'", *p);
		return thNEW(pigDataError,(thNEW(stdString,(buf)), tok_info()));
	}
}


/*******************************************
	STATE MACHINE
********************************************/

TS_STATE(INI_ptsObject_START)
{
	parser     = ns_sravaParser::ParseAlloc(c_malloc);
	parse_flag = 1;
	input_p    = ( input.is_notNull() ) ? input->get_str() : "";
	line       = 1;
	return rDO|ACT_cgptsLemonParser_STEP;
}

TS_STATE(ACT_cgptsLemonParser_STEP)   /* 同期 rDO ループ: 1 トークン取り Parse に送る */
{
	dd = get_token(&tid);
	if ( sPtr<pigDataError>::d_cast(dd).is_notNull() ) {   /* レキサエラー */
		result = dd;
		return rDO|FIN_START;
	}
	ns_sravaParser::Parse(parser, tid, dd, ifThis);
	if ( sPtr<pigDataError>::d_cast(result).is_notNull() )  /* 構文エラー(parseFailure) */
		return rDO|FIN_START;
	if ( tid == 0 )                                          /* EOF を送り program 確定 */
		return rDO|FIN_START;
	return rDO|ACT_cgptsLemonParser_STEP;
}

TS_STATE(FIN_START)
{
	if ( parse_flag ) {
		ns_sravaParser::ParseFree(parser, ::free);
		parse_flag = 0;
	}
	if ( result == thNULL )
		result = thNEW(pigDataError,(thNEW(stdString,("empty parse result"))));
	/* 親へ結果ツリー(または pigDataError)を返す。 */
	parent->eventHandler(thNEW(stdEvent,(TSE_RETURN, ifThis, result)));
	return rDO|FIN_ptsObject_START;
}
