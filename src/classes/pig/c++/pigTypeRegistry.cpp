/*
 * pigTypeRegistry — 実装 (rev4 Phase A)。
 * ★ #3427 ③: 可変 static テーブルを廃し値クラス化 (テーブルはメンバ types_v)。
 *   実体は pigModuleRegistry (ハブ) が所有する。設計: pigTypeRegistry.h / docs/agent_so_design.md §9。
 */
#include "pig/c++/pigTypeRegistry.h"

#include <cstring>

int
pigTypeRegistry::register_type(const char *name, const char *tag4)
{
  if (name == 0) return -1;
  int e = id_of_type(name);
  if (e >= 0) {                             /* 既存名は既存 id (冪等)。tag は後勝ちで更新 */
    if (tag4) { ::memcpy(types_v[(size_t)e].tag, tag4, 4); types_v[(size_t)e].has_tag = true; }
    return e;
  }
  TypeEnt t;
  t.name = name;
  t.has_tag = (tag4 != 0);
  if (tag4) ::memcpy(t.tag, tag4, 4); else ::memset(t.tag, 0, 4);
  types_v.push_back(t);
  return (int)types_v.size() - 1;
}

int
pigTypeRegistry::id_of_type(const char *name) const
{
  if (name == 0) return -1;
  for (size_t i = 0; i < types_v.size(); ++i)
    if (types_v[i].name == name) return (int)i;
  return -1;
}

const char *
pigTypeRegistry::name_of_type_id(int id) const
{
  if (id < 0 || (size_t)id >= types_v.size()) return 0;
  return types_v[(size_t)id].name.c_str();
}

int
pigTypeRegistry::type_count() const
{
  return (int)types_v.size();
}

const char *
pigTypeRegistry::type_of_tag(const unsigned char tag[4]) const
{
  for (size_t i = 0; i < types_v.size(); ++i)
    if (types_v[i].has_tag && ::memcmp(types_v[i].tag, tag, 4) == 0) return types_v[i].name.c_str();
  return 0;   /* 未登録タグ */
}

int
pigTypeRegistry::tag_of_type(const char *name, unsigned char out[4]) const
{
  int id = id_of_type(name);
  if (id < 0) return 0;
  if (!types_v[(size_t)id].has_tag) return 0;
  ::memcpy(out, types_v[(size_t)id].tag, 4);
  return 1;
}
