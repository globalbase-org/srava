/*
 * pigModuleRegistry — 実装 (ハブ・#3427 ③)。
 * 設計: pigModuleRegistry.h / docs/agent_so_design.md。
 * 旧「namespace + 可変 static テーブル」を廃し、ptsApplication が INI で thNEW する
 * stdObject 派生クラスにした (テーブルは全て直接メンバ)。
 * .so ロード (load_file / load_search_path) の実装は pigModuleLoader.cpp。
 * pig_current_registry() (TLS 経由の現行レジストリ) は ptsApplication.cpp。
 */
#include "pig/c++/pigModuleRegistry.h"
#include "pig/c++/pigModule.h"

#include <cstring>
#include <climits>

/* pigRefCacheCodec.cpp: D_REF codec の組込登録 (カーネル非依存・pig 層)。 */
extern void pigRefCacheCodec_register(pigCacheCodec &codecs);

namespace {

/* ★ #3427: "a,b,c" 形式の CSV を位置対応で組にし、型軸レジストリへ登録する。
 * types 側が空/短い場合はその位置を飛ばす (無型の codec = REF 等)。 */
void register_types_from_csv(pigTypeRegistry &tr, const char *types, const char *tags)
{
  if (types == 0 || tags == 0) return;
  const char *tp = types, *gp = tags;
  while (*tp != '\0' && *gp != '\0') {
    const char *tc = std::strchr(tp, ','), *gc = std::strchr(gp, ',');
    std::string t(tp, tc ? (size_t)(tc - tp) : std::strlen(tp));
    std::string g(gp, gc ? (size_t)(gc - gp) : std::strlen(gp));
    if (!t.empty() && !g.empty())
      tr.register_type(t.c_str(), g.c_str());
    if (!tc || !gc) break;
    tp = tc + 1; gp = gc + 1;
  }
}

/* CSV に ext (先頭 '.' 付き想定、大小無視) が含まれるか。
 * ★ rev4 Phase C: セグメントは "stl" (無型) でも "stl:cg-mesh3d" (型付き・import 用) でもよい。
 *   ext との照合は ':' より前 (拡張子部) のみで行う (型付きでも can_import_ext が従来通り効く)。 */
bool csv_has_ext(const char* csv, const char* ext) {
  if (csv == 0 || ext == 0) return false;
  if (*ext == '.') ++ext;                 /* 先頭ドットを外す */
  size_t el = ::strlen(ext);
  const char* p = csv;
  while (*p) {
    const char* c = ::strchr(p, ',');
    size_t seg = c ? (size_t)(c - p) : ::strlen(p);
    const char* colon = (const char*)::memchr(p, ':', seg);   /* "ext:type" の ':' */
    size_t extlen = colon ? (size_t)(colon - p) : seg;         /* 拡張子部だけで照合 */
    if (extlen == el && ::strncasecmp(p, ext, el) == 0) return true;
    if (!c) break;
    p = c + 1;
  }
  return false;
}

}   /* anonymous namespace */

/* ★ rev4 Phase D: 予約は **"delayed"(=id 0=MODULE_NONE 番兵) だけ**。cgal/manifold は
 * 記述子駆動で登録される。組込 codec (pig-ref) はここで登録 (旧: 静的初期化の自己登録)。
 * backends (thread/process) は pigExecBackend 自身の ctor が登録する。 */
pigModuleRegistry::pigModuleRegistry()
  : seqN_(0), pdcHelper_(0)
{
  names_v.push_back("delayed");
  pigRefCacheCodec_register(codecs);
}

int
pigModuleRegistry::register_module(const char *name)
{
  if (name == 0) return -1;
  int e = id_of_name(name);
  if (e >= 0) return e;                 /* 既存名は既存 id (冪等) */
  names_v.push_back(name);
  return (int)names_v.size() - 1;
}

int
pigModuleRegistry::id_of_name(const char *name) const
{
  if (name == 0) return -1;
  for (size_t i = 0; i < names_v.size(); ++i)
    if (names_v[i] == name) return (int)i;
  return -1;
}

const char *
pigModuleRegistry::name_of_id(int id) const
{
  if (id < 0 || (size_t)id >= names_v.size()) return names_v[0].c_str();   /* 範囲外 = "delayed" */
  return names_v[(size_t)id].c_str();
}

int
pigModuleRegistry::count() const
{
  return (int)names_v.size();
}

void
pigModuleRegistry::ensure_ovr(int id)
{
  if (id < 0) return;
  if ((size_t)id >= prioOvr_v.size()) prioOvr_v.resize(id + 1, INT_MIN);
  if ((size_t)id >= execOvr_v.size()) execOvr_v.resize(id + 1, -1);
  if ((size_t)id >= seq_v.size())     seq_v.resize(id + 1, 0);
}

void
pigModuleRegistry::register_descriptor(const srava_module_descriptor *d)
{
  if (d == 0 || d->name == 0) return;
  int id = register_module(d->name);       /* name→id (既存名は既存 id・後勝ちでメタ上書き) */
  if ((size_t)id >= descs_v.size()) descs_v.resize(id + 1, 0);
  descs_v[(size_t)id] = d;
  ensure_ovr(id);
  seq_v[(size_t)id] = ++seqN_;             /* ロード順 (後勝ちの tie-break) */

  /* ★ Phase 4③': descriptor.codecs を codec 表へ登録 (旧: 各 codec TU の静的初期化で自己登録)。
   * ★ P2 (⑤ 型変換): owner (module id) は渡さない。reader 選択は tags × out_types の型軸 2 キー
   *   (reader_for/reader_for_tag) で一意に絞れるため、どのモジュールが登録したかは不要になった。 */
  if (d->codecs != 0)
    for (const pigModuleCodec* c = d->codecs; c->name != 0; ++c)
      codecs.register_codec(c->name, c->tags, c->out_types, c->match, c->mkReader, c->mkWriter);

  /* ★ #3427: 以下 3 つは従来 **.so 側の静的初期化** が自分でグローバルへ書き込んでいた
   *   (mfatsAgent.cpp の register_agent / manifest.cpp の register_type・register_hash_salt)。
   *   .so は「誰のレジストリか」を知らず引数も取れないため、dlopen をいつ呼んでもプロセス全体の
   *   可変 static に書く形にしかならなかった。記述子は必要な情報を全部持っているので、登録を
   *   ここ 1 箇所に集約し、.so は「記述子を返すだけ」の受け身にする。
   *   これで登録経路が 1 本になり、#3425 の「記述子は後勝ち / 実行体・codec は先勝ち」という
   *   意味論の食い違いも構造的に起きなくなる。 */

  /* ① 実行体 (in-proc agent の生成子)。 */
  if (d->make_agent != 0)
    agents.register_agent(d->name, d->make_agent);

  /* ② 型軸レジストリ。記述子が **明示的に申告した** 型表を登録する。
   *   ⚠ codecs の out_types/tags からの導出は誤り: cgal の cg-mf-upgrade は MFM3/MFC2 を読んで
   *     cg-mesh3d/cg-cross2d を出す foreign 読み codec なので、導出すると
   *     register_type("cg-mesh3d","MFM3") が後勝ちで焼き込まれ型↔4CC 対応が壊れる (実際に
   *     export 後の読み直しで agent が死んだ)。 */
  register_types_from_csv(types, d->types, d->type_tags);

  /* ③ キャッシュキーソルト (v6 で記述子へ移動)。 */
  if (d->hash_salt != 0)
    register_hash_salt(id, d->hash_salt);
}

const srava_module_descriptor *
pigModuleRegistry::descriptor(int module_id) const
{
  if (module_id < 0 || (size_t)module_id >= descs_v.size()) return 0;
  return descs_v[(size_t)module_id];
}

int
pigModuleRegistry::supports_op(int module_id, const char *op) const
{
  const srava_module_descriptor* d = descriptor(module_id);
  if (d == 0 || d->ops == 0 || d->n_ops <= 0) return -1;   /* 不明 (万能フォールバック扱い) */
  for (int i = 0; i < d->n_ops; ++i)
    if (d->ops[i].op && op && ::strcmp(d->ops[i].op, op) == 0) return 1;
  return 0;
}

const char *
pigModuleRegistry::op_sig(int module_id, const char *op) const
{
  const srava_module_descriptor* d = descriptor(module_id);
  if (d == 0 || d->ops == 0 || d->n_ops <= 0 || op == 0) return 0;
  for (int i = 0; i < d->n_ops; ++i)
    if (d->ops[i].op && ::strcmp(d->ops[i].op, op) == 0) return d->ops[i].sig;   /* 未注釈は sig=0 */
  return 0;
}

unsigned
pigModuleRegistry::exec_caps(int module_id) const
{
  const srava_module_descriptor* d = descriptor(module_id);
  return d ? d->exec_caps : 0u;
}

int
pigModuleRegistry::any_supports_op(const char *op) const
{
  if (op == 0) return 0;
  for (size_t i = 1 /*skip delayed*/; i < descs_v.size(); ++i) {
    if (!is_enabled((int)i)) continue;   /* 無効カーネルは generic 受理の候補にしない */
    if (supports_op((int)i, op) == 1) return 1;
  }
  return 0;
}

int
pigModuleRegistry::op_out_is_mesh(const char *op) const
{
  if (op == 0) return -1;
  for (size_t i = 1; i < descs_v.size(); ++i) {
    const srava_module_descriptor* d = descs_v[i];
    if (d == 0 || d->ops == 0) continue;
    for (int k = 0; k < d->n_ops; ++k)
      if (d->ops[k].op && ::strcmp(d->ops[k].op, op) == 0)
        return (d->ops[k].out == AK_CACHE) ? 1 : 0;   /* mesh=1 / value=0 */
  }
  return -1;   /* 不明 (どの記述子にも無い) */
}

int
pigModuleRegistry::can_import_ext(int module_id, const char *ext) const
{
  const srava_module_descriptor* d = descriptor(module_id);
  if (d == 0 || d->import_exts == 0) return -1;   /* 不明 */
  return csv_has_ext(d->import_exts, ext) ? 1 : 0;
}

int
pigModuleRegistry::can_export_ext(int module_id, const char *ext) const
{
  const srava_module_descriptor* d = descriptor(module_id);
  if (d == 0 || d->export_exts == 0) return -1;   /* 不明 */
  return csv_has_ext(d->export_exts, ext) ? 1 : 0;
}

int
pigModuleRegistry::priority(int module_id) const
{
  if (module_id >= 0 && (size_t)module_id < prioOvr_v.size()
      && prioOvr_v[(size_t)module_id] != INT_MIN)
    return prioOvr_v[(size_t)module_id];          /* agent() 上書き優先 */
  const srava_module_descriptor* d = descriptor(module_id);
  return d ? d->priority : 0;
}

int
pigModuleRegistry::exec_default(int module_id) const
{
  if (module_id >= 0 && (size_t)module_id < execOvr_v.size()
      && execOvr_v[(size_t)module_id] != -1)
    return execOvr_v[(size_t)module_id];          /* agent() 上書き優先 */
  const srava_module_descriptor* d = descriptor(module_id);
  return d ? d->exec_default : 0;
}

void
pigModuleRegistry::set_priority(int module_id, int p)
{
  if (module_id < 0) return;
  ensure_ovr(module_id);
  prioOvr_v[(size_t)module_id] = p;
  seq_v[(size_t)module_id] = ++seqN_;             /* 「今ロードした扱い」= 後勝ち (docs §2.4) */
}

void
pigModuleRegistry::set_exec_default(int module_id, int exec)
{
  if (module_id < 0) return;
  ensure_ovr(module_id);
  execOvr_v[(size_t)module_id] = exec;
}

void
pigModuleRegistry::set_enabled(int module_id, bool on)
{
  if (module_id < 0) return;
  if ((size_t)module_id >= enabled_v.size()) enabled_v.resize(module_id + 1, 1);   /* 既定 = 有効 */
  enabled_v[(size_t)module_id] = on ? 1 : 0;
}

bool
pigModuleRegistry::is_enabled(int module_id) const
{
  if (module_id < 0) return false;
  if ((size_t)module_id >= enabled_v.size()) return true;                  /* 未設定 = 有効 */
  return enabled_v[(size_t)module_id] != 0;
}

const char *
pigModuleRegistry::default_module_name() const
{
  int bestId = -1, bestPrio = 0; long bestSeq = -1;
  /* 実効 priority (agent() 上書き込み) が最大・同値は seq 大 (= 後にロード/上書きした方) が勝つ。 */
  for (size_t i = 1 /*skip delayed*/; i < descs_v.size(); ++i) {
    if (descs_v[i] == 0) continue;
    if (!is_enabled((int)i)) continue;   /* agent("so","off") で無効化されたカーネルは既定候補外 */
    int p = priority((int)i);
    long s = ((size_t)i < seq_v.size()) ? seq_v[i] : 0;
    if (bestId < 0 || p > bestPrio || (p == bestPrio && s >= bestSeq)) {
      bestId = (int)i; bestPrio = p; bestSeq = s;
    }
  }
  return bestId < 0 ? "" : name_of_id(bestId);
}

void
pigModuleRegistry::register_hash_salt(int module_id, const char *salt)
{
  if (module_id < 0 || salt == 0) return;
  if ((size_t)module_id >= salts_v.size()) salts_v.resize(module_id + 1);
  salts_v[(size_t)module_id] = salt;
}

const char *
pigModuleRegistry::hash_salt(int module_id) const
{
  if (module_id < 0 || (size_t)module_id >= salts_v.size()) return 0;
  return salts_v[(size_t)module_id].empty() ? 0 : salts_v[(size_t)module_id].c_str();
}
