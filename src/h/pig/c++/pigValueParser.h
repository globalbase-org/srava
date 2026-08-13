/*
 * pigValueParser — 「値テキスト → pigData」パーサの生成子レジストリ
 *                  (#3406, 2026-07-31 メモ 3.2 = ひさ選択「案 B」)。
 *
 * D_TEXT キャッシュの中身は素のテキストではなく **pigData のコード**(= body->serialize() の出力)。
 * よって読み側は必ずパーサを通して pigData に戻す必要がある。ところが復号器には 2 種類あり、
 * 層が違う:
 *
 *   - **同期・pig 層** = pig_value_parse() (pigValueCodec.h)。serialize() の出力サブセット
 *     (null/int/float/string/array/hash) を扱う。言語非依存でどこでも使える。
 *   - **非同期・srava 層** = cgptsLemonParser VALUE モード。srava の VALUE 文法(厳密有理数等)
 *     まで扱う上位互換だが、pig 層から名指しできない(言語依存・tinyState 子状態機械)。
 *
 * ★ #3427 ③: 旧 namespace + 可変 static スロットを廃し、**素の値クラス**にした。
 *   実体は pigModuleRegistry (ハブ) が直接メンバ `vparser` として所有する。
 *   登録は旧「cgptsLemonParser.cpp の静的初期化」を廃し、**言語パーサを持つ app の INI**
 *   (cgptsPlanner / テスト fixture) が明示的に呼ぶ。
 *
 * 使い分け(呼び側の規約):
 *   - get() != 0 なら**そちらを使う**(登録されている = その実行体は言語パーサを持っている =
 *     上位互換なので同期版を試す意味がない)。子状態機械なので TSE_RETURN(msg_obj=pigData) を待つ。
 *   - get() == 0 (agent プロセス等・言語パーサ非リンク) は parse_sync() を使う。
 */
#ifndef ___pigValueParser_H___
#define ___pigValueParser_H___

#include	"ts2/c++/sPtr.h"
#include	"ts2/c++/stdString.h"

#include	<string>

class tinyState;
class ptsObject;
class pigData;

/* 非同期パーサ生成子。parent = イベントの受け手。生成された子状態機械は
 * TSE_RETURN の msg_obj で pigData を返す約束(構文エラーは pigDataError)。 */
typedef sPtr<tinyState> (*pigValueParserFn)(sPtr<ptsObject> parent, sPtr<stdString> text);

class pigValueParser {
public:
	pigValueParser() : mk_(0) {}

	/* 言語パーサを 1 つ登録する(先勝ち = 二重登録は無視)。 */
	void register_parser(const char *name, pigValueParserFn mk);

	/* 登録済み生成子(無ければ 0 = parse_sync を使え、の意)。 */
	pigValueParserFn get() const;

	/* 既定の同期パーサ = pig_value_parse。text が thNULL なら pigDataError を返す。
	 * (状態を持たないので static。レジストリ未所持の文脈からも呼べる。) */
	static sPtr<pigData> parse_sync(sPtr<stdString> text);

private:
	/* 言語パーサは実行体につき 1 つ(srava なら cgptsLemonParser)なのでテーブルではなく単一スロット。 */
	std::string      name_;
	pigValueParserFn mk_;
};

#endif
