/*
 * pigValueCodec — pig 値パーサ(serialize() の対)。再帰下降。pig 層・srava 非依存。
 * 文法: value = null | int | float | string | array | hash
 *   int    : -?[0-9]+                       (小数点/指数を含まない)→ pigDataInteger
 *   float  : 上記 + '.'/'e'/'E' を含む数     → pigDataFloat (inf/nan も float 扱い)
 *   string : "..."(\\ \" \n \t をアンエスケープ)→ pigDataString
 *   array  : '[' (value (',' value)*)? ']'  → pigDataArray
 *   hash   : '{' (string ':' value (',' ...))? '}' → pigDataHash
 * 空白はスキップ。malformed は pigDataError。
 */
#include "pig/c++/pigValueCodec.h"
#include "ts2/c++/stdString.h"

#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <string>

namespace {

struct Parser {
	const char *p;
	int         err;   /* 1=構文エラー */

	Parser(const char *s) : p(s), err(0) {}

	void skip_ws() { while ( *p==' '||*p=='\t'||*p=='\n'||*p=='\r' ) ++p; }

	sPtr<pigData> fail() { err = 1; return sPtr<pigData>(); }

	sPtr<pigData> parse_value() {
		skip_ws();
		char c = *p;
		if ( c == 0 )   return fail();
		if ( c == '[' ) return parse_array();
		if ( c == '{' ) return parse_hash();
		if ( c == '"' ) return parse_string_node();
		if ( c=='-' || c=='+' || (c>='0'&&c<='9') || c=='.' ) return parse_number();
		if ( ::strncmp(p,"null",4)==0 )  { p += 4; return thNEW(pigDataNull,()); }
		if ( ::strncmp(p,"inf",3)==0 )   { p += 3; return thNEW(pigDataFloat,((double)HUGE_VAL)); }
		if ( ::strncmp(p,"-inf",4)==0 )  { p += 4; return thNEW(pigDataFloat,(-(double)HUGE_VAL)); }
		if ( ::strncmp(p,"nan",3)==0 )   { p += 3; return thNEW(pigDataFloat,((double)(0.0/0.0))); }
		return fail();
	}

	/* 数: 小数点/指数/inf/nan を含めば float、でなければ integer。 */
	sPtr<pigData> parse_number() {
		const char *s = p;
		int isFloat = 0;
		if ( *p=='-' || *p=='+' ) ++p;
		/* inf/nan(符号つき)も数として受ける */
		if ( ::strncmp(p,"inf",3)==0 || ::strncmp(p,"nan",3)==0 ) {
			p += 3;
			double dv = ::strtod(s, 0);
			return thNEW(pigDataFloat,(dv));
		}
		while ( (*p>='0'&&*p<='9') ) ++p;
		if ( *p=='.' ) { isFloat = 1; ++p; while ( *p>='0'&&*p<='9' ) ++p; }
		if ( *p=='e' || *p=='E' ) {
			isFloat = 1; ++p;
			if ( *p=='-' || *p=='+' ) ++p;
			while ( *p>='0'&&*p<='9' ) ++p;
		}
		if ( p == s || (p==s+1 && (s[0]=='-'||s[0]=='+')) ) return fail();   /* 数字なし */
		std::string tok(s, (size_t)(p - s));
		if ( isFloat )
			return thNEW(pigDataFloat,(::strtod(tok.c_str(), 0)));
		return thNEW(pigDataInteger,((INTEGER64)::strtoll(tok.c_str(), 0, 10)));
	}

	/* "..." を読み、アンエスケープした std::string を返す(err 時は空 + err=1)。 */
	std::string parse_string_raw() {
		std::string out;
		if ( *p != '"' ) { err = 1; return out; }
		++p;
		while ( *p && *p != '"' ) {
			if ( *p == '\\' ) {
				++p;
				switch ( *p ) {
					case '\\': out += '\\'; break;
					case '"':  out += '"';  break;
					case 'n':  out += '\n'; break;
					case 't':  out += '\t'; break;
					case 0:    err = 1; return out;
					default:   out += *p;   break;   /* 未知エスケープは素通し */
				}
				++p;
			} else {
				out += *p++;
			}
		}
		if ( *p != '"' ) { err = 1; return out; }
		++p;
		return out;
	}

	sPtr<pigData> parse_string_node() {
		std::string s = parse_string_raw();
		if ( err ) return sPtr<pigData>();
		return thNEW(pigDataString,(s.c_str()));
	}

	sPtr<pigData> parse_array() {
		++p;   /* '[' */
		sPtr<pigDataArray> arr = thNEW(pigDataArray,());
		skip_ws();
		if ( *p == ']' ) { ++p; return arr; }
		for ( ;; ) {
			sPtr<pigData> v = parse_value();
			if ( err ) return sPtr<pigData>();
			arr->push(v);
			skip_ws();
			if ( *p == ',' ) { ++p; continue; }
			if ( *p == ']' ) { ++p; break; }
			return fail();
		}
		return arr;
	}

	sPtr<pigData> parse_hash() {
		++p;   /* '{' */
		sPtr<pigDataHash> h = thNEW(pigDataHash,());
		skip_ws();
		if ( *p == '}' ) { ++p; return h; }
		for ( ;; ) {
			skip_ws();
			if ( *p != '"' ) return fail();   /* キーは必ず文字列 */
			std::string key = parse_string_raw();
			if ( err ) return sPtr<pigData>();
			skip_ws();
			if ( *p != ':' ) return fail();
			++p;
			sPtr<pigData> v = parse_value();
			if ( err ) return sPtr<pigData>();
			h->set_ix(thNEW(pigDataString,(key.c_str())), v);
			skip_ws();
			if ( *p == ',' ) { ++p; continue; }
			if ( *p == '}' ) { ++p; break; }
			return fail();
		}
		return h;
	}
};

} /* anonymous namespace */

sPtr<pigData>
pig_value_parse(const char *text)
{
	if ( text == 0 ) return thNEW(pigDataError,(thNEW(stdString,("pig_value_parse: null text"))));
	Parser ps(text);
	sPtr<pigData> v = ps.parse_value();
	if ( ps.err || ! v.is_notNull() )
		return thNEW(pigDataError,(thNEW(stdString,("pig_value_parse: malformed value"))));
	ps.skip_ws();
	if ( *ps.p != 0 )   /* 末尾に余分なトークン */
		return thNEW(pigDataError,(thNEW(stdString,("pig_value_parse: trailing characters"))));
	return v;
}
