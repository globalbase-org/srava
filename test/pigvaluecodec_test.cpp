/* pigValueCodec 往復テスト: pigData → serialize() → pig_value_parse → 同値 を検証。
 * pig 値コーデック(プラグイン機構のデータ受け渡し)の健全性。 */
#include "pig/c++/pigData.h"
#include "pig/c++/pigValueCodec.h"
#include <stdio.h>
#include <string.h>

static int fails = 0;
#define CHECK(c) do { if (!(c)) { \
    printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static const char *ser(sPtr<pigData> v) { return v->serialize()->get_str(); }

/* v を serialize → parse し直し、再 serialize が一致するか(往復不変)。 */
static void roundtrip(sPtr<pigData> v) {
	sPtr<stdString> s1 = v->serialize();
	sPtr<pigData>   r  = pig_value_parse(s1->get_str());
	CHECK(! r->is_error());
	if ( r->is_error() ) { printf("  parse err for: %s\n", s1->get_str()); return; }
	const char *s2 = r->serialize()->get_str();
	CHECK(::strcmp(s1->get_str(), s2) == 0);
	if ( ::strcmp(s1->get_str(), s2) != 0 )
		printf("  roundtrip mismatch: '%s' -> '%s'\n", s1->get_str(), s2);
}

int main(void)
{
	/* --- スカラ --- */
	roundtrip(thNEW(pigDataInteger,((INTEGER64)42)));
	roundtrip(thNEW(pigDataInteger,((INTEGER64)-7)));
	roundtrip(thNEW(pigDataFloat,(3.14159)));
	roundtrip(thNEW(pigDataFloat,(-0.5)));
	roundtrip(thNEW(pigDataFloat,(1.5e10)));
	roundtrip(thNEW(pigDataNull,()));
	roundtrip(thNEW(pigDataString,("hello")));
	roundtrip(thNEW(pigDataString,("with \"quote\" and \\ and\nnewline\ttab")));

	/* --- int / float の区別が保たれる --- */
	CHECK(is_pigDataType(pigDataInteger, pig_value_parse("42")));
	CHECK(is_pigDataType(pigDataFloat,   pig_value_parse("42.0")));
	CHECK(is_pigDataType(pigDataFloat,   pig_value_parse("1e3")));
	CHECK(pig_value_parse("42")->get_int() == 42);
	CHECK(pig_value_parse("3.5")->get_flt() == 3.5);

	/* --- 配列(入れ子) = pipe_proximity の Contact 風 --- */
	sPtr<pigDataArray> contact = thNEW(pigDataArray,());
	contact->push(thNEW(pigDataFloat,(0.42)));                 /* gap */
	sPtr<pigDataArray> pA = thNEW(pigDataArray,());
	pA->push(thNEW(pigDataFloat,(1.0)));
	pA->push(thNEW(pigDataFloat,(2.0)));
	pA->push(thNEW(pigDataFloat,(3.0)));
	contact->push(pA);                                         /* [x,y,z] */
	sPtr<pigDataArray> contacts = thNEW(pigDataArray,());
	contacts->push(contact);
	roundtrip(contacts);
	/* 復元して添字アクセス */
	sPtr<pigData> rc = pig_value_parse(ser(contacts));
	CHECK(! rc->is_error());
	sPtr<pigData> g = rc->get_ix(thNEW(pigDataInteger,((INTEGER64)0)))
	                    ->get_ix(thNEW(pigDataInteger,((INTEGER64)0)));
	CHECK(g->get_flt() == 0.42);

	/* --- ハッシュ --- */
	sPtr<pigDataHash> h = thNEW(pigDataHash,());
	h->set_ix(thNEW(pigDataString,("n")),    thNEW(pigDataInteger,((INTEGER64)5)));
	h->set_ix(thNEW(pigDataString,("name")), thNEW(pigDataString,("part")));
	roundtrip(h);

	/* --- 空配列/空ハッシュ --- */
	roundtrip(thNEW(pigDataArray,()));
	roundtrip(thNEW(pigDataHash,()));

	/* --- malformed → エラー --- */
	CHECK(pig_value_parse("[1,2")->is_error());        /* 閉じ忘れ */
	CHECK(pig_value_parse("1 2")->is_error());         /* 余分トークン */
	CHECK(pig_value_parse("{1:2}")->is_error());       /* キー非文字列 */
	CHECK(pig_value_parse("")->is_error());            /* 空 */

	if ( fails == 0 ) printf("pigvaluecodec: ALL PASS\n");
	else              printf("pigvaluecodec: %d FAIL\n", fails);
	return fails ? 1 : 0;
}
