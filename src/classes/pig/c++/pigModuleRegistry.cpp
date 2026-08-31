/*
 * pigModuleRegistry — 実装 (ハブ・#3427 ③)。
 * 設計: pigModuleRegistry.h / docs/agent_so_design.md。
 * 旧「namespace + 可変 static テーブル」を廃し、ptsApplication が INI で thNEW する
 * stdObject 派生クラスにした (テーブルは全て直接メンバ)。
 * .so ロード (load_file / load_search_path) の実装は pigModuleLoader.cpp。
 * pig_current_registry() (TLS 経由の現行レジストリ) は ptsApplication.cpp。
 */
#include "pig/c++/pigModuleRegistry.h"
#include "pig/c++/osglue.h"        /* osglue_dlclose (unload_module) */
#include "pig/c++/pigSigGrammar.h"   /* ★ #3436 P4 §6.1: sig の文法検査 */
#include "pig/c++/pigModule.h"

#include <cstring>
#include <climits>

/* pigRefCacheCodec.cpp: 組込モジュール "pig" の記述子 (D_REF codec・カーネル非依存・pig 層)。 */
extern const srava_module_descriptor *pig_builtin_module_descriptor(void);

namespace {

/* CSV 中で name が何番目のトークンか (無ければ -1)。types の位置対応引きに使う。 */
int csv_index_of(const char *csv, const char *name)
{
  if (csv == 0 || name == 0) return -1;
  size_t nl = std::strlen(name);
  const char *p = csv;
  int i = 0;
  while (true) {
    const char *c = std::strchr(p, ',');
    size_t len = c ? (size_t)(c - p) : std::strlen(p);
    if (len == nl && std::strncmp(p, name, nl) == 0) return i;
    if (c == 0) return -1;
    p = c + 1; ++i;
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

/* ★ 2026-08-28 (ひさ設計): 非幾何型の**唯一の定義**。宣言の解説はヘッダを参照。
 *   旧実装は「op を 1 つも持たないモジュールが申告する型」という**構造の判定**
 *   (type_module_is_opless) で導いていたが、記述子から .types を撤去したので
 *   導出元が無くなった。全モジュール・planner・agent で同じ集合なので 1 本持てば足りる。 */
const char *const pig_nongeometric_types[] = { "value", "ref", 0 };

int
pig_type_is_nongeometric(const char *name)
{
  if (name == 0 || name[0] == '\0') return 0;
  for (int i = 0; pig_nongeometric_types[i] != 0; ++i)
    if (std::strcmp(name, pig_nongeometric_types[i]) == 0) return 1;
  return 0;
}

/* ★ rev4 Phase D: 予約は **"delayed"(=id 0=MODULE_NONE 番兵) だけ**。cgal/manifold は
 * 記述子駆動で登録される。組込 codec (pig-ref) はここで登録 (旧: 静的初期化の自己登録)。
 * backends (thread/process) は pigExecBackend 自身の ctor が登録する。 */
pigModuleRegistry::pigModuleRegistry()
  : seqN_(0), pdcHelper_(0)
{
  configuringId_ = -1;
  names_v.push_back("delayed");
  /* ★ #3439 ③: 組込 codec (D_REF) も **記述子**として登録する。検索が記述子走査に
   *   なったので、組込だけ別経路にすると見えなくなる。常に有効 (set_enabled の対象外)。 */
  register_descriptor(pig_builtin_module_descriptor());
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
  if ((size_t)id >= arityOvr_v.size()) arityOvr_v.resize(id + 1, 0);   /* 0 = 未設定 */
  if ((size_t)id >= optsOvr_v.size())  optsOvr_v.resize(id + 1);        /* thNULL = 未設定 */
}

void
pigModuleRegistry::register_descriptor(const srava_module_descriptor *d)
{
  if (d == 0 || d->name == 0) return;
  int id = register_module(d->name);       /* name→id (既存名は既存 id・後勝ちでメタ上書き) */
  if ((size_t)id >= descs_v.size()) descs_v.resize(id + 1, 0);
  descs_v[(size_t)id] = d;
  if ((size_t)id >= descPath_v.size()) descPath_v.resize(id + 1);
  descPath_v[(size_t)id] = loadingPath_;   /* この登録の出所 (.so パス)。組込登録なら空 */
  ensure_ovr(id);
  seq_v[(size_t)id] = ++seqN_;             /* ロード順 (後勝ちの tie-break) */

  /* ★ #3439 ③: codec 表への複製をやめた。検索 (reader_for / writer_for_body) は
   *   descs_v[id]->codecs を **is_enabled(id) を見ながら**走査する。 */

  /* ★ #3427: 以下 3 つは従来 **.so 側の静的初期化** が自分でグローバルへ書き込んでいた
   *   (mfatsAgent.cpp の register_agent / manifest.cpp の register_type・register_hash_salt)。
   *   .so は「誰のレジストリか」を知らず引数も取れないため、dlopen をいつ呼んでもプロセス全体の
   *   可変 static に書く形にしかならなかった。記述子は必要な情報を全部持っているので、登録を
   *   ここ 1 箇所に集約し、.so は「記述子を返すだけ」の受け身にする。
   *   これで登録経路が 1 本になり、#3425 の「記述子は後勝ち / 実行体・codec は先勝ち」という
   *   意味論の食い違いも構造的に起きなくなる。 */

  /* ★ #3439 ④: 実行体 (make_agent) の複製もやめた。agent_factory() が記述子を走査する。 */

  /* ★ 2026-08-28 (ひさ設計): **記述子から .types / .type_tags を撤去**した (ABI v11)。
   *   「自分が所有する型とその形式」は codecs の **writer を持つ行** がそのまま表している
   *   (types × tags の位置対応)。writer があるということは自分の body クラスで書けるということ
   *   で、これが「所有」の定義そのもの。読取専用の foreign 行 (cgal の cg-mf-upgrade 等) は
   *   writer が 0 なので混ざらない — 旧コメントが警告していた「codecs から導出すると
   *   cg-mesh3d↔MFM3 を焼き込む」は **全行から導出**した場合の話で、writer 行に限れば起きない。
   *   → 派生テーブル types_v は廃止。型軸クエリは記述子の codecs を直読みする。 */

  /* ★ #3439 ④: ソルトも複製しない。hash_salt() が記述子を直読みする。 */
}

const srava_module_descriptor *
pigModuleRegistry::descriptor(int module_id) const
{
  if (module_id < 0 || (size_t)module_id >= descs_v.size()) return 0;
  return descs_v[(size_t)module_id];
}

/* その記述子を実際に供給した .so のパス (#3425 ①)。組込登録など由来が無ければ ""。 */
const char*
pigModuleRegistry::descriptor_path(int module_id) const
{
  if (module_id < 0 || (size_t)module_id >= descPath_v.size()) return "";
  return descPath_v[(size_t)module_id].c_str();
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

const pigOpEntry *
pigModuleRegistry::op_entry(int module_id, const char *op) const
{
  const srava_module_descriptor* d = descriptor(module_id);
  if (d == 0 || d->ops == 0 || d->n_ops <= 0 || op == 0) return 0;
  for (int i = 0; i < d->n_ops; ++i)
    if (d->ops[i].op && ::strcmp(d->ops[i].op, op) == 0) return &d->ops[i];
  return 0;
}

const char*
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
  /* ★ 2026-08-18: ここで seq を進めていた (「今ロードした扱い」)。**仕様の誤り**として撤去。
   *   module() は記述子の内容を書き換える op であって、ロード順を変えるものではない。
   *   ロード順は「実際にロードした順」だけで決まる (docs §2.4 も併せて訂正)。
   *   → priority が同点になったときの勝敗は **不定** (ロード順はディレクトリ走査順で決まる)。
   *     既定を確実に切り替えたいなら、同点でなく **大きい値**を指定すること。 */
}

void
pigModuleRegistry::set_exec_default(int module_id, int exec)
{
  if (module_id < 0) return;
  ensure_ovr(module_id);
  execOvr_v[(size_t)module_id] = exec;
}

/* ★ モジュール専用の大域データ (ひさ設計 2026-08-26)。registry は **中身を知らない**
 * (stdObject のまま預かるだけ)。モジュールが自分の派生型へ d_cast して使う。 */
void
pigModuleRegistry::set_module_data(int module_id, sPtr<stdObject> d)
{
  if (module_id < 0) return;
  if ((size_t)module_id >= data_v.size()) data_v.resize(module_id + 1);
  data_v[(size_t)module_id] = d;
}

sPtr<stdObject>
pigModuleRegistry::module_data(int module_id) const
{
  if (module_id < 0 || (size_t)module_id >= data_v.size()) return sPtr<stdObject>();
  return data_v[(size_t)module_id];
}

/* ★ #3436 P4: N' = module(so,{arity:k}) → 記述子の arity → 既定 2 (docs/sig_grammar_design.md §5.4)。
 *   ⚠ N' は **policy (つまみ)** であって capability ではない。実際に何項で投げるかは
 *   k = min(N', op の sig が申告する N, 群の執行者が許す最大) で決まる。 */
int
pigModuleRegistry::arity(int module_id) const
{
  if (module_id >= 0 && (size_t)module_id < arityOvr_v.size()
      && arityOvr_v[(size_t)module_id] >= 2)
    return arityOvr_v[(size_t)module_id];         /* module() 上書き優先 */
  const srava_module_descriptor* d = descriptor(module_id);
  int a = d ? d->arity : 0;
  return (a >= 2) ? a : 2;                        /* 0 = 未指定 = 2 */
}

void
pigModuleRegistry::set_arity(int module_id, int k)
{
  if (module_id < 0 || k < 2) return;
  ensure_ovr(module_id);
  arityOvr_v[(size_t)module_id] = k;
}

/* ★ #3441: opts ハッシュ全体を保持するだけ (適用は apply_opts の役目・呼び手が別途呼ぶ)。 */
void
pigModuleRegistry::set_opts(int module_id, sPtr<pigData> opts)
{
  if (module_id < 0) return;
  ensure_ovr(module_id);
  optsOvr_v[(size_t)module_id] = opts;
}

sPtr<pigData>
pigModuleRegistry::opts_for(int module_id) const
{
  if (module_id < 0 || (size_t)module_id >= optsOvr_v.size()) return sPtr<pigData>(thNULL);
  return optsOvr_v[(size_t)module_id];
}

/* ★ #3441: そのモジュールの descriptor->configure(opts_for(id)) を呼ぶ。
 * module==0/"" の引き方は ensure_initialized(0) と同じ (agent プロセスのように
 * make_agent を持つ有効なモジュールが 1 本だけの構成で「その 1 本」を指す)。
 * ⚠ initialize と違い「1 度だけ」のガードは持たない — 呼ぶたびに configure する
 * (opts が更新されるたびに呼び直せるように。モジュール側が冪等に実装する前提)。 */
void
pigModuleRegistry::apply_opts(const char *module)
{
  int id = resolve_single_or_named(module);
  if (id < 0) return;
  const srava_module_descriptor *d = descriptor(id);
  if (d == 0 || d->configure == 0) return;
  /* ★ configure は素の関数ポインタで **自分のモジュール id を知らない**。呼んでいる間だけ
   * ここに置き、モジュール側が configuring_module_id() で引けるようにする (ひさ設計 2026-08-26)。
   * ⚠ ABI (記述子の引数) を増やさずにモジュール別の設定を実現するための最小の口。
   *   呼び出しは TS_STATE 内なので入れ子や並行はしない。 */
  configuringId_ = id;
  d->configure(opts_for(id));
  configuringId_ = -1;
}

/* ★ #3441: agent 側 (C_ENV 受信) の便利口。set_opts + apply_opts を 1 回で。 */
void
pigModuleRegistry::set_and_apply_opts(const char *module, sPtr<pigData> opts)
{
  int id = resolve_single_or_named(module);
  if (id < 0) return;
  set_opts(id, opts);
  const srava_module_descriptor *d = descriptor(id);
  if (d != 0 && d->configure != 0) {
    configuringId_ = id;
    d->configure(opts);
    configuringId_ = -1;
  }
}

/* ★ #3436 P4: op が可換か。**有効なモジュールのどれかが申告していれば可換**。
 *   (ロード時検査で「同じ op を申告するモジュール間で食い違わないこと」を要求する = §6.1)。 */
int
pigModuleRegistry::op_commutative(const char *op) const
{
  if (op == 0) return 0;
  for (size_t i = 1; i < descs_v.size(); ++i) {
    const srava_module_descriptor* d = descs_v[i];
    if (d == 0 || d->ops == 0) continue;
    for (int j = 0; j < d->n_ops; ++j)
      if (d->ops[j].op != 0 && ::strcmp(d->ops[j].op, op) == 0 && d->ops[j].commutative)
        return 1;
  }
  return 0;
}



const char *
pigModuleRegistry::default_module_name() const
{
  int bestId = -1, bestPrio = 0; long bestSeq = -1;
  /* 実効 priority (agent() 上書き込み) が最大・同値は seq 大 (= 後にロード/上書きした方) が勝つ。 */
  for (size_t i = 1 /*skip delayed*/; i < descs_v.size(); ++i) {
    if (descs_v[i] == 0) continue;
    int p = priority((int)i);
    long s = ((size_t)i < seq_v.size()) ? seq_v[i] : 0;
    if (bestId < 0 || p > bestPrio || (p == bestPrio && s >= bestSeq)) {
      bestId = (int)i; bestPrio = p; bestSeq = s;
    }
  }
  return bestId < 0 ? "" : name_of_id(bestId);
}

/* ★ #3439 ④: 派生テーブル salts_v を廃止し記述子を直読み。無効モジュールは 0 (= 基準カーネル扱い)
 *   だが、無効なモジュールへ op が振られること自体が無いので実際には参照されない。 */
const char *
pigModuleRegistry::hash_salt(int module_id) const
{
  const srava_module_descriptor *d = descriptor(module_id);
  return (d != 0 && d->hash_salt != 0 && d->hash_salt[0] != '\0') ? d->hash_salt : 0;
}

/* ★ #3439 ④: in-proc 実行体の生成子も記述子走査へ (旧 pigAgentRegistry)。
 *   module が空/0 = 「make_agent を持つ有効なモジュールがちょうど 1 つならそれ」。
 *   agent プロセスは planner が計画した .so を 1 本だけ積む (srava_agent_main.cpp:5) ので、
 *   その 1 本を名前を知らずに引くための経路。 */
pigAgentFactory
pigModuleRegistry::agent_factory(const char *module) const
{
  if (module == 0 || module[0] == '\0') {
    pigAgentFactory only = 0;
    int n = 0;
    for (size_t id = 0; id < descs_v.size(); ++id) {
      const srava_module_descriptor *d = descs_v[id];
      if (d != 0 && d->make_agent != 0) { only = d->make_agent; ++n; }
    }
    return (n == 1) ? only : 0;
  }
  for (size_t id = 0; id < descs_v.size(); ++id) {
    const srava_module_descriptor *d = descs_v[id];
    if (d != 0 && d->make_agent != 0 && d->name != 0 && std::strcmp(d->name, module) == 0)
      return d->make_agent;
  }
  return 0;
}

/* ==================================================================
 * ★ #3439 ②: 型軸クエリ — 記述子 (module の持ち物) を走査し、**無効モジュールは飛ばす**。
 *   旧 pigTypeRegistry (派生テーブル) の置き換え。module(so,"off") が
 *   「最初からロードしなかった場合」と同じ挙動になる。
 *   走査は id 昇順で**先勝ち**。同じ型名を 2 モジュールが申告するのは設定ミスで、旧実装は
 *   後勝ちで黙って上書きし「export 後の読み直しで agent が死んだ」事故を起こしている。
 *   ★ 2026-08-19: 逆引き (type_of_tag: 4CC → 型) は**撤去**した。形式 (4CC) は複数モジュールが
 *   共有してよいもの (geogram と manifold はどちらも "MFM3" を書く) なので、逆引きは先勝ちで
 *   嘘をつく。正引き tag_of_type は型名が一意なので曖昧さが無い。形式から型を見せたいときは
 *   types_readable_from_tag で**全部**列挙する。
 * ================================================================== */
/* 型名として知られているか (メンバシップのみ)。宣言のコメント参照。
 * ★ is_enabled を見ない。既に押されたスタンプの **解釈** であって、仕事の振り先の選択ではない。
 * ★ 読取専用行の types も数える。「誰かがその型を作れる」= その名前は型である、で十分。 */
int
pigModuleRegistry::type_is_known(const char *name) const
{
  if (name == 0 || name[0] == '\0') return 0;
  if (pig_type_is_nongeometric(name)) return 1;
  for (size_t id = 0; id < descs_v.size(); ++id) {
    const srava_module_descriptor *d = descs_v[id];
    if (d == 0 || d->provides == 0) continue;
    for (const pigModuleType *c = d->provides; c->wire != 0; ++c)
      if (csv_index_of(c->types, name) >= 0) return 1;
  }
  return 0;
}


/* ==================================================================
 * ★ #3439 ③: codec クエリ — 記述子 (module の持ち物) を走査し、**無効モジュールは飛ばす**。
 *   旧 pigCacheCodec (派生テーブル) の置き換え。組込 (D_REF) も "pig" 記述子として同じ経路に乗る。
 *   走査は module id 昇順で先勝ち (旧 register_codec も name 重複は先勝ちだった)。
 * ================================================================== */
namespace {
/* tags CSV でこの 4CC が何番目 (0 始まり) か。無ければ -1。 */
int csv_tag_index(const char *tags, const unsigned char tag[4])
{
  if (tags == 0) return -1;
  const char *p = tags;
  int i = 0;
  while (*p) {
    if (std::strncmp(p, (const char*)tag, 4) == 0) return i;
    const char *c = std::strchr(p, ',');
    if (c == 0) break;
    p = c + 1; ++i;
  }
  return -1;
}
}   /* anonymous namespace */

/* ★ 2026-08-28 (ABI v13): 「この形式は何として読めるか」を **申告から引かず、実際に試す**。
 *   各 wire クラスの create_for_meta にタグを渡し、受理したら返ってきた具象の type_name() を採る。
 *   タグ→型の対応表 (旧 codec 表の tags×types の位置対応) が不要になり、
 *   「表には書いてあるが実は読めない / 読めるのに表に無い」というずれが原理的に起きない。
 *   用途はエラー文 (describe) なので、順序はモジュール登録順で足りる。 */
int
pigModuleRegistry::types_readable_from_tag(const unsigned char tag[4], std::string &out) const
{
  out.clear();
  int n = 0;
  for (size_t id = 0; id < descs_v.size(); ++id) {
    const srava_module_descriptor *d = descs_v[id];
    if (d == 0 || d->provides == 0) continue;
    for (const pigModuleType *c = d->provides; c->wire != 0; ++c) {
      if (c->wire->create == 0) continue;
      sPtr<pigData> probe = c->wire->create(tag, 4);
      if (probe == thNULL) continue;
      const char *t = probe->type_name();
      if (t == 0 || t[0] == '\0') continue;
      std::string ts(t);
      if (out.find(ts) != std::string::npos) continue;   /* 同じ型を 2 度書かない */
      if (!out.empty()) out += ",";
      out += ts;
      ++n;
    }
  }
  return n;
}


/* set_body された本文を書ける writer (match 述語で選ぶ)。 */
pigCacheWriterFn
pigModuleRegistry::writer_for_body(sPtr<pigData> body) const
{
  if (body == thNULL) return 0;
  /* ★ 2026-08-28 (ABI v13): codec 表ではなく **wire クラス**の match で選ぶ。
   *   match は d_cast<階層> そのもので、writer も階層に 1 本しかない。 */
  for (size_t id = 0; id < descs_v.size(); ++id) {
    const srava_module_descriptor *d = descs_v[id];
    if (d == 0 || d->provides == 0) continue;
    for (const pigModuleType *c = d->provides; c->wire != 0; ++c)
      if (c->wire->match != 0 && c->wire->match(body) && c->wire->mkWriter != 0) return c->wire->mkWriter;
  }
  return 0;
}

/* ==================================================================
 * ★ #3439 ⑦: 記述子の自己矛盾を検出する (違反は load_file が ABI 不一致と同じく拒否する)。
 *   routing は import/export を **拡張子**で振る (can_import_ext / can_export_ext)。op を持つのに
 *   対応する exts を申告しないと、その形式を「誰も扱えない」のに一般ロジックへ落ちて、実行時に
 *   的外れなエラー (例: "export: no mesh to write") になる。申告漏れは仕様違反なので黙認しない。
 *   ※ export_vox は拡張子 routing の対象外 (専用 op) なので検査しない。
 * ================================================================== */
std::string
pig_descriptor_violation(const srava_module_descriptor *d)
{
  if (d == 0) return "descriptor is null";
  if (d->ops == 0 || d->n_ops <= 0) return std::string();
  bool hasExport = false, hasImport = false;
  for (int i = 0; i < d->n_ops; ++i) {
    if (d->ops[i].op == 0) continue;
    if (std::strcmp(d->ops[i].op, "export") == 0) hasExport = true;
    if (std::strcmp(d->ops[i].op, "import") == 0) hasImport = true;
  }
  const char *nm = (d->name != 0) ? d->name : "(null)";
  char buf[224];
  if (hasExport && (d->export_exts == 0 || d->export_exts[0] == '\0')) {
    ::snprintf(buf, sizeof buf,
               "module '%s': has an export op but declares no export_exts "
               "(routing dispatches by file extension, so the declaration is required)", nm);
    return std::string(buf);
  }
  if (hasImport && (d->import_exts == 0 || d->import_exts[0] == '\0')) {
    ::snprintf(buf, sizeof buf,
               "module '%s': has an import op but declares no import_exts "
               "(routing dispatches by file extension, so the declaration is required)", nm);
    return std::string(buf);
  }

  /* ★ #3436 P4 §6.1: sig の記法と、記述子の中で **突き合わせられる**申告の整合を見る。
   * ⚠ ここで見られるのは「その記述子だけで判定できること」に限る。§6.1 の
   *   「各 ti から主型への昇格経路が実在する」「型名が型レジストリにある」は他モジュールの
   *   codec/型が要るが、ロード順は不定だし、agent プロセスは **1 本しか載せない**のに foreign 型を
   *   sig に書くのが正常なので、ここで要求すると正しい構成を弾いてしまう。 */
  for (int i = 0; i < d->n_ops; ++i) {
    const pigOpEntry &e = d->ops[i];
    if (e.op == 0 || e.sig == 0 || e.sig[0] == '\0') continue;
    bool variable = false;                       /* この op に可変長の行があるか */
    std::string all = e.sig;
    size_t sp = 0;
    while (sp <= all.size()) {
      size_t sc = all.find(';', sp);
      std::string one = all.substr(sp, (sc == std::string::npos ? all.size() : sc) - sp);
      pigSigLine L; parse_sigline(one, L);
      if (L.bad) {
        ::snprintf(buf, sizeof buf, "module '%s': op '%s' has a malformed sig: \"%s\"",
                   nm, e.op, one.c_str());
        return std::string(buf);
      }
      if (L.kind == SK_FOLD) {
        /* 主型を含む型集合は互いに異なること (同じ型を 2 度書くのは記述子の書き間違い)。 */
        for (size_t a = 0; a < L.set.size(); ++a)
          for (size_t b = a + 1; b < L.set.size(); ++b)
            if (L.set[a] == L.set[b]) {
              ::snprintf(buf, sizeof buf,
                         "module '%s': op '%s' repeats a type in its fold form (\"%s\")",
                         nm, e.op, L.set[a].c_str());
              return std::string(buf);
            }
        /* 出力は必ず主型 (§3.3)。ref/value を返す op は fold 形を使わない。 */
        if (L.out != L.set[0]) {
          ::snprintf(buf, sizeof buf,
                     "module '%s': op '%s' fold-form output '%s' differs from the principal type '%s' "
                     "(use the repeat form {...}... if this op does not fold)",
                     nm, e.op, L.out.c_str(), L.set[0].c_str());
          return std::string(buf);
        }
      }
      if (L.kind == SK_REPEAT || (L.kind == SK_FOLD && (L.arity < 0 || L.arity > 2)))
        variable = true;
      if (sc == std::string::npos) break;
      sp = sc + 1;
    }
    /* ★ variadic フラグと sig の可変部の整合。**対で書く約束なのに突き合わせが無かった**
     *   (occt で踏んだ「export_exts はあるが op が無い」と同じ形の記述子の嘘)。
     *   片方だけだと「planner は 3 項を振れるのに agent が受け取りを拒む」等の食い違いになる。 */
    if (variable && ! e.variadic) {
      ::snprintf(buf, sizeof buf,
                 "module '%s': op '%s' has a variadic sig but the variadic flag is 0 "
                 "(the planner will dispatch n operands but the agent cannot accept them)", nm, e.op);
      return std::string(buf);
    }
    if (variable && e.vtail_value) {
      ::snprintf(buf, sizeof buf,
                 "module '%s': op '%s' has a variadic tail of geometry arguments but declares vtail_value=1 "
                 "(claiming the tail holds values)", nm, e.op);
      return std::string(buf);
    }
    /* ⚠ **逆は成り立たない**。sig は幾何引数への射影でしかない (§1) のに対し variadic は
     *   全引数を数えるので、「値引数が可変長」の op (demo_add / pipe_proximity 等) は
     *   variadic=1 かつ sig に可変部が無いのが**正常**。設計書 §6.1 の 6 は
     *   双方向に書いてあったが、片方向が正しい (実装で判明・docs を訂正済み)。 */
  }
  return std::string();
}

/* ═══ #3419: 負荷コントロールのための申告と初期化 ═══════════════════════ */

/* ★ module==0/"" なら「make_agent を持つ有効なモジュールが 1 本だけ」の構成でそれを指す
 * (agent プロセスは自分が積んだ .so が 1 本だけなので、名前を知らなくても引ける)。
 * ensure_initialized / apply_opts / set_and_apply_opts が共有する (#3441)。 */
int
pigModuleRegistry::resolve_single_or_named(const char *module) const
{
  if (module != 0 && module[0] != 0)
    return id_of_name(module);
  for (size_t i = 0; i < descs_v.size(); i++) {
    const srava_module_descriptor *d = descs_v[i];
    if (d != 0 && d->make_agent != 0) return (int)i;
  }
  return -1;
}

/* ★ §7: モジュール全体の初期化を 1 回だけ。TS_STATE 内から呼ばれる前提なので排他は不要。 */
/* ★ 2026-08-28 (ひさ設計): モジュールを **本当にアンロードする**。
 *
 *   なぜ「一度でも使われたら落とせない」で足りるか:
 *     .so (とその依存 libsrava_xx) の中身を指したまま生き残りうるのは、本体クラスの実体
 *     (cgMesh 等・pigDataCache::converted[].body に載る) と agent の 2 つ。どちらも
 *     **make_agent から始まる連鎖**でしか生まれない (agent → calc body → 本体クラス、
 *     agent → reader/writer)。そして make_agent を呼ぶ 2 箇所 (ptsMediatorInternal /
 *     ptsAgentApplication) は直前に必ず ensure_initialized を通り、そこで used_v が立つ。
 *     ★ exec_default=PROCESS のモジュールは planner 側でそこを通らないので、planner が
 *       外部 agent へ .so を託す確定点でも used_v を立てる (そちらは planner に生存
 *       オブジェクトを作らないが、「使ったものは落とせない」を一貫させる)。
 *     よって used_v が 0 = そのモジュール由来の生存オブジェクトは存在し得ない。
 *     ★ types_readable_from_tag が作る create_for_meta のプローブは関数内で解放されるので残らない。
 *
 *   記述子・ops・codecs・wires・hash_salt はすべて .so の中にあるので、dlclose の前に
 *   descs_v からも外す。id と名前は残す (再ロードで同じ id を使う)。 */
void
pigModuleRegistry::mark_used(int module_id)
{
  if (module_id < 0) return;
  if ((size_t)module_id >= used_v.size()) used_v.resize(module_id + 1, 0);
  used_v[(size_t)module_id] = 1;
}

int
pigModuleRegistry::unload_module(int module_id, std::string *err)
{
  if (module_id < 0 || (size_t)module_id >= descs_v.size() || descs_v[(size_t)module_id] == 0) {
    if (err) *err = "module is not loaded";
    return 0;
  }
  if ((size_t)module_id < used_v.size() && used_v[(size_t)module_id]) {
    if (err) *err = "already used by this program";
    return 0;
  }
  if ((size_t)module_id >= handle_v.size() || handle_v[(size_t)module_id] == 0) {
    if (err) *err = "built-in module (not loaded from a file)";
    return 0;
  }
  void *h = handle_v[(size_t)module_id];
  handle_v[(size_t)module_id] = 0;
  descs_v[(size_t)module_id]  = 0;            /* 記述子は .so の中 — 先に手放す */
  if ((size_t)module_id < descPath_v.size()) descPath_v[(size_t)module_id].clear();
  if ((size_t)module_id < optsOvr_v.size())   optsOvr_v[(size_t)module_id] = thNULL;
  osglue_dlclose(h);
  return 1;
}

void
pigModuleRegistry::ensure_initialized(const char *module)
{
  int id = resolve_single_or_named(module);
  if (id < 0) return;
  if ((int)initDone_v.size() <= id) initDone_v.resize(id + 1, 0);
  if (initDone_v[id]) return;
  initDone_v[id] = 1;                     /* 先に立てる (initialize が再入しても 2 度走らない) */
  mark_used(id);                          /* 実行体を起こす = このモジュールで仕事をした */
  const srava_module_descriptor *d = descriptor(id);
  if (d != 0 && d->initialize != 0) d->initialize();
}

