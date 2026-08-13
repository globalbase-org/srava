/*
 * pigCacheCodec — キャッシュ本文の reader/writer テーブル実装 (#3406, 2026-07-29 メモ 3.)。
 * ★ #3427 ③: 可変 static テーブルを廃し値クラス化 (テーブルはメンバ entries_v)。
 *   実体は pigModuleRegistry (ハブ) が所有する。設計はヘッダ参照。
 */
#include "pig/c++/pigCacheCodec.h"
#include "pig/c++/pigData.h"
#include "pig/c++/pigTypeRegistry.h"   /* ★ P2 (⑤ 型変換): reader_for_tag の canonical 型を type_of_tag で引く */

#include <string.h>

namespace {

/* tags CSV でこの 4CC が何番目 (0 始まり) か。無ければ -1。 */
int tag_index(const std::string& tags, const unsigned char tag[4]) {
	const char *p = tags.c_str();
	int i = 0;
	while ( *p ) {
		if ( ::strncmp(p, (const char*)tag, 4) == 0 ) return i;
		const char *c = ::strchr(p, ',');
		if ( c == 0 ) break;
		p = c + 1; ++i;
	}
	return -1;
}

/* CSV の ix 番目のトークンを返す (無ければ空)。 */
std::string csv_at(const std::string& csv, int ix) {
	size_t p = 0; int i = 0;
	while ( true ) {
		size_t c = csv.find(',', p);
		size_t e = ( c == std::string::npos ) ? csv.size() : c;
		if ( i == ix ) return csv.substr(p, e - p);
		if ( c == std::string::npos ) break;
		p = c + 1; ++i;
	}
	return std::string();
}

}   /* anonymous namespace */

void
pigCacheCodec::register_codec(const char *name, const char *tags, const char *out_types,
                              pigCacheMatchFn match,
                              pigCacheReaderFn mkReader, pigCacheWriterFn mkWriter)
{
	if ( name == 0 || tags == 0 ) return;
	for ( size_t i = 0 ; i < entries_v.size() ; ++i )
		if ( entries_v[i].name == name )
			return;              /* 先勝ち (二重登録は無視・冪等) */
	Entry e; e.name = name; e.tags = tags; e.outTypes = ( out_types ? out_types : "" );
	e.match = match; e.mkReader = mkReader; e.mkWriter = mkWriter;
	entries_v.push_back(e);
}

/* ★ P2 (⑤ 型変換): file の 4CC を読めて出力型が target_type の reader を引く (2 キー選択)。
 *   planner が全モジュールの codec を同居させても、同じ 4CC を自型 (その 4CC の型を出す) と foreign
 *   (昇格して別型を出す) の複数 codec が主張しうるが、**出力型で一意に絞れる** (owner 不要)。 */
pigCacheReaderFn
pigCacheCodec::reader_for(const unsigned char file_tag[4], const char *target_type) const
{
	if ( target_type == 0 || target_type[0] == '\0' ) return 0;
	for ( size_t i = 0 ; i < entries_v.size() ; ++i ) {
		int ix = tag_index(entries_v[i].tags, file_tag);
		if ( ix < 0 ) continue;
		if ( csv_at(entries_v[i].outTypes, ix) == target_type )
			return entries_v[i].mkReader;
	}
	return 0;
}

/* ★ P2 (⑤ 型変換): 旧 owner 優先を型軸へ。get_body() 無指定 (自型読み) の canonical reader 選択。
 *   まず「そのタグの自型」(type_of_tag) を出す codec を選び (planner で自型優先を実現)、
 *   無ければ「そのタグを読める任意 codec」へフォールバック (= カーネル単独 agent で foreign 昇格読み
 *   codec しか無い場合や、REF 等の無型タグ)。旧 module_of_tag owner 優先と厳密に等価。 */
pigCacheReaderFn
pigCacheCodec::reader_for_tag(const unsigned char tag[4], const pigTypeRegistry &types) const
{
	const char *nt = types.type_of_tag(tag);               /* そのタグの自型 (無ければ 0) */
	if ( nt != 0 ) {
		pigCacheReaderFn r = reader_for(tag, nt);
		if ( r != 0 ) return r;                            /* 自型を出す codec を優先 */
	}
	for ( size_t i = 0 ; i < entries_v.size() ; ++i )      /* フォールバック: タグを読める任意 codec */
		if ( tag_index(entries_v[i].tags, tag) >= 0 )
			return entries_v[i].mkReader;
	return 0;
}

pigCacheWriterFn
pigCacheCodec::writer_for_body(sPtr<pigData> body) const
{
	if ( body == thNULL ) return 0;
	for ( size_t i = 0 ; i < entries_v.size() ; ++i )
		if ( entries_v[i].match != 0 && entries_v[i].match(body) )
			return entries_v[i].mkWriter;
	return 0;
}

int
pigCacheCodec::is_stream_body(sPtr<pigData> body) const
{
	return ( writer_for_body(body) != 0 ) ? 1 : 0;
}
