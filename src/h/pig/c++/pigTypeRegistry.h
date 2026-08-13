#ifndef ___pigTypeRegistry_H___
#define ___pigTypeRegistry_H___
/*
 * pigTypeRegistry — 型軸レジストリ (rev4 Phase A・docs/agent_so_design.md §9)。
 *   実装型名 ("cg-mesh3d" / "mf-mesh3d" / ...) ↔ キャッシュ 4CC タグ ("MESH" / "MFM3" / ...)。
 *   routing (decide_executor) と継続スタンプ (型リスト) の一次キー。
 *
 * ★ #3427 ③: 旧 namespace + 可変 static テーブルを廃し、**素の値クラス**にした。
 *   実体は pigModuleRegistry (ハブ) が直接メンバ `types` として所有し、ハブは
 *   ptsApplication が INI で thNEW する = プロセス全体の可変 static が消えリエントラント。
 *   登録は pigModuleRegistry::register_descriptor (記述子駆動・#3427 ①) の 1 経路。
 * 同一 tag の再登録は後勝ち。
 */
#include <string>
#include <vector>

class pigTypeRegistry {
public:
	/* 型名を登録 (tag4 = 4CC・0 可)。既存名は既存 id (冪等)・tag は後勝ちで更新。戻り = 型 id。 */
	int         register_type(const char *name, const char *tag4);
	/* 型名 → id。未登録 = -1。 */
	int         id_of_type(const char *name) const;
	/* id → 型名。範囲外 = 0。 */
	const char* name_of_type_id(int id) const;
	/* 登録済み型数。 */
	int         type_count() const;
	/* 4CC → 型名。未登録タグ = 0。 */
	const char* type_of_tag(const unsigned char tag[4]) const;
	/* 型名 → 4CC (out[4] へ)。無い/無タグ = 0、取れたら 1。 */
	int         tag_of_type(const char *name, unsigned char out[4]) const;

private:
	/* 実装型: 型名 + 4CC タグ (tag が空の型もありうるので has_tag を持つ)。登録順 = id (0 起点)。 */
	struct TypeEnt { std::string name; char tag[4]; bool has_tag; };
	std::vector<TypeEnt> types_v;
};

#endif
