/*
 * pigAgentRegistry — 「KERNEL 名 → ptsAgent 生成子」のテーブル実装 (#3406 段階 4.2)。
 * ★ #3427 ③: 可変 static テーブルを廃し値クラス化 (テーブルはメンバ entries_v)。
 *   実体は pigModuleRegistry (ハブ) が所有する。設計はヘッダコメント参照。
 */
#include "pig/c++/pigAgentRegistry.h"

void
pigAgentRegistry::register_agent(const char *module, pigAgentFactory f)
{
	if ( f == 0 ) return;
	std::string k = ( module != 0 ) ? module : "";
	for ( size_t i = 0 ; i < entries_v.size() ; ++i )
		if ( entries_v[i].module == k )
			return;              /* 先勝ち (二重登録は無視) */
	Entry e; e.module = k; e.fn = f;
	entries_v.push_back(e);
}

pigAgentFactory
pigAgentRegistry::lookup(const char *module) const
{
	if ( module == 0 || module[0] == 0 )
		return ( entries_v.size() == 1 ) ? entries_v[0].fn : 0;   /* 唯一の登録 (4.2 の agent バイナリ) */
	for ( size_t i = 0 ; i < entries_v.size() ; ++i )
		if ( entries_v[i].module == module )
			return entries_v[i].fn;
	return 0;
}

int
pigAgentRegistry::count() const
{
	return (int)entries_v.size();
}
