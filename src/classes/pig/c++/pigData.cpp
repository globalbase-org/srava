/*
 * pigData 軽パス実装。
 * 二項演算は p_OP 逆向き二重ディスパッチ。エラーは pigDataError が全演算を override
 * して自分を返す「吸収元」なので、base 実装に is_error() 分岐は無い(pigData.h 参照)。
 */
#include "pig/c++/pigData.h"
#include "ts2/c++/sException.h"
#include "ts2/c++/sCallSection.h"
#include "ts2/c++/stdEvent.h"
#include <stdio.h>
#include <stdlib.h>     /* getenv (SRAVA_DBG_CONV 計装) */
#include <string.h>
#include <string>
#include <typeinfo>
#include <math.h>       /* 初等関数(sin/cos/sqrt/...) */
#include <sys/stat.h>   /* pigDataFileRef::get_hashkey の (size,mtime) ゲート */
#include <unistd.h>     /* access (pigDataCache::is_valid) */
#include "pig/c++/pigwire.h"   /* pigDataCache::get_module_tag の D_META オフセット/定数 */
#include "pig/c++/pigModuleRegistry.h"   /* カーネル名↔id・4CC→id はレジストリへ集約 (Phase 1) */
#include "pig/c++/pigTypeRegistry.h"     /* rev4 Phase B-2: 型名↔tag (継続スタンプの型リスト解釈) */
#include "pig/c++/pigCacheCodec.h"       /* P3: get_body(wantTypes) の reader_for (4CC×型で変換可否) */
#include "pig/c++/pigModuleLoader.h"
#include "pig/c++/osglue.h"   /* OSGLUE_MODULE_SUFFIX: module("x.so") の拡張子正規化 */     /* load()/agent() op が .so をロード (Phase 4b) */
#include "pig/c++/pigModule.h"           /* srava_module_descriptor / EXEC_THREAD・EXEC_PROCESS */

/* ------------------------------------------------------------------ */
/* pigData 基底                                                        */
/* ------------------------------------------------------------------ */

pHashKeyType
pigData::get_hashkey()
{
  /* FNV-1a 64bit。型名(typeid のマングル名)を先に混ぜて型違いを別ハッシュに。 */
  uint64_t h = 1469598103934665603ULL;
  const uint64_t prime = 1099511628211ULL;
  for (const char *p = typeid(*this).name(); *p; ++p) { h ^= (unsigned char)*p; h *= prime; }
  h ^= 0x2f;  h *= prime;
  sPtr<stdString> s = get_str();
  for (const char *p = s->get_str(); *p; ++p) { h ^= (unsigned char)*p; h *= prime; }
  return (pHashKeyType)h;
}

/* import 用: (path, size, mtime) の安いゲートを FNV-1a/64 でハッシュ(stat のみ。内容は読まない)。
 * 危険なのは false-hit(変わったのに不変判定→stale)だが、通常の編集は mtime が前進するので
 * 誤りは false-miss(無駄な再 import)側に倒れる。現代 FS は mtime ナノ秒精度なので粒度起因の
 * false-hit はほぼ起きない(残るは touch -r 等で mtime を意図的に据え置く運用のみ)。
 * 内容全読の content-hash より安く、プランナーは stat だけで済む。typeid も混ぜ衝突回避。 */
pHashKeyType pigDataFileRef::get_hashkey() {
  uint64_t h = 1469598103934665603ULL;
  const uint64_t prime = 1099511628211ULL;
  for (const char *p = "pigDataFileRef"; *p; ++p) { h ^= (unsigned char)*p; h *= prime; }
  h ^= 0x2f; h *= prime;
  const char *path = get_str()->get_str();
  for (const char *p = path; *p; ++p) { h ^= (unsigned char)*p; h *= prime; }   /* path */
  struct stat st;
  if (::stat(path, &st) == 0) {                                                  /* size, mtime */
    uint64_t sz = (uint64_t)st.st_size, mt = (uint64_t)st.st_mtime;
    for (int b = 0; b < 8; ++b) { h ^= (sz >> (8*b)) & 0xff; h *= prime; }
    for (int b = 0; b < 8; ++b) { h ^= (mt >> (8*b)) & 0xff; h *= prime; }
  }
  return (pHashKeyType)h;   /* 不在時は path のみ(size/mtime 寄与なし) */
}

/* 構造ハッシュ(可変ソートの順序キー)。typeid + op_name + 各 arg の recipe_hash を FNV-1a で混ぜる。
 * compact を一切呼ばないので評価(agent dispatch)を起こさない = 純静的。子も recipe_hash で再帰。 */
pHashKeyType pigDataOperator::recipe_hash() {
  uint64_t h = 1469598103934665603ULL;
  const uint64_t prime = 1099511628211ULL;
  for (const char *p = typeid(*this).name(); *p; ++p) { h ^= (unsigned char)*p; h *= prime; }
  if (op_name != thNULL)
    for (const char *p = op_name->get_str(); *p; ++p) { h ^= (unsigned char)*p; h *= prime; }
  for (int k = 0; k < args.length(); ++k) {
    uint64_t a = (uint64_t)args[k]->recipe_hash();
    for (int b = 0; b < 8; ++b) { h ^= (a >> (8*b)) & 0xff; h *= prime; }
  }
  return (pHashKeyType)h;
}

/* 静的正規化: 子を先に正規化(構造ハッシュが正準化される)→ 可換 op(union/intersection)の引数を
 * recipe_hash 昇順に並べ替え。これで a|||b と b|||a が同一引数順 → 同一キャッシュキー(再利用)。
 * difference は非可換なのでソートしない。box 等の固定位置引数も対象外(op_name で限定)。 */
void pigDataOperator::normalize() {
  for (int k = 0; k < args.length(); ++k)
    args[k]->normalize();
  int commutative = 0;
  if (op_name != thNULL) {
    const char *nm = op_name->get_str();
    commutative = (::strcmp(nm, "union") == 0 || ::strcmp(nm, "intersection") == 0);
  }
  if (commutative) {   /* 引数を recipe_hash 昇順に安定ソート(n 小なので挿入ソート) */
    for (int i = 1; i < args.length(); ++i)
      for (int j = i; j > 0 && (uint64_t)args[j-1]->recipe_hash() > (uint64_t)args[j]->recipe_hash(); --j) {
        sPtr<pigData> t = args[j-1]; args[j-1] = args[j]; args[j] = t;
      }
  }
}

/* 非対応エラー(エラーオペランド自体は pigDataError の override が短絡するのでここには来ない) */
#define PERR(op) sPtr<pigData>(thNEW(pigDataError, ("unsupported operation (" op ")", info, 1)))  /* fatal=型エラー */
sPtr<pigData> pigData::add(sPtr<pigData>) { return PERR("add"); }
sPtr<pigData> pigData::sub(sPtr<pigData>) { return PERR("sub"); }
sPtr<pigData> pigData::mul(sPtr<pigData>) { return PERR("mul"); }
sPtr<pigData> pigData::div(sPtr<pigData>) { return PERR("div"); }
sPtr<pigData> pigData::rem(sPtr<pigData>) { return PERR("rem"); }
sPtr<pigData> pigData::p_add(INTEGER64)       { return PERR("p_add"); }
sPtr<pigData> pigData::p_add(double)          { return PERR("p_add"); }
sPtr<pigData> pigData::p_add(sPtr<stdString>) { return PERR("p_add"); }
sPtr<pigData> pigData::p_sub(INTEGER64)       { return PERR("p_sub"); }
sPtr<pigData> pigData::p_sub(double)          { return PERR("p_sub"); }
sPtr<pigData> pigData::p_mul(INTEGER64)       { return PERR("p_mul"); }
sPtr<pigData> pigData::p_mul(double)          { return PERR("p_mul"); }
sPtr<pigData> pigData::p_div(INTEGER64)       { return PERR("p_div"); }
sPtr<pigData> pigData::p_div(double)          { return PERR("p_div"); }
sPtr<pigData> pigData::p_rem(INTEGER64)       { return PERR("p_rem"); }
sPtr<pigData> pigData::get_ix(sPtr<pigData>)               { return PERR("index get"); }
sPtr<pigData> pigData::set_ix(sPtr<pigData>, sPtr<pigData>){ return PERR("index set"); }
/* 非 pair の car/cdr: thNULL でなくエラー値を返す(get_str/cmp が安全に流れる)。 */
sPtr<pigData> pigData::car()                               { return PERR("car"); }
sPtr<pigData> pigData::cdr()                               { return PERR("cdr"); }
#undef PERR

/* 論理/ビット: p_OP(左の値) OP 自分。型非依存(get_bool/get_int)。base 1 実装。
 * dd は左オペランドの真偽(int 0/1)または整数値。エラーは pigDataError が短絡。 */
sPtr<pigData> pigData::p_band(int dd) { return thNEW(pigDataInteger, ((INTEGER64)(dd && get_bool()))); }
sPtr<pigData> pigData::p_bor(int dd)  { return thNEW(pigDataInteger, ((INTEGER64)(dd || get_bool()))); }
sPtr<pigData> pigData::p_bxor(int dd) { return thNEW(pigDataInteger, ((INTEGER64)(dd != get_bool()))); }
sPtr<pigData> pigData::bnot()         { return thNEW(pigDataInteger, ((INTEGER64)(!get_bool()))); }
sPtr<pigData> pigData::p_aand(INTEGER64 dd) { return thNEW(pigDataInteger, (dd & get_int())); }
sPtr<pigData> pigData::p_aor(INTEGER64 dd)  { return thNEW(pigDataInteger, (dd | get_int())); }
sPtr<pigData> pigData::p_axor(INTEGER64 dd) { return thNEW(pigDataInteger, (dd ^ get_int())); }
sPtr<pigData> pigData::anot()               { return thNEW(pigDataInteger, (~get_int())); }
sPtr<pigData> pigData::p_ashl(INTEGER64 dd) {     /* dd << 自分(=シフト量) */
  INTEGER64 n = get_int();
  if (n < 0 || n >= 64) return thNEW(pigDataError, ("shift amount out of range [0,63]", info));
  return thNEW(pigDataInteger, (dd << n));
}
sPtr<pigData> pigData::p_ashr(INTEGER64 dd) {     /* 符号付き = 算術右シフト */
  INTEGER64 n = get_int();
  if (n < 0 || n >= 64) return thNEW(pigDataError, ("shift amount out of range [0,63]", info));
  return thNEW(pigDataInteger, (dd >> n));
}

/* 比較: p_OP(左) は 左->cmp(self) で判定。エラーは pigDataError が短絡(ここには来ない)。
 * thThis は self(右オペランド)。 */
#define PIG_B(v) thNEW(pigDataInteger, ((INTEGER64)(v)))
sPtr<pigData> pigData::p_eq(sPtr<pigData> left) { return PIG_B(left->cmp(thThis) == PIG_EQ); }
sPtr<pigData> pigData::p_ne(sPtr<pigData> left) { return PIG_B(left->cmp(thThis) != PIG_EQ); }
sPtr<pigData> pigData::p_lt(sPtr<pigData> left) {
  int c = left->cmp(thThis);
  if (c == PIG_INCOMP) return thNEW(pigDataError, ("incomparable types", info));
  return PIG_B(c == PIG_LT);
}
sPtr<pigData> pigData::p_gt(sPtr<pigData> left) {
  int c = left->cmp(thThis);
  if (c == PIG_INCOMP) return thNEW(pigDataError, ("incomparable types", info));
  return PIG_B(c == PIG_GT);
}
sPtr<pigData> pigData::p_le(sPtr<pigData> left) {
  int c = left->cmp(thThis);
  if (c == PIG_INCOMP) return thNEW(pigDataError, ("incomparable types", info));
  return PIG_B(c == PIG_LT || c == PIG_EQ);
}
sPtr<pigData> pigData::p_ge(sPtr<pigData> left) {
  int c = left->cmp(thThis);
  if (c == PIG_INCOMP) return thNEW(pigDataError, ("incomparable types", info));
  return PIG_B(c == PIG_GT || c == PIG_EQ);
}
#undef PIG_B

/* ------------------------------------------------------------------ */
/* Null / Error                                                        */
/* ------------------------------------------------------------------ */

sPtr<stdString> pigDataNull::get_str() { return thNEW(stdString, ("null")); }

pigDataError::pigDataError(const char *m, sPtr<pigInfo> i, int fatal) : pigData(i), fatal_(fatal) {
  msg = thNEW(stdString, (m));
}
pigDataError::pigDataError(sPtr<stdString> m, sPtr<pigInfo> i, int fatal) : pigData(i), msg(m), fatal_(fatal) {}
sPtr<stdString> pigDataError::get_str() {
  /* ソース位置(file,line)が刻まれていれば ERROR[file,line] msg、無ければ従来の ERROR: msg。 */
  if ( info.is_notNull() && info->get_lineno() > 0 ) {
    char hd[512];
    const char *fn = info->get_filename().is_notNull()
                     ? info->get_filename()->get_str() : "?";
    ::snprintf(hd, sizeof hd, "ERROR[%s,%d] ", fn, info->get_lineno());
    return thNEW(stdString, (hd))->add(msg);
  }
  return thNEW(stdString, ("ERROR: "))->add(msg);
}

/* ------------------------------------------------------------------ */
/* Integer  (p_OP(dd) = 「左の生値 dd」 OP 「自分 d」)                  */
/* ------------------------------------------------------------------ */

sPtr<stdString> pigDataInteger::get_str() {
  char buf[32]; ::snprintf(buf, sizeof buf, "%lld", (long long)d);
  return thNEW(stdString, (buf));
}
sPtr<pigData> pigDataInteger::p_add(INTEGER64 dd) { return thNEW(pigDataInteger, (dd + d)); }
sPtr<pigData> pigDataInteger::p_add(double dd)    { return thNEW(pigDataFloat,   (dd + (double)d)); }
sPtr<pigData> pigDataInteger::p_add(sPtr<stdString> dd) {  /* str + 自分の数字 */
  return thNEW(pigDataString, (dd->add(get_str())));
}
sPtr<pigData> pigDataInteger::p_sub(INTEGER64 dd) { return thNEW(pigDataInteger, (dd - d)); }
sPtr<pigData> pigDataInteger::p_sub(double dd)    { return thNEW(pigDataFloat,   (dd - (double)d)); }
sPtr<pigData> pigDataInteger::p_mul(INTEGER64 dd) { return thNEW(pigDataInteger, (dd * d)); }
sPtr<pigData> pigDataInteger::p_mul(double dd)    { return thNEW(pigDataFloat,   (dd * (double)d)); }
sPtr<pigData> pigDataInteger::p_div(INTEGER64 dd) {
  if (d == 0) return thNEW(pigDataError, ("division by zero", info));
  return thNEW(pigDataInteger, (dd / d));
}
sPtr<pigData> pigDataInteger::p_div(double dd)    { return thNEW(pigDataFloat,   (dd / (double)d)); }
sPtr<pigData> pigDataInteger::p_rem(INTEGER64 dd) {
  if (d == 0) return thNEW(pigDataError, ("modulo by zero", info));
  return thNEW(pigDataInteger, (dd % d));
}

/* ------------------------------------------------------------------ */
/* Float                                                               */
/* ------------------------------------------------------------------ */

sPtr<stdString> pigDataFloat::get_str() {
  char buf[40]; ::snprintf(buf, sizeof buf, "%.17g", d);   /* round-trip 桁 */
  return thNEW(stdString, (buf));
}
sPtr<pigData> pigDataFloat::p_add(INTEGER64 dd) { return thNEW(pigDataFloat, ((double)dd + d)); }
sPtr<pigData> pigDataFloat::p_add(double dd)    { return thNEW(pigDataFloat, (dd + d)); }
sPtr<pigData> pigDataFloat::p_add(sPtr<stdString> dd) {
  return thNEW(pigDataString, (dd->add(get_str())));
}
sPtr<pigData> pigDataFloat::p_sub(INTEGER64 dd) { return thNEW(pigDataFloat, ((double)dd - d)); }
sPtr<pigData> pigDataFloat::p_sub(double dd)    { return thNEW(pigDataFloat, (dd - d)); }
sPtr<pigData> pigDataFloat::p_mul(INTEGER64 dd) { return thNEW(pigDataFloat, ((double)dd * d)); }
sPtr<pigData> pigDataFloat::p_mul(double dd)    { return thNEW(pigDataFloat, (dd * d)); }
sPtr<pigData> pigDataFloat::p_div(INTEGER64 dd) { return thNEW(pigDataFloat, ((double)dd / d)); }
sPtr<pigData> pigDataFloat::p_div(double dd)    { return thNEW(pigDataFloat, (dd / d)); }

/* ------------------------------------------------------------------ */
/* String  (p_add は連結。左の生値 + 自分の文字列)                     */
/* ------------------------------------------------------------------ */

sPtr<pigData> pigDataString::p_add(INTEGER64 dd) {
  char buf[32]; ::snprintf(buf, sizeof buf, "%lld", (long long)dd);
  return thNEW(pigDataString, (thNEW(stdString, (buf))->add(d)));
}
sPtr<pigData> pigDataString::p_add(double dd) {
  char buf[40]; ::snprintf(buf, sizeof buf, "%.17g", dd);
  return thNEW(pigDataString, (thNEW(stdString, (buf))->add(d)));
}
sPtr<pigData> pigDataString::p_add(sPtr<stdString> dd) {
  return thNEW(pigDataString, (dd->add(d)));
}

/* ------------------------------------------------------------------ */
/* Array                                                               */
/* ------------------------------------------------------------------ */

sPtr<stdString> pigDataArray::get_str() {
  /* std::string アキュムレータ(償却 O(1))。`r->add` 連結は O(N²)(serialize と同じ罠)。
   * ★ get_hashkey() が get_str() を呼ぶため、巨大配列のキャッシュキー計算がここに律速される
   *   (#4: 8192 点 tube の planner 19s の正体。serialize は直したが get_str を見落としていた)。 */
  std::string acc = "[";
  for (int i = 0; i < d.length(); ++i) {
    if (i) acc += ",";
    acc += d[i]->get_str()->get_str();      /* 要素を観測 → 必要なら解決 */
  }
  acc += "]";
  return thNEW(stdString, (acc.c_str()));
}
/* print(x) 用: get_str(非ブロッキング repr)と違い各要素を print() で辿る。配列内の未解決 mesh
 * 継続("delayed".promise)も解決され、スカラ print(mesh) と同じくキャッシュパス(=ハッシュ.cache)が
 * 出る(従来は (delayed . <delayed>) が漏れていた)。未解決要素は print() が compact ゲートで yield
 * → print 演算子の _start 再走で続行。get_hashkey は get_str を使うので print は分離しておく。 */
sPtr<stdString> pigDataArray::print() {
  std::string acc = "[";
  for (int i = 0; i < d.length(); ++i) {
    if (i) acc += ",";
    acc += d[i]->print()->get_str();
  }
  acc += "]";
  return thNEW(stdString, (acc.c_str()));
}
/* pair repr。cdr が未解決の promise でも get_str がブロックしないよう、解決済みのときだけ展開。 */
sPtr<stdString> pigDataPair::get_str() {
  sPtr<stdString> r = thNEW(stdString, ("("));
  r = r->add(_car->get_str());
  r = r->add(" . ");
  r = r->add(_cdr->is_compact() ? _cdr->get_str() : sPtr<stdString>(thNEW(stdString,("<delayed>"))));
  return r->add(")");
}
/* print(): 継続は 3 段 ("delayed" . ("begin" . 実値))。"delayed"/"begin" の段は cdr を辿って
 * 実値(cache 等)まで解決して表示する。cdr->print() は遅延ノードなら compact ゲートで未解決時に
 * yield(呼び元 _start 再走)。それ以外の素の pair は cons 表記。 */
sPtr<stdString> pigDataPair::print() {
  if (pig_module_from_name(_car->get_str()->get_str()) >= 0 || _car->get_str()->cmp("begin") == 0)
    return _cdr->print();   /* カーネル名(継続)→("begin".実値)、"begin"→実値、と辿る */
  sPtr<stdString> r = thNEW(stdString, ("("));
  return r->add(_car->print())->add(" . ")->add(_cdr->print())->add(")");
}
/* ★ P2e (⑤ 型軸化): 旧 pigDataCache::get_module_tag (キャッシュの「サポートするカーネル」を 4CC→module で
 *   引く #3404 の API) を撤去した。routing はキャッシュの **型** (type_name) を読み、そこから型軸で
 *   executor を決める (arg_type_set→decide_executor / module_of_type)。「値に module が属す」概念は消えた。 */

/* キャッシュ本文の実装型名 = **file 先頭 D_META の 4CC → type_of_tag**。routing (arg_type_set) の
 *   一次キー。非ブロッキング (peek_tag は同期 read。file 不在/未メタなら fopen 失敗で 0)。
 *   ★ converted[] は見ない: converted[0] は「最初に載ったエントリ」で file の native 型とは限らず
 *   (downgrade で先に foreign 型を読むと不一致)、routing が誤った型を見る。型は 4CC が唯一の真実。
 *   値キャッシュ (D_META でない) は型なし = 0。 */
const char *pigDataCache::type_name() {
  unsigned char tag[4];
  if (!peek_tag(tag)) return 0;
  sPtr<pigModuleRegistry> reg = pig_current_registry();   /* ★ #3427 ③ */
  return ( reg != thNULL ) ? reg->types.type_of_tag(tag) : 0;
}

/* ★ in-memory body(#3406, 2026-0727 メモ §2 / 2026-07-29 メモ 3. で抽象化完成)。
 * ディスク I/O は専用 helper ptsDataCache が内包 (生成はフック経由 = pig 静的ライブラリから
 * codegen クラスへ直依存しないため)。待ち合わせは TSE_DESTROY 統一 (helper の ZOM 時に
 * tinyState が listener へ自動配布)。呼び出し規約: set_body/get_body は **TS_STATE から呼ぶ**
 * (大域 mtx 下でシリアライズ)。TS_THREAD 内から触らない(WSM-FgT)。 */

/* ★ カーネル名 API (2026-07-29 メモ 1.)。実体は pigModuleRegistry へ委譲 (K2・Phase 1)。
 * 公開シグネチャ (pigData.h) は互換のため残し、名前↔id の一元管理だけレジストリへ移した。
 * MODULE_NONE = id 0 = "delayed"。第 3 カーネルは pig_register_module_name (= register_module)。 */
/* ★ #3427 ③: レジストリは app 所有 (pig_current_registry = sCallSection TLS 経由)。
 * app の無い文脈 (単体テスト等) は「未登録」フォールバック ("delayed" / -1)。 */
const char *pig_module_name(int k) {
  sPtr<pigModuleRegistry> reg = pig_current_registry();
  return ( reg != thNULL ) ? reg->name_of_id(k) : "delayed";
}
int pig_module_from_name(const char *s) {
  sPtr<pigModuleRegistry> reg = pig_current_registry();
  return ( reg != thNULL ) ? reg->id_of_name(s) : -1;
}
int pig_register_module_name(const char *s) {
  sPtr<pigModuleRegistry> reg = pig_current_registry();
  return ( reg != thNULL ) ? reg->register_module(s) : -1;
}
/* 遅延継続の共有述語: car が継続マーカー文字列なら継続 pair (旧 car=="delayed" 固定判定の一般化)。
 * マーカーは **型名リスト** (新・pigfAgent が stamp) または **カーネル名** (旧・coexistence)。
 * ★ P2e (⑤ 型軸化): 型名リスト判定を「先頭トークンが **登録済み型名** か」(tag_of_type) に。旧実装は
 *   pig_module_from_type_list (型→tag→module_of_tag) で module id まで引いていたが、is_delayed に必要なのは
 *   「型スタンプか否か」= 型メンバシップだけ。これで module_of_tag 依存が消える。
 * 非 pair は car() がエラー値を返し get_str が安全に流れる = 従来判定と同じ素通り。 */
int pig_is_delayed(sPtr<pigData> v) {
  const char *car = v->car()->get_str()->get_str();
  char tok[64]; int n = 0;                        /* 先頭トークン (CSV 型名リストの先頭型) */
  while (car[n] != '\0' && car[n] != ',' && n < 63) { tok[n] = car[n]; ++n; }
  tok[n] = '\0';
  unsigned char tag[4];
  sPtr<pigModuleRegistry> reg = pig_current_registry();   /* ★ #3427 ③: app 所有レジストリ (TLS) */
  if ( reg == thNULL ) return 0;                  /* app 無し (単体テスト) = 型スタンプ概念なし */
  return reg->types.tag_of_type(tok, tag)         /* 新: 先頭が登録済み型名 (型スタンプ) */
      || reg->id_of_name(car) >= 0;               /* 旧: カーネル名スタンプ (coexistence) */
}

/* ★ #3427 ③: 旧グローバルフック (プロセス唯一の可変 static) は撤去。helper 生成子は
 * app 所有レジストリ (module_registry->pdc_helper()) が持ち、TLS で引く。
 * 未登録 (app 無し = 単体テスト) は 0 = 素の入れ物として振る舞う (従来フォールバックと同じ)。 */
static pigDataCacheHelperFn pdc_helper_fn() {
  sPtr<pigModuleRegistry> reg = pig_current_registry();
  return ( reg != thNULL ) ? reg->pdc_helper() : 0;
}

/* ★ 2026-08-12 再設計 (ひさ): pigDataCache の body は **型ごとの converted[] 一本**。canonical/foreign
 *   の区別は無い。get_body(type) は converted に該当型があれば即返し、無ければ is_valid を確認して
 *   type 指定の reader を起動する (writer 走行中は is_valid が「メタ書込済」を待つ)。value 本文
 *   (type_name()==0) は型名 "value"。設計: docs/cross_module_conversion_design.md。 */

/* value 本文 (type_name()==0) は型名 "value" として扱う (get_body("value") と対)。 */
static const char* pdc_type_of(sPtr<pigData> d) {
  const char* t = (d != thNULL) ? d->type_name() : 0;
  return (t != 0 && t[0] != '\0') ? t : "value";
}

/* converted 中の型名一致エントリ index (無ければ -1)。push_back のみ・erase しないので index は安定。 */
int pigDataCache::conv_index(const char* type) {
  for (size_t k = 0; k < converted.size(); ++k)
    if (converted[k].type != thNULL && ::strcmp(converted[k].type->get_str(), type) == 0)
      return (int)k;
  return -1;
}
/* 無ければ新規 push して index を返す (以後 index 不変)。 */
int pigDataCache::conv_ensure(const char* type) {
  int i = conv_index(type);
  if (i >= 0) return i;
  converted.push_back(ConvEntry());
  i = (int)converted.size() - 1;
  converted[i].type = thNEW(stdString,(type));
  return i;
}
/* set_body で作られた writer エントリの index (is_valid/is_complete 用)。無ければ -1。 */
int pigDataCache::writer_index() {
  for (size_t k = 0; k < converted.size(); ++k)
    if (converted[k].isWriter) return (int)k;
  return -1;
}

/* helper (ptsDataCache) が結果本文/完了を書き戻す口 (型名で該当エントリを引く)。 */
void pigDataCache::conv_set_body(const char* type, sPtr<pigData> b) {
  int i = conv_index(type);
  if (i >= 0) converted[i].body = b;
}
void pigDataCache::conv_finish(const char* type) {
  int i = conv_index(type);
  if (i >= 0) { converted[i].done = 1; converted[i].helper = thNULL; }
}

/* file 先頭の D_META 4CC を同期で覗く (get_body の候補選定 / type_name 用)。1=D_META(out に 4CC 充填)・
 * 0=D_META 無し (短小/破損)。★値キャッシュも D_META (4CC "TEXT") で始まる — 値か型付きかの判別は
 * wire_tag_is_text で行う。file は is_valid でメタ済が保証された後に呼ぶこと。
 * ★成功のみメモ化: 同一 hash = 同一データなので 4CC は一度書かれたら不変 → 2 回目以降は file を
 * 触らない。失敗 (file 不在/メタ未書込) はメモ化しない — writer 進行中に後で成功へ変わるので、
 * 焼き込むと CV_INVALID スティッキーと同種の病気になる。 */
int pigDataCache::peek_tag(unsigned char out[4]) {
  if (tagKnown) {
    out[0]=tagMemo[0]; out[1]=tagMemo[1]; out[2]=tagMemo[2]; out[3]=tagMemo[3];
    return 1;
  }
  if (path == thNULL) return 0;
  unsigned char h[WIRE_STREAMHDR_SIZE + WIRE_RECHDR_SIZE + 4];
  size_t n = 0;
  FILE *fp = ::fopen(path->get_str(), "rb");
  if (fp != 0) { n = ::fread(h, 1, sizeof h, fp); ::fclose(fp); }
  if (n < sizeof h) return 0;
  uint16_t rtype = (uint16_t)(h[WIRE_STREAMHDR_SIZE + 4] | (h[WIRE_STREAMHDR_SIZE + 5] << 8));
  if (rtype != D_META) return 0;
  const unsigned char *tag = h + WIRE_STREAMHDR_SIZE + WIRE_RECHDR_SIZE;
  tagMemo[0]=tag[0]; tagMemo[1]=tag[1]; tagMemo[2]=tag[2]; tagMemo[3]=tag[3];
  tagKnown = 1;
  out[0]=tag[0]; out[1]=tag[1]; out[2]=tag[2]; out[3]=tag[3];
  return 1;
}

/* ★ get_body 統一実装 (2026-08-12 ひさ設計)。実体は「欲しい型の候補リスト」1 本だけで、
 *   単型版・無引数版は n=1 の退化形。converted[] は一様なリスト (位置に意味なし)。
 *   ① 候補が converted に居れば: body 有 → 即返す / helper 走行中 → listen+yield (single-flight
 *      dedup) / done で body 無し → その型は読み失敗 (再起動しない)。
 *   ② 候補が無ければ is_valid (メタ書込済) を確認。writer 走行中は待つ・不在は設計違反 panic。
 *   ③ file 形式 (D_META 4CC / 値) × codec 表 (reader_for) で **読める候補**を選ぶ。読める候補が
 *      無ければ thNULL — reader は起動しない。「mesh cache に value は無い」「値キャッシュから
 *      mesh 型は作れない」も特殊ケースではなくこの一般規則の帰結。
 *   ④ 選ばれた型の reader helper を起動して listen+yield。 */
sPtr<pigData> pigDataCache::get_body(const char* const* wantTypes, int n) {
  static const char* const kValue = "value";
  if (wantTypes == 0 || n <= 0) { wantTypes = &kValue; n = 1; }
  sPtr<tinyState> caller = sCallSection::key->caller();
  /* ① converted 走査。出来上がりが最優先・次に走行中 (待つ)・失敗済みは候補から外す。 */
  int pending = -1, untried = 0;
  for (int i = 0; i < n; ++i) {
    if (wantTypes[i] == 0) continue;
    int j = conv_index(wantTypes[i]);
    if (j >= 0 && converted[j].body != thNULL) return converted[j].body;
    if (j >= 0 && converted[j].helper.is_notNull()) { if (pending < 0) pending = j; continue; }
    if (j >= 0 && converted[j].done) continue;       /* 完了・body 無し = 読み失敗 */
    ++untried;
  }
  if (pending >= 0) {                                /* 誰かが読み/変換中 → 相乗り */
    converted[pending].helper->listen(caller, TSE_DESTROY);
    throw sException([](sPtr<tinyState> caller) { return 1; });
  }
  if (untried == 0) return thNULL;                   /* 全候補が読み失敗済み */
  /* ② valid (メタ書込済) を確認。
   * (a) writer 走行中でメタ未書込 → is_valid が TSE_ASSERT を購読済 → 待つ。
   * (b) writer 無し & ::access 不在 (CV_INVALID) → 存在し得ないキャッシュ = 設計違反。 */
  if (!is_valid()) {
    if (writer_index() >= 0)
      throw sException([](sPtr<tinyState> caller) { return 1; });
    stdObject::panic("pigDataCache::get_body: cache not valid and no writer (nonexistent cache)");
  }
  /* ③ 読める候補を選ぶ。★値キャッシュも D_META で始まる (WriterText が 4CC "TEXT" を書く) —
   *   値か型付きかは **4CC == "TEXT"** で判別する (wire_tag_is_text・ReaderText の gate と対)。
   *   「D_META = mesh」も「型レジストリに登録済みか」も誤り: 前者は 2026-08-12 の値読み全滅、
   *   後者はレジストリが per-binary (agent process は他 module の型を登録しないが codec は
   *   file を読める) で kernel_mix を壊した。型付き file の読み可否は codec 表 (reader_for)
   *   が唯一の真実。 */
  unsigned char tag[4];
  int isText = peek_tag(tag) ? wire_tag_is_text(tag) : 1;   /* D_META 無し (短小/破損) も値扱い */
  const char* pick = 0;
  for (int i = 0; i < n && pick == 0; ++i) {
    if (wantTypes[i] == 0) continue;
    int j = conv_index(wantTypes[i]);
    if (j >= 0 && converted[j].done) continue;       /* 失敗済みは再起動しない */
    sPtr<pigModuleRegistry> reg = pig_current_registry();   /* ★ #3427 ③ */
    if (isText ? (::strcmp(wantTypes[i], "value") == 0)
               : (reg != thNULL && reg->codecs.reader_for(tag, wantTypes[i]) != 0))
      pick = wantTypes[i];
  }
  pigDataCacheHelperFn pdcFn = pdc_helper_fn();
  if (pick == 0 || pdcFn == 0 || path == thNULL)
    return thNULL;                                   /* この file から候補のどれも作れない */
  /* ④ reader helper 起動。[CONV] は **genuine な変換 (src != target) のときだけ** 出す
   *   (native 読みは出さない・旧仕様互換)。 */
  const char* src = "value";
  if (!isText) {
    sPtr<pigModuleRegistry> reg = pig_current_registry();
    src = ( reg != thNULL ) ? reg->types.type_of_tag(tag) : 0;
  }
  if (::getenv("SRAVA_DBG_CONV") != 0 && src != 0 && ::strcmp(src, pick) != 0)
    ::fprintf(stderr, "[CONV] %s -> %s\n", src, pick);
  int i = conv_ensure(pick);
  converted[i].body = thNULL;
  converted[i].done = 0;
  converted[i].isWriter = 0;
  converted[i].helper = pdcFn(caller, sPtr<pigDataCache>(this), PDC_MODE_LOAD,
                                      thNEW(stdString,(pick)));
  if (converted[i].done) converted[i].helper = thNULL;   /* 同期完走の取りこぼし防止 */
  if (converted[i].body != thNULL) return converted[i].body;
  if (converted[i].helper.is_notNull()) {
    converted[i].helper->listen(caller, TSE_DESTROY);
    throw sException([](sPtr<tinyState> caller) { return 1; });
  }
  return converted[i].body;   /* done・body 無し = 失敗 */
}

/* 単型版 = 候補 1 個の退化形。 */
sPtr<pigData> pigDataCache::get_body(const char* type) {
  if (type == 0 || type[0] == '\0') type = "value";
  return get_body(&type, 1);
}

void pigDataCache::set_body(sPtr<pigData> d) {
  /* writer は 1 つだけ (isWriter フラグで引く。位置に意味はないが、set_body は読みより先に
   * 来る運用なので実質先頭に載る = writer_index() は常に 0 か -1)。二重 set_body は
   * 「同一キャッシュ=同一データ」の破れ = 設計違反 (上流 inflight dedup で単一化されているはず)。 */
  if (writer_index() >= 0)
    stdObject::panic("pigDataCache::set_body: body already set (double set_body)");
  const char* ty = pdc_type_of(d);
  int i = conv_ensure(ty);
  converted[i].body = d;              /* in-memory で即利用可 (同型消費者は待たない) */
  converted[i].isWriter = 1;
  /* writer helper (ptsDataCache MODE_SAVE) を起動して file へ永続化。メタ書込で CV_VALID・
   * 完了 (ZOM) で done。フック未登録 (単体テスト) は素の入れ物として done 扱い。 */
  pigDataCacheHelperFn pdcFn = pdc_helper_fn();
  if (pdcFn != 0 && path != thNULL) {
    converted[i].done = 0;
    converted[i].helper = pdcFn(sCallSection::key->caller(), sPtr<pigDataCache>(this),
                                        PDC_MODE_SAVE, thNEW(stdString,(ty)));
    if (converted[i].done) converted[i].helper = thNULL;   /* 掲示より先に完走 → 取消 */
  } else {
    converted[i].done = 1;
  }
}

/* 無引数 = 候補 {"value"} の退化形 (A_SAVE_BEGIN payload 判定 / 値本文の取得)。mesh cache に
 * 対しては「D_META から value は作れない」の一般規則で thNULL = インライン値ではない、が返る。 */
sPtr<pigData> pigDataCache::get_body() { return get_body((const char* const*)0, 0); }

/* SAVE (writer) が走り終えたか。writer エントリの done で判定・走行中は購読して 0。
 * writer が無い (process 生産者/reader) 場合は validState 確定で完了扱い。 */
int pigDataCache::is_complete() {
  int wi = writer_index();
  if (wi >= 0) {
    if (converted[wi].done) return 1;
    if (converted[wi].helper.is_notNull()) {
      converted[wi].helper->listen(sCallSection::key->caller(), TSE_DESTROY);
      return 0;
    }
    return 1;   /* helper が掲示より先に完走 (done 未反映のレース) → 完了扱い */
  }
  return (validState != CV_UNKNOWN);
}

/* A_SAVE_BEGIN の反映 (宣言的 valid 成立)。CV_INVALID からの上書きは意図: leaf 生産者の
 * ACT_START HIT 判定が焼き込んだ MISS 時の CV_INVALID を、生産者の実書込宣言で癒す。 */
void pigDataCache::mark_valid() {
  validState = CV_VALID;
}

int pigDataCache::is_valid() {
  if (validState == CV_VALID) return 1;
  if (validState == CV_INVALID) return 0;
  /* UNKNOWN: writer 走行中なら「メタ書込済 (TSE_ASSERT)」を購読して 0 (呼び手は待つ)。
   *          writer 無しなら ::access で存在検査して bit 確定 (process 生産者 = メタ済み後に来る)。 */
  int wi = writer_index();
  if (wi >= 0 && converted[wi].helper.is_notNull()) {
    converted[wi].helper->listen(sCallSection::key->caller(), TSE_ASSERT);
    return 0;
  }
  if (path != thNULL)
    validState = ( ::access(path->get_str(), F_OK) == 0 ) ? CV_VALID : CV_INVALID;
  return validState == CV_VALID;
}

sPtr<pigData> pigDataArray::get_ix(sPtr<pigData> key) {
  /* ★ 引数のエラーチェック (2026-08-11 修正)。無いとエラーを握り潰す。
   * pigDataControl は pigDataError 派生なので、これで exit も伝播するようになる。 */
  if (key->is_error()) return key;
  int ix = (int)key->get_int();
  if (ix < 0 || ix >= d.length())
    return thNEW(pigDataError, ("array index out of range", info));
  return d[ix];                        /* 観測されるまで解決しない */
}
sPtr<pigData> pigDataArray::set_ix(sPtr<pigData> key, sPtr<pigData> val) {
  if (key->is_error()) return key;     /* ★ 引数のエラーチェック (同上) */
  if (val->is_error()) return val;
  int ix = (int)key->get_int();
  if (ix < 0) return thNEW(pigDataError, ("array index out of range", info));
  if (ix >= d.length()) {
    int old = d.length();
    d.length(ix + 1);
    for (int j = old; j < ix; ++j) d[j] = thNEW(pigDataNull, ());
  }
  d[ix] = val;
  return val;
}

/* 要素ごとの算術。op: 0=add 1=sub 2=mul 3=div。右辺(o)が配列なら要素ごと(長さ一致必須)、
 * そうでなければスカラーとして各要素にブロードキャスト。要素の add/sub/mul/div を呼ぶので
 * ネスト配列(配列の配列)も再帰的に処理される。要素がエラーになれば即そのエラーを返す。 */
static sPtr<pigData> arr_elemop(pigDataArray *self, sPtr<pigData> o, int op,
                                sPtr<pigInfo> info) {
  sPtr<pigData> ov = o->compact();
  if (ov->is_error()) return ov;
  sPtr<pigDataArray> oa = ov->obt_array();
  int n = self->length();
  if (oa.is_notNull() && oa->length() != n)
    return thNEW(pigDataError, ("array arithmetic: size mismatch", info));
  sPtr<pigDataArray> r = thNEW(pigDataArray, ());
  for (int i = 0; i < n; ++i) {
    sPtr<pigData> a = self->get_ix(thNEW(pigDataInteger, ((INTEGER64)i)));
    sPtr<pigData> b = oa.is_notNull()
        ? oa->get_ix(thNEW(pigDataInteger, ((INTEGER64)i)))   /* 配列: 対応要素 */
        : ov;                                                  /* スカラー: ブロードキャスト */
    sPtr<pigData> e = (op == 0) ? a->add(b) : (op == 1) ? a->sub(b)
                    : (op == 2) ? a->mul(b) : a->div(b);
    if (e->is_error()) return e;
    r->push(e);
  }
  return r;
}
sPtr<pigData> pigDataArray::add(sPtr<pigData> o) { return arr_elemop(this, o, 0, info); }
sPtr<pigData> pigDataArray::sub(sPtr<pigData> o) { return arr_elemop(this, o, 1, info); }
sPtr<pigData> pigDataArray::mul(sPtr<pigData> o) { return arr_elemop(this, o, 2, info); }
sPtr<pigData> pigDataArray::div(sPtr<pigData> o) { return arr_elemop(this, o, 3, info); }

/* ------------------------------------------------------------------ */
/* Hash                                                                */
/* ------------------------------------------------------------------ */

int pigDataHash::find(sPtr<stdString> key) {
  for (int i = 0; i < keys.length(); ++i)
    if (keys[i]->cmp(key) == 0) return i;
  return -1;
}
sPtr<pigData> pigDataHash::get_ix(sPtr<pigData> key) {
  if (key->is_error()) return key;     /* ★ 引数のエラーチェック (pigDataArray::get_ix と同じ) */
  int i = find(key->get_str());
  if (i < 0) {
    /* どのキーが無いか + 存在するキー一覧を出す(キャッシュの hash key と紛らわしいので具体名を出す)。 */
    sPtr<stdString> m = thNEW(stdString,("hash key not found: \""));
    m = m->add(key->get_str())->add("\"");
    if (keys.length() > 0) {
      m = m->add(" (have:");
      for (int k = 0; k < keys.length(); ++k) m = m->add(" ")->add(keys[k]);
      m = m->add(")");
    }
    return thNEW(pigDataError, (m, info));
  }
  return vals[i];
}
sPtr<pigData> pigDataHash::set_ix(sPtr<pigData> key, sPtr<pigData> val) {
  if (key->is_error()) return key;     /* ★ 引数のエラーチェック (同上) */
  if (val->is_error()) return val;
  sPtr<stdString> k = key->get_str();
  int i = find(k);
  if (i >= 0) { vals[i] = val; return val; }
  keys.push(k); vals.push(val);
  return val;
}
sPtr<stdString> pigDataHash::get_str() {
  int n = keys.length();
  sArray<int> ord;
  for (int i = 0; i < n; ++i) ord.push(i);
  for (int i = 0; i < n; ++i)                 /* キーソート(正規形) */
    for (int j = i + 1; j < n; ++j)
      if (keys[ord[j]]->cmp(keys[ord[i]]) < 0) { int t = ord[i]; ord[i] = ord[j]; ord[j] = t; }
  std::string acc = "{";
  for (int i = 0; i < n; ++i) {
    if (i) acc += ",";
    acc += keys[ord[i]]->get_str();
    acc += ":";
    acc += vals[ord[i]]->get_str()->get_str();
  }
  acc += "}";
  return thNEW(stdString, (acc.c_str()));
}
/* print(x) 用: 値を print() で辿る(mesh 継続→解決しキャッシュパス)。get_str(=get_hashkey が
 * 使う非ブロッキング repr)とは分離。キーソート順は get_str と同じ。 */
sPtr<stdString> pigDataHash::print() {
  int n = keys.length();
  sArray<int> ord;
  for (int i = 0; i < n; ++i) ord.push(i);
  for (int i = 0; i < n; ++i)
    for (int j = i + 1; j < n; ++j)
      if (keys[ord[j]]->cmp(keys[ord[i]]) < 0) { int t = ord[i]; ord[i] = ord[j]; ord[j] = t; }
  std::string acc = "{";
  for (int i = 0; i < n; ++i) {
    if (i) acc += ",";
    acc += keys[ord[i]]->get_str();
    acc += ":";
    acc += vals[ord[i]]->print()->get_str();
  }
  acc += "}";
  return thNEW(stdString, (acc.c_str()));
}

/* ------------------------------------------------------------------ */
/* Environment                                                         */
/* ------------------------------------------------------------------ */

int pigEnvironment::find_local(sPtr<stdString> name) {
  for (int i = 0; i < names.length(); ++i)
    if (names[i]->cmp(name) == 0) return i;
  return -1;
}
sPtr<pigData> pigEnvironment::def_var(sPtr<stdString> name, sPtr<pigData> val) {
  int i = find_local(name);
  if (i >= 0) { values[i] = val; return val; }
  names.push(name); values.push(val);
  return val;
}
sPtr<pigData> pigEnvironment::get_var(sPtr<stdString> name) {
  int i = find_local(name);
  if (i >= 0) return values[i];
  if (parent != thNULL) return parent->get_var(name);
  return thNEW(pigDataError, (thNEW(stdString, ("undefined variable: "))->add(name), thNULL, 1));
}
sPtr<pigData> pigEnvironment::set_var(sPtr<stdString> name, sPtr<pigData> val) {
  int i = find_local(name);
  if (i >= 0) { values[i] = val; return val; }
  if (parent != thNULL) return parent->set_var(name, val);
  return thNEW(pigDataError, (thNEW(stdString, ("assign to undefined variable: "))->add(name), thNULL, 1));
}
/* 外側(根)から内側へ def_var していくので、同名は内側で上書き=shadow が保たれる。
 * 値は capture_copy(): スカラ/メッシュ/lambda は不変なので共有(=値捕捉と等価)、配列/ハッシュは
 * spine を deep copy して葉を共有 → 捕捉後の `a[i]=v`(破壊代入)もクロージャに漏れない。
 * 名前の重複コピー(外側→内側)も def_var が find_local で潰すので frozen は各名 1 エントリ。 */
void pigEnvironment::snapshot_into(sPtr<pigEnvironment> frozen) {
  if (parent != thNULL) parent->snapshot_into(frozen);
  for (int i = 0; i < names.length(); ++i)
    frozen->def_var(names[i], values[i]->capture_copy());
}

/* ------------------------------------------------------------------ */
/* Delay / Operators                                                   */
/* ------------------------------------------------------------------ */

/* 非同期 helper が結果を確定したら呼ぶ。result をセットし、compact 待ちの caller を起こす。
 * helper の終了(TSE_DESTROY)だけに頼らず、ここで TSE_UPDATED を能動発火するので、
 * helper が生存したまま結果を返す「継続」(pigfAgent: 早期に delayed を返してから処理続行)でも
 * caller が起きる。→ mid-life wake の責務を set_result 内に閉じる(呼び側は set_result するだけ)。 */
void pigDataDelay::set_result(sPtr<pigData> r, int flag) {
  if (flag && result != thNULL) return;
  result = r;
  if (helper.is_notNull())
    helper->invoke_listen(thNEW(stdEvent,(TSE_UPDATED, helper, thNULL)));
}

/* ★ 上流を止める (ひさ設計 2026-08-11)。helper を destroy し、委譲先(result)へ再帰する。
 * PIG_DBG_TD=1 で到達をトレースできる (teardown 調査と同じスイッチ)。 */
void pigDataDelay::destroy() {
  if (::getenv("PIG_DBG_TD"))
    ::fprintf(stderr, "[td] pigData destroy: helper=%d result=%d\n",
              helper.is_notNull() ? 1 : 0, (result != thNULL) ? 1 : 0);
  if (helper.is_notNull()) helper->destroy();
  if (result != thNULL)    result->destroy();
}

void pigDataDelay::preprocess() {
  start();
  if (result == thNULL) {
    if (helper.is_notNull()) {
      /* 非同期: 現在の状態機械(caller)を helper に listen させ sException で yield。
       *  - TSE_UPDATED : helper が set_result した瞬間(mid-life 継続を含む)
       *  - TSE_DESTROY : helper が set_result せず終了した場合のフォールバック
       * いずれかで caller 再起動 → compact 再評価で result 取得。
       * (caller の状態関数は compact 地点まで冪等であること) */
      sPtr<tinyState> caller = sCallSection::key->caller();
      helper->listen(caller, TSE_UPDATED);
      helper->listen(caller, TSE_DESTROY);
      throw sException([](sPtr<tinyState> caller) { return 1; });
    }
    /* result も helper も無い = この delay を解決する手段が無い(start() でも値が出ず、待つべき helper も
     * 無い)= **永遠に未解決** = どこかの設計バグ。silently null を返すと下流で謎挙動になるので、素通り
     * させず System Error(fatal)を result に立てて観測点で確実に報告・即終了させる(ひさ指摘)。
     * (クラッシュさせる panic ではなく pigDataError にして、上位の *** ERROR *** 表示 + fail-fast に乗せる) */
    result = thNEW(pigDataError, ("system error: unresolvable delay (no result & no helper)", thNULL, 1));
  }
}

sPtr<pigData> pigDataDelay::compact(int depth) {
  preprocess();
  if (result == thNULL)   /* helper も無く未確定 = 論理エラー(同期演算子は到達しない) */
    return thNEW(pigDataError, ("delay not resolved", info));
  /* result 自身が遅延ノード(varref→束縛ノード、sequence→最終文ノード、lambdaExpr→lambda 値…)の
   * ことがあるので、不動点まで解決する。値ノードの compact() は thThis を返すのでそこで止まる。
   * (継続 ("delayed".promise) は pigDataPair=値ノードなので thThis、promise は別途解決)
   * depth で再帰上限。循環束縛(var a=a;)等で無限再帰=スタックオーバーフローするのを防ぐ。 */
  if (depth <= 0)
    return thNEW(pigDataError, ("compact: depth limit exceeded (cyclic reference?)", info));
  return result->compact(depth - 1);
}
int pigDataDelay::is_compact() {
  if (result == thNULL) { start(); return 0; }
  return result->is_compact();
}

/* 演算子 _start: args を左畳み(二項)/単項適用。pigDataDelay が compact() ゲートウェイ済
 * なので acc->OP(args[i]) で左右の delay は自動解決(明示 compact 不要)。継続 ("delayed".promise)
 * の cdr 追跡は pigfAgent だけの責務で、演算子はしない(値返し op は front が実値に解決すべき)。 */
#define PIG_OP_FOLD(Name, method) \
  void pigDataOperator##Name::_start() { \
    if (args.length() == 0) { result = thNEW(pigDataNull, ()); return; } \
    /* 並列 spark: 2 引数以上なら全 args を起動だけ蹴ってから畳む(独立 op の agent が並列に走る)。 \
     * 冪等(start_flag)なので _start の yield 再走で二度蹴っても安全。 */ \
    if (args.length() >= 2) for (int i = 0; i < args.length(); ++i) args[i]->trigger(); \
    sPtr<pigData> acc = args[0]; \
    for (int i = 1; i < args.length(); ++i) acc = acc->method(args[i]); \
    result = acc; \
    /* この op が生んだ型エラー(例: mesh + mesh)は acc->method() が **operand 値の info**(値が作られた行) \
       を付けるが、本当の位置は **この式(演算子)の行**。operand 自体がエラーでない=この op が新規に作った \
       場合は、operand 値の info に引きずられず式の行で上書きする。operand 由来のエラーは内側の実位置を温存 \
       (ただし位置なしなら式の行を補う)。pigDataOperatorIndex と同作法。 */ \
    if (result->is_error() && info.is_notNull()) { \
      bool oe = false; \
      for (int i = 0; i < args.length(); ++i) if (args[i]->is_error()) { oe = true; break; } \
      if (!oe || !result->get_info().is_notNull()) result->set_info(get_info()); \
    } \
    clean();   /* result 確定 → operand DAG を解放 */ \
  }
#define PIG_OP_UN(Name, method) \
  void pigDataOperator##Name::_start() { \
    if (args.length() == 0) { result = thNEW(pigDataNull, ()); return; } \
    result = args[0]->method(); \
    if (result->is_error() && info.is_notNull() \
        && (!args[0]->is_error() || !result->get_info().is_notNull())) \
      result->set_info(get_info()); \
    clean(); \
  }
PIG_OP_FOLD(Add, add)   PIG_OP_FOLD(Sub, sub)   PIG_OP_FOLD(Mul, mul)
PIG_OP_FOLD(Div, div)   PIG_OP_FOLD(Rem, rem)
PIG_OP_FOLD(Band, band) PIG_OP_FOLD(Bor, bor)   PIG_OP_FOLD(Bxor, bxor)
PIG_OP_FOLD(Aand, aand) PIG_OP_FOLD(Aor, aor)   PIG_OP_FOLD(Axor, axor)
PIG_OP_FOLD(Ashl, ashl) PIG_OP_FOLD(Ashr, ashr)
PIG_OP_FOLD(Eq, eq)     PIG_OP_FOLD(Ne, ne)     PIG_OP_FOLD(Lt, lt)
PIG_OP_FOLD(Gt, gt)     PIG_OP_FOLD(Le, le)     PIG_OP_FOLD(Ge, ge)
PIG_OP_UN(Bnot, bnot)   PIG_OP_UN(Anot, anot)
#undef PIG_OP_FOLD
#undef PIG_OP_UN

/* export / export_async / flush 演算子の _start() 定義と async export レジストリは、srava 言語固有の
 * I/O シンク機能なので **データ層(ここ)ではなく srava アプリ層の cgptsPlanner.cpp** に置く(#3366)。
 * レジストリを planner メンバ(gc 管理下)にすることで、かつて file-static グローバルだった頃に
 * libc exit が main スレッドで破棄 → use-after-free → 終了時 SEGV を起こしていた問題を構造的に根絶した。 */

/* a[ix] / a.key: 被参照を get_ix(key)。get_ix は array=整数添字, hash=文字列キー。
 * 遅延ノード(被参照が var ref 等)は get_ix ゲートで compact 解決される。 */
void pigDataOperatorIndex::_start() {
  if (args.length() < 2) { result = thNEW(pigDataError, ("index needs base and key", info)); return; }
  result = args[0]->get_ix(args[1]);
  /* get_ix のエラー(範囲外添字・キー無し等)は配列/ハッシュ値の info(通常 null)を持つ。
   * 位置不明なら、この添字式の位置(`a[i]` の出所)を刻んで ERROR[file,line] にする。 */
  if (result->is_error() && !result->get_info().is_notNull() && info.is_notNull())
    result->set_info(info);
}
/* a[key]=val / a.key=val: 被参照を compact し set_ix で破壊的代入。val は評価地点で compact
 * (ループ変数を捕捉。pigfAssign の SET と同じ作法)。結果=代入した値。 */
void pigDataOperatorSetIndex::_start() {
  if (args.length() < 3) { result = thNEW(pigDataError, ("index assignment needs base, key, value", info)); return; }
  sPtr<pigData> base = args[0]->compact();
  if (base->is_error()) { result = base; return; }
  sPtr<pigData> key = args[1]->compact();
  if (key->is_error()) { result = key; return; }
  sPtr<pigData> val = args[2]->compact();
  if (val->is_error()) { result = val; return; }
  sPtr<pigData> r = base->set_ix(key, val);   /* array=整数添字 / hash=文字列キー。破壊的 */
  if (r->is_error() && !r->get_info().is_notNull() && info.is_notNull())
    r->set_info(info);                         /* 位置不明なら代入式の位置を刻む */
  result = r->is_error() ? r : val;           /* 代入式の値 = 代入した値 */
}
/* length(x): array/hash の要素数。被参照を compact してから型で分岐(遅延 varref も解決)。 */
void pigDataOperatorLength::_start() {
  if (args.length() < 1) { result = thNEW(pigDataError, ("length needs one argument", info)); return; }
  sPtr<pigData> v = args[0]->compact();
  if (v->is_error()) { result = v; return; }
  sPtr<pigDataArray> a = v->obt_array();
  if (a.is_notNull()) { result = thNEW(pigDataInteger, ((INTEGER64)a->length(), info)); return; }
  sPtr<pigDataHash> h = v->obt_hash();
  if (h.is_notNull()) { result = thNEW(pigDataInteger, ((INTEGER64)h->length(), info)); return; }
  result = thNEW(pigDataError, ("length: argument is not an array or hash", info));
}
/* float(x): 値を浮動小数へ変換。文字列は数値としてパース、整数は昇格、浮動小数はそのまま。
 * 配列/ハッシュはスカラでないためエラー(get_flt が 0.0 に化けるのを防ぐ)。 */
void pigDataOperatorToFloat::_start() {
  if (args.length() < 1) { result = thNEW(pigDataError, ("float needs one argument", info)); return; }
  sPtr<pigData> v = args[0]->compact();
  if (v->is_error()) { result = v; return; }
  if (v->obt_array().is_notNull()) { result = thNEW(pigDataError, ("float: argument is an array (scalar expected)", info)); return; }
  if (v->obt_hash().is_notNull())  { result = thNEW(pigDataError, ("float: argument is a hash (scalar expected)", info)); return; }
  result = thNEW(pigDataFloat, (v->get_flt(), info));
}
/* int(x): 値を整数へ変換。文字列は数値としてパース、浮動小数は 0 方向へ切り捨て、整数はそのまま。
 * 配列/ハッシュはスカラでないためエラー(get_int が 0 に化けるのを防ぐ)。 */
void pigDataOperatorToInt::_start() {
  if (args.length() < 1) { result = thNEW(pigDataError, ("int needs one argument", info)); return; }
  sPtr<pigData> v = args[0]->compact();
  if (v->is_error()) { result = v; return; }
  if (v->obt_array().is_notNull()) { result = thNEW(pigDataError, ("int: argument is an array (scalar expected)", info)); return; }
  if (v->obt_hash().is_notNull())  { result = thNEW(pigDataError, ("int: argument is a hash (scalar expected)", info)); return; }
  result = thNEW(pigDataInteger, (v->get_int(), info));
}
/* return expr: 引数式を compact(評価地点 env で値確定=ループ変数等を捕捉)→ CTRL_RETURN に包む。 */
void pigDataOperatorReturn::_start() {
  sPtr<pigData> v = ( args.length() > 0 ) ? args[0]->compact() : sPtr<pigData>(thNEW(pigDataNull, ()));
  if (v->is_error() && v->control_kind() < 0) { result = v; return; }   /* 引数自体が実エラー → 伝播 */
  result = thNEW(pigDataControl, (CTRL_RETURN, v, info));
}
/* exit msg: メッセージ式を compact → CTRL_EXIT に包む。トップレベル(プランナ)が捕捉し、メッセージ
   表示 + 正常終了(exit 0)。ループ/関数は貫通する(return と違い剥がされない)。 */
void pigDataOperatorExit::_start() {
  sPtr<pigData> v = ( args.length() > 0 ) ? args[0]->compact() : sPtr<pigData>(thNEW(pigDataNull, ()));
  if (v->is_error() && v->control_kind() < 0) { result = v; return; }   /* 引数自体が実エラー → 伝播 */
  result = thNEW(pigDataControl, (CTRL_EXIT, v, info));
}
/* catch_continue(body): body を評価し CONTINUE 信号なら握りつぶして null、他はそのまま伝播。 */
void pigDataOperatorCatchContinue::_start() {
  if (args.length() == 0) { result = thNEW(pigDataNull, ()); return; }
  sPtr<pigData> bv = args[0]->compact();
  if (bv->control_kind() == CTRL_CONTINUE) { result = thNEW(pigDataNull, ()); return; }
  result = bv;
}
/* concat(a, b, ...): 配列連結。配列引数は要素展開、非配列引数は 1 要素として追加。 */
void pigDataOperatorConcat::_start() {
  sPtr<pigDataArray> r = thNEW(pigDataArray, ());
  for (int i = 0; i < args.length(); ++i) {
    sPtr<pigData> v = args[i]->compact();
    if (v->is_error()) { result = v; return; }
    sPtr<pigDataArray> a = v->obt_array();
    if (a.is_notNull())
      for (int j = 0; j < a->length(); ++j)
        r->push(a->get_ix(thNEW(pigDataInteger, ((INTEGER64)j))));
    else
      r->push(v);   /* 非配列は 1 要素として追加 */
  }
  result = r;
}
/* transpose(arr): [n][m]→[m][n]。矩形(全行同長)必須。 */
void pigDataOperatorTranspose::_start() {
  if (args.length() < 1) { result = thNEW(pigDataError,("transpose: needs an array", info)); return; }
  sPtr<pigData> v = args[0]->compact();
  if (v->is_error()) { result = v; return; }
  sPtr<pigDataArray> outer = v->obt_array();
  if (!outer.is_notNull()) { result = thNEW(pigDataError,("transpose: argument must be an array", info)); return; }
  int n = outer->length();
  if (n == 0) { result = thNEW(pigDataArray,()); return; }
  sArray<sPtr<pigDataArray> > rows;
  rows.length(n);
  int m = -1;
  for (int i = 0; i < n; ++i) {
    sPtr<pigData> rv = outer->get_ix(thNEW(pigDataInteger,((INTEGER64)i)))->compact();
    if (rv->is_error()) { result = rv; return; }
    sPtr<pigDataArray> r = rv->obt_array();
    if (!r.is_notNull()) { result = thNEW(pigDataError,("transpose: argument must be an array of arrays", info)); return; }
    if (m < 0) m = r->length();
    else if (m != r->length()) { result = thNEW(pigDataError,("transpose: rows must have equal length", info)); return; }
    rows[i] = r;
  }
  sPtr<pigDataArray> out = thNEW(pigDataArray,());
  for (int j = 0; j < m; ++j) {
    sPtr<pigDataArray> col = thNEW(pigDataArray,());
    for (int i = 0; i < n; ++i)
      col->push(rows[i]->get_ix(thNEW(pigDataInteger,((INTEGER64)j))));
    out->push(col);
  }
  result = out;
}
/* cumsum(arr): 累積和(数値配列→浮動小数・同長)。 */
void pigDataOperatorCumsum::_start() {
  if (args.length() < 1) { result = thNEW(pigDataError,("cumsum: needs an array", info)); return; }
  sPtr<pigData> v = args[0]->compact();
  if (v->is_error()) { result = v; return; }
  sPtr<pigDataArray> a = v->obt_array();
  if (!a.is_notNull()) { result = thNEW(pigDataError,("cumsum: argument must be a numeric array", info)); return; }
  sPtr<pigDataArray> out = thNEW(pigDataArray,());
  double acc = 0.0;
  for (int i = 0; i < a->length(); ++i) {
    sPtr<pigData> e = a->get_ix(thNEW(pigDataInteger,((INTEGER64)i)))->compact();
    if (e->is_error()) { result = e; return; }
    acc += e->get_flt();
    out->push(thNEW(pigDataFloat,(acc)));
  }
  result = out;
}
/* sum(arr): 総和(数値配列→浮動小数)。 */
void pigDataOperatorSum::_start() {
  if (args.length() < 1) { result = thNEW(pigDataError,("sum: needs an array", info)); return; }
  sPtr<pigData> v = args[0]->compact();
  if (v->is_error()) { result = v; return; }
  sPtr<pigDataArray> a = v->obt_array();
  if (!a.is_notNull()) { result = thNEW(pigDataError,("sum: argument must be a numeric array", info)); return; }
  double acc = 0.0;
  for (int i = 0; i < a->length(); ++i) {
    sPtr<pigData> e = a->get_ix(thNEW(pigDataInteger,((INTEGER64)i)))->compact();
    if (e->is_error()) { result = e; return; }
    acc += e->get_flt();
  }
  result = thNEW(pigDataFloat,(acc));
}
/* 初等関数: スカラ計算(libm)。fn=op 名。unary は y 無視。角度はラジアン。 */
static double math_scalar(const char *fn, double x, double y) {
  if (!::strcmp(fn,"sin"))   return ::sin(x);
  if (!::strcmp(fn,"cos"))   return ::cos(x);
  if (!::strcmp(fn,"tan"))   return ::tan(x);
  if (!::strcmp(fn,"asin"))  return ::asin(x);
  if (!::strcmp(fn,"acos"))  return ::acos(x);
  if (!::strcmp(fn,"atan"))  return ::atan(x);
  if (!::strcmp(fn,"sqrt"))  return ::sqrt(x);
  if (!::strcmp(fn,"exp"))   return ::exp(x);
  if (!::strcmp(fn,"log"))   return ::log(x);
  if (!::strcmp(fn,"abs"))   return ::fabs(x);
  if (!::strcmp(fn,"floor")) return ::floor(x);
  if (!::strcmp(fn,"ceil"))  return ::ceil(x);
  if (!::strcmp(fn,"round")) return ::round(x);
  if (!::strcmp(fn,"sign"))  return (x>0)?1.0:((x<0)?-1.0:0.0);
  if (!::strcmp(fn,"atan2")) return ::atan2(x,y);
  if (!::strcmp(fn,"pow"))   return ::pow(x,y);
  if (!::strcmp(fn,"mod"))   return ::fmod(x,y);
  if (!::strcmp(fn,"min"))   return (x<y)?x:y;
  if (!::strcmp(fn,"max"))   return (x>y)?x:y;
  return 0.0;
}
/* 初等関数の評価(ベクトル化): 引数が配列なら要素ごと(スカラはブロードキャスト・配列同士は zip)。 */
static sPtr<pigData> math_eval(const char *fn, sArray<sPtr<pigData> >& a, int n) {
  sPtr<pigData>      v[4];
  sPtr<pigDataArray> arr[4];
  int N = -1;
  for (int i = 0; i < n && i < 4; ++i) {
    v[i] = a[i]->compact();
    if (v[i]->is_error()) return v[i];
    arr[i] = v[i]->obt_array();
    if (arr[i].is_notNull()) {
      int L = arr[i]->length();
      if (N < 0) N = L;
      else if (N != L) return thNEW(pigDataError,("math: array length mismatch"));
    }
  }
  if (N < 0) {                 /* 全スカラ */
    double x = v[0]->get_flt();
    double y = (n > 1) ? v[1]->get_flt() : 0.0;
    return thNEW(pigDataFloat,(math_scalar(fn, x, y)));
  }
  sPtr<pigDataArray> out = thNEW(pigDataArray,());     /* ベクトル化 */
  for (int k = 0; k < N; ++k) {
    sArray<sPtr<pigData> > sub;
    sub.length(n);
    for (int i = 0; i < n; ++i)
      sub[i] = arr[i].is_notNull() ? arr[i]->get_ix(thNEW(pigDataInteger,((INTEGER64)k))) : v[i];
    out->push(math_eval(fn, sub, n));
  }
  return out;
}
void pigDataOperatorMath::_start() {
  if (args.length() < 1) { result = thNEW(pigDataError,("math: needs an argument", info)); return; }
  result = math_eval(op_name->get_str(), args, args.length());
}
/* print(x, ...): 全 args の print() を連結 → 1 回だけ ::printf。print() は遅延/継続なら
 * 未解決時に yield(sException)し、start_flag が立たないので _start が再走。printf 到達 =
 * 全引数解決済み(もう yield しない)なので二重表示しない。result は最後の引数の値(passthrough)。
 * 引数が無ければ空行を表示し null を返す。エラー引数はそのまま表示(エラー文字列)。 */
void pigDataOperatorPrint::_start() {
  /* ★ 引数のエラーチェック (2026-08-11 修正)。他の op (load 等) は入口で見ているのに print だけ
   * 抜けており、**エラーや制御値をそのまま文字列化して出力**していた。
   * 実害: 関数内 exit が `print("r", f(5))` の引数に来ると "ERROR: exit" と表示されてしまう
   * (pigDataControl は pigDataError 派生なので is_error()=1 で伝播すべき値)。
   * エラー/制御値はそのまま結果として返し、上方へ伝播させる (exit はトップで捕捉される)。 */
  for (int i = 0; i < args.length(); ++i) {
    sPtr<pigData> a = args[i]->compact();
    if (a->is_error()) { result = a; clean(); return; }
  }
  sPtr<stdString> out = thNEW(stdString, (""));
  for (int i = 0; i < args.length(); ++i) {
    if (i > 0) out = out->add(" ");
    out = out->add(args[i]->print());   /* 未解決なら yield → 再走で解決後に続行 */
  }
  ::printf("%s\n", out->get_str());      /* 全解決後に 1 回(冪等) */
  ::fflush(stdout);
  result = (args.length() > 0) ? args[args.length()-1]->compact()
                               : sPtr<pigData>(thNEW(pigDataNull, ()));
  clean();
}

/* load(so) — .so をロードして registry へ配線 (planner 側・agent 不要)。冪等。結果 = モジュール名。 */
void pigDataOperatorLoad::_start() {
  if (args.length() < 1) { result = thNEW(pigDataError, ("load needs a .so path", info)); return; }
  sPtr<pigData> pv = args[0]->compact();
  if (pv->is_error()) { result = pv; return; }
  const char* path = pv->get_str()->get_str();
  sPtr<pigModuleRegistry> reg = pig_current_registry();   /* ★ #3427 ③: app 所有レジストリ (TLS) */
  if (reg == thNULL) { result = thNEW(pigDataError, ("load: no module registry (no app)", info)); return; }
  std::string err;
  const srava_module_descriptor* d = reg->load_file(path, &err, /*lazy=*/true);
  if (d == 0) {
    char buf[600]; ::snprintf(buf, sizeof buf, "load: %s: %s", path, err.c_str());
    result = thNEW(pigDataError, (buf, info));
    return;
  }
  result = thNEW(pigDataString, (d->name ? d->name : ""));
}

/* agent(so[, {exec_default, priority}]) — .so をロードし設定を上書き (docs §2.4)。結果 = モジュール名。 */
/* module("cgal.so") の拡張子をこの OS のものへ正規化する (2026-08-12)。
 * モジュールの実ファイル名は OS で違う (Linux/macOS=.so / MinGW・Cygwin=.dll) が、**スクリプトを
 * 書き換えずに済ませたい** ので、既知のモジュール拡張子はここで差し替える。
 * 例: Windows で module("cgal.so") → "cgal.dll" を読む。それ以外の文字列は素通し。 */
static std::string
normalize_module_path(const char* path) {
  std::string p(path ? path : "");
  static const char* known[] = { ".so", ".dll", ".dylib", 0 };
  for (int i = 0; known[i] != 0; ++i) {
    size_t L = ::strlen(known[i]);
    if (p.size() > L && p.compare(p.size() - L, L, known[i]) == 0) {
      if (::strcmp(known[i], OSGLUE_MODULE_SUFFIX) != 0)
        p.replace(p.size() - L, L, OSGLUE_MODULE_SUFFIX);
      break;
    }
  }
  return p;
}

void pigDataOperatorModule::_start() {
  if (args.length() < 1) { result = thNEW(pigDataError, ("module needs a .so path", info)); return; }
  sPtr<pigData> pv = args[0]->compact();
  if (pv->is_error()) { result = pv; return; }
  std::string npath = normalize_module_path(pv->get_str()->get_str());
  const char* path = npath.c_str();
  sPtr<pigModuleRegistry> reg = pig_current_registry();   /* ★ #3427 ③: app 所有レジストリ (TLS) */
  if (reg == thNULL) { result = thNEW(pigDataError, ("module: no module registry (no app)", info)); return; }
  std::string err;
  const srava_module_descriptor* d = reg->load_file(path, &err, /*lazy=*/true);
  if (d == 0) {
    char buf[600]; ::snprintf(buf, sizeof buf, "module: %s: %s", path, err.c_str());
    result = thNEW(pigDataError, (buf, info));
    return;
  }
  int id = reg->id_of_name(d->name);

  if (args.length() >= 2) {
    sPtr<pigData> ov = args[1]->compact();
    if (ov->is_error()) { result = ov; return; }
    /* ★ 文字列 "off"/"on" = 実行時無効化/有効化 (routing 候補からの出し入れ・2026-08-10)。
     *   例: module("cgal.so","off"); box(2,2,2) → cgal 無効で manifold (既定次点) へ。
     *   ロードは済ませたまま (codec は生きる) なので、無効カーネルが既に作った mesh の読みは可能。 */
    sPtr<pigDataString> sv = sPtr<pigDataString>::d_cast(ov);
    if (sv.is_notNull()) {
      const char* s = sv->get_str()->get_str();
      if      (::strcmp(s, "off") == 0) reg->set_enabled(id, false);
      else if (::strcmp(s, "on")  == 0) reg->set_enabled(id, true);
      else { result = thNEW(pigDataError, ("module: string option must be \"off\" or \"on\"", info)); return; }
      result = thNEW(pigDataString, (d->name ? d->name : ""));
      return;
    }
    sPtr<pigDataHash> opts = ov->obt_hash();
    if (opts.is_notNull()) {
      /* exec_default: "thread" / "process" */
      sPtr<pigData> ed = opts->get_ix(thNEW(pigDataString, ("exec_default")));
      if (ed.is_notNull() && !ed->is_error()) {
        const char* s = ed->get_str()->get_str();
        if      (::strcmp(s, "thread")  == 0) reg->set_exec_default(id, EXEC_THREAD);
        else if (::strcmp(s, "process") == 0) reg->set_exec_default(id, EXEC_PROCESS);
        else { result = thNEW(pigDataError, ("module: exec_default must be \"thread\" or \"process\"", info)); return; }
      }
      /* priority: 整数 (「今ロードした扱い」で後勝ち) */
      sPtr<pigData> pr = opts->get_ix(thNEW(pigDataString, ("priority")));
      if (pr.is_notNull() && !pr->is_error())
        reg->set_priority(id, (int)pr->get_int());
    }
  }
  result = thNEW(pigDataString, (d->name ? d->name : ""));
}

/* 配列構築: 各要素式を **この地点の env で compact**(評価)して値配列を作る。
 * 要素に varref が含まれても、評価が起きた地点(代入/式評価)の env で解決される。
 * 要素が遅延(mesh agent op の継続 pair 等)でも、その node をそのまま入れる(値として配列化。
 * 観測時に各要素の compact ゲートで解決=従来の配列値と同じ振る舞い)。 */
void pigDataOperatorArray::_start() {
  /* 要素を **先に全部 trigger(非ブロック並列起動)** してから compact する。これで配列リテラル
   * [a,b,c] は要素を一斉ディスパッチ=並列評価する(= 旧 par(a,b,c) と同義)。既定の遅延評価は
   * 観測で 1 つずつ force=直列化するので、先に全 trigger して並列性を出す(Add/Eq/par と同じ手筋・
   * 再 trigger は冪等で安全)。「関数引数は並列」の言語約束を配列リテラルでも満たす。 */
  for (int i = 0; i < args.length(); ++i) args[i]->trigger();
  sPtr<pigDataArray> r = thNEW(pigDataArray, ());
  for (int i = 0; i < args.length(); ++i) {
    sPtr<pigData> e = args[i]->compact();
    /* 要素がエラー(範囲外添字・キー無し等)なら、配列に埋もれさせずそのエラーを伝播する。
     * 配列のまま下流(agent のインライン引数等)へ渡すと "inline arg parse error" に化けて
     * 本当の原因(hash key not found 等)が見えなくなるため。 */
    if (e->is_error()) { result = e; return; }
    r->push(e);
  }
  result = r;
}
/* (par は撤去 — [a,b,c]=pigDataOperatorArray が並列評価で等価。上の Array::_start を参照。) */
/* ハッシュ構築: args は [key0,val0,key1,val1,...]。値式を評価地点 env で compact してハッシュ化。
 * key はパース時に確定の pigDataString(評価不要)。値がエラーならそのエラーを伝播。 */
void pigDataOperatorHash::_start() {
  sPtr<pigDataHash> r = thNEW(pigDataHash, ());
  for (int i = 0; i + 1 < args.length(); i += 2) {
    sPtr<pigData> v = args[i+1]->compact();
    if (v->is_error()) { result = v; return; }
    r->set_ix(args[i], v);
  }
  result = r;
}

/* ------------------------------------------------------------------ */
/* serialize() — round-trip 可能な値リテラル直列形(VALUE モードで読み戻せる)  */
/* ------------------------------------------------------------------ */

/* 型取得ゲートウェイの既定 = 「配列でもハッシュでもない」。pigDataArray/pigDataHash が自分を返し、
 * pigDataDelay が compact() へ委譲する (宣言は pigData.h)。 */
sPtr<pigDataArray> pigData::obt_array() { return sPtr<pigDataArray>(); }
sPtr<pigDataHash>  pigData::obt_hash()  { return sPtr<pigDataHash>(); }

sPtr<stdString> pigData::serialize() { return get_str(); }   /* 既定: 表示形(Error/Cache 等の保険) */

sPtr<stdString> pigDataNull::serialize() { return thNEW(stdString, ("null")); }

sPtr<stdString> pigDataFloat::serialize() {
  /* 整数と区別するため必ず小数点(または指数)を含める。 */
  char buf[40]; ::snprintf(buf, sizeof buf, "%.17g", d);
  for (const char *p = buf; ; ++p) {
    if (*p == 0) { ::snprintf(buf + ::strlen(buf), sizeof buf - ::strlen(buf), ".0"); break; }
    if (*p == '.' || *p == 'e' || *p == 'E' || *p == 'n' /*nan/inf*/) break;
  }
  return thNEW(stdString, (buf));
}

sPtr<stdString> pigDataString::serialize() {
  /* "..." にして \ " \n \t をエスケープ。 */
  sPtr<stdString> r = thNEW(stdString, ("\""));
  for (const char *p = d->get_str(); *p; ++p) {
    char c = *p;
    if      (c == '\\') r = r->add("\\\\");
    else if (c == '"')  r = r->add("\\\"");
    else if (c == '\n') r = r->add("\\n");
    else if (c == '\t') r = r->add("\\t");
    else { char one[2] = { c, 0 }; r = r->add(one); }
  }
  return r->add("\"");
}

sPtr<stdString> pigDataArray::serialize() {
  /* std::string アキュムレータ(幾何成長=償却 O(1) append)に各要素を連結し、最後に 1 度だけ
   * stdString を構築。stdString::add は毎回全体コピー(O(N²))、addIn も幾何成長でないため、
   * 巨大配列の serialize はここを O(N) に保つのが要(#4 で判明した二次コスト)。 */
  std::string acc = "[";
  for (int i = 0; i < d.length(); ++i) {
    if (i) acc += ",";
    acc += d[i]->serialize()->get_str();
  }
  acc += "]";
  return thNEW(stdString, (acc.c_str()));
}

sPtr<stdString> pigDataHash::serialize() {
  int n = keys.length();
  sArray<int> ord;
  for (int i = 0; i < n; ++i) ord.push(i);
  for (int i = 0; i < n; ++i)                 /* キーソート(正規形) */
    for (int j = i + 1; j < n; ++j)
      if (keys[ord[j]]->cmp(keys[ord[i]]) < 0) { int t = ord[i]; ord[i] = ord[j]; ord[j] = t; }
  std::string acc = "{";
  for (int i = 0; i < n; ++i) {
    if (i) acc += ",";
    acc += thNEW(pigDataString, (keys[ord[i]]))->serialize()->get_str();   /* キーもクォート */
    acc += ":";
    acc += vals[ord[i]]->serialize()->get_str();
  }
  acc += "}";
  return thNEW(stdString, (acc.c_str()));
}
