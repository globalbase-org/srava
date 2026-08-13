/*
 * pigValueParser — 値パーサ生成子レジストリ実装 (#3406, 2026-07-31 メモ 3.2)。
 * ★ #3427 ③: 可変 static スロットを廃し値クラス化 (スロットはメンバ)。
 *   実体は pigModuleRegistry (ハブ) が所有する。設計はヘッダ参照。
 */
#include "pig/c++/pigValueParser.h"
#include "pig/c++/pigValueCodec.h"   /* 既定の同期パーサ pig_value_parse */
#include "pig/c++/pigData.h"

void
pigValueParser::register_parser(const char *name, pigValueParserFn mk)
{
	if ( name == 0 || mk == 0 ) return;
	if ( mk_ != 0 ) return;          /* 先勝ち(二重登録は無視) */
	name_ = name;
	mk_   = mk;
}

pigValueParserFn
pigValueParser::get() const
{
	return mk_;
}

sPtr<pigData>
pigValueParser::parse_sync(sPtr<stdString> text)
{
	if ( text == thNULL )
		return thNEW(pigDataError,(thNEW(stdString,("value parse: null text"))));
	return pig_value_parse(text->get_str());
}
