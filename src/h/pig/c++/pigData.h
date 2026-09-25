#ifndef PIGDATA_H
#define PIGDATA_H
#include "pig/c++/pigOpEntry.h"   /* pigWireFactoryFn (配線 get_body) */
/*
 * pigData — srava の値/式 DAG ノード(軽パス)
 *
 * stdObject 派生の普通の C++ クラス(状態機械ではない → tscpp2 不要)。
 * libtinyState2 の sPtr / stdObject / stdString / sArray を使う。
 *
 * 設計方針(ひさレビュー 2026-06-02):
 *  - 二項演算は逆向き p_OP の double dispatch。a.OP(b) = b->p_OP(a の生値/真偽/自身)。
 *    overload(引数 C++ 型)× virtual(b の動的型)で解決。型タグ enum は持たない。
 *  - エラーは「吸収元」: pigDataError があらゆる OP / p_OP を override して自分を返す。
 *    これで base 側の演算実装から is_error() 分岐が消える(短絡は純粋に virtual 解決)。
 *    pigDataError 以外は base 実装をそのまま使う(= ディスパッチ爆発しない)。
 *  - 論理/ビットは get_bool()/get_int() に一律強制(型非依存)。base p_OP 1 実装が働き、
 *    pigDataError だけが override。比較は 3-way cmp/p_cmp(型依存)を eq 系が包む。
 *  - ハッシュの型分離は typeid(*this).name()。get_str は生のまま。
 *  - compact は基底で thThis。遅延(pigDataDelay)派生のみ解決を持ち、public virtual を
 *    全て compact() ゲートウェイ(観測したら解決)。Array/Hash は要素を eager 解決しない。
 */
#include <stdint.h>
#include <vector>                /* P4: pigDataCache の型別 conv body-list */
#include <string>                /* #3595: pigCandItem.name (候補列の表示名) */
#include "ts2/c++/ts_types.h"    /* INTEGER64 (tinyState v2: ts2/c/ 廃止→c++/ に inline) */
#include "ts2/c++/stdObject.h"
#include "ts2/c++/sPtr.h"
#include "ts2/c++/stdString.h"
#include "ts2/c++/sArray.h"
#include "ts2/c++/tinyState.h"   /* pigDataDelay の非同期 helper: listen/invoke_listen/sException */
#include "ts2/c++/sCallSection.h" /* pigDataFunction<T>::_start で caller() を使う */

class ptsObject;   /* tinyState 系 元祖(pig/c++/ptsObject)。pigDataFunction の helper の実態親 */

typedef INTEGER64 pHashKeyType;

/* ptsDataCache (pigDataCache 専用 helper) の起動モードと生成子フック (#3406, 2026-07-29 メモ 3.)。
 * pigData.cpp は pig 静的ライブラリに入るため codegen クラス ptsDataCache を直接 thNEW できない
 * (tinyState 実行体を持たない単体テストが link 不能になる)。ptsDataCache.cpp の TU が静的初期化で
 * 生成子を登録し、set_body/get_body はフック経由で起動する (未登録なら body の素朴な入れ物として
 * 動く = 単体テスト互換)。**消費者向け API ではない** (pig 内部配線)。 */
class pigData;
class pigDataCache;
/* ★ P3 (⑤ cross-module 変換): LOAD_CONV を追加。SAVE=保存 / LOAD=canonical 読み (file 4CC を自型 reader で
 *   読む) / LOAD_CONV=変換読み (file 4CC + helper の target_type で reader_for を引き conv エントリへ)。 */
enum { PDC_MODE_SAVE = 0, PDC_MODE_LOAD = 1, PDC_MODE_LOAD_CONV = 2 };
/* ★ P4 修正 (conv リスト化): target_type を factory 引数に追加。LOAD_CONV のとき「どの型へ変換読みするか」を
 *   **helper 自身が不変に保持**する (共有 conv スロットの単一 convType を廃止 → 異型同時要求の競合を根絶)。
 *   SAVE/LOAD では thNULL。 */
/* ★ 2026-08-28 (ABI v12): wire = **この引数に配線された本体クラス** (0 可)。非 0 なら reader は
 *   そのクラスのものを直に起こす (型名で codec 表を引き直さない)。target_type は converted[] の
 *   dedup キーとして残る。 */
typedef sPtr<tinyState> (*pigDataCacheHelperFn)(sPtr<tinyState> starter, sPtr<pigDataCache> cache, int mode,
                                               sPtr<stdString> target_type, const pigWireClass* wire);
/* ★ #3427 ③: 旧 pigDataCache_set_helper_factory (プロセスグローバルのフック登録) は撤去。
 * 生成子は app 所有レジストリ (pigModuleRegistry::set_pdc_helper) が持ち、ptsApplication の
 * INI が ptsDataCache_helper() を登録する。 */

/* v を compact(=観測で解決) してから __TYPE か判定。true=その型である。 */
#define is_pigDataType(__TYPE, v)  sPtr<__TYPE>::d_cast((v)->compact()).is_notNull()

/* 3-way 比較の結果 */
enum pigCmp { PIG_LT = -1, PIG_EQ = 0, PIG_GT = 1, PIG_INCOMP = 2 };

/* ★ カーネル id の「値なし」番兵 (#3404)。カーネル id は今や **モジュールレジストリの登録順**
 *   (pigModuleRegistry・.so 申告駆動) で決まり、cgal/manifold の固定値は持たない (rev4 Phase D で
 *   MODULE_CGAL=1 / MODULE_MANIFOLD=2 を撤去。routing のカーネル名指しは全て型ディスパッチ+registry
 *   クエリへ移行済)。MODULE_NONE = id 0 = "delayed" = 値/カーネル中立 (どのカーネルにも寄与しない)。 */
enum pigModule { MODULE_NONE = 0 };

/* ★ カーネル名 (2026-07-29 メモ 1.): 継続 pair の car に載せる文字列。**MODULE_NONE の名前 =
 * "delayed"** (従来の見え方を保つ)。カーネルタグを pigData から追い出し、car の文字列そのもので
 * カーネルを運ぶ。判定側は pig_is_delayed 1 本に集約 → 第 3 のカーネルは pig_register_module_name
 * で名前を登録するだけで pigData も判定側も無改変 (ひさ回答 a・2026-07-29)。 */
const char *pig_module_name(int k);                 /* id → 名前 (範囲外は "delayed") */
int         pig_module_from_name(const char *s);    /* 名前 → id。未登録 = -1 */
int         pig_register_module_name(const char *s);/* 追加登録 (既存名は既存 id)。id を返す */
/* ★ P2e: 旧 pig_module_from_type_list (型リストスタンプ→module id) は撤去 (module_of_tag 依存)。
 *   継続スタンプ→executor の解決は pigfModuleAgent の型軸 routing (arg_type_set→decide_executor /
 *   module_of_type) が担う。is_delayed は「型スタンプか否か」の判定だけ残す (型メンバシップ)。 */
int         pig_is_delayed(sPtr<pigData> v);        /* v が遅延継続 pair か (car が型名スタンプ/カーネル名) */

/* ソース位置(エラー報告用)。軽パスでは省略可(thNULL 許容) */
class pigInfo : public stdObject {
public:
  pigInfo() : lineno(0) {}
  pigInfo(sPtr<stdString> fn, int ln) : filename(fn), lineno(ln) {}
  sPtr<stdString> get_filename() { return filename; }
  int get_lineno() { return lineno; }
protected:
  sPtr<stdString> filename;
  int lineno;
};

/* compact() の再帰上限(indirection 連鎖の最大段数。循環/病的に深い束縛の保険)。
 * 正常な連鎖(varref→束縛→sequence 最終文→lambda 値…)は数段なので余裕の既定値。 */
#define PIG_COMPACT_MAX 1000

class pigDataArray;   /* obt_array() の戻り型 (前方宣言。実体は下で定義) */
class pigDataHash;    /* obt_hash()  の戻り型 */
class pigDataTryCatch;   /* ★ #3482: try/catch 文のノード。env が **生ポインタ**で 1 本持つ */

class pigData : public stdObject {
public:
  pigData(sPtr<pigInfo> _info = thNULL) : info(_info) {}
  virtual ~pigData() {}

  virtual int is_error() { return 0; }
  /* 致命エラーか (planner が in-flight agent を即撤収するか drain するかの判断)。
   * is_error と対の多態述語 (pigDataError が override)。 */
  virtual int is_fatal() { return 0; }
  /* ★ #3503: in-proc の実行体が destroy に応じない要求 (PE_PANIC)。基底は偽。 */
  virtual int is_panic() { return 0; }
  /* ★★ #3482: **前段のエラーの写し / 依頼された撤収の跡** (PE_DERIVED)。基底は偽。
   *   「これは新しい失敗ではない」という印で、**集約・報告・終了コードから外す**ための述語。
   *   例: try の catch が destroy() で畳んだ agent は「失敗した」のではなく「畳まれた」。
   *   ⚠ is_fatal / is_panic と同じく **compact 済みの値に対して**聞く (遅延ノードは委譲しない)。 */
  virtual int is_derived() { return 0; }
  /* エラーの **生メッセージ** (前置なし)。ワイヤ/表示の整形をしない素の本文で、pigDataError が
   * override して msg を返す。既定は get_str()。d_cast を使わず多態で取るための述語対
   * (is_cache と同じ流儀。#3406 / 2026-07-30 メモ L651: Mediator が符号化に使う)。
   * NB: pigDataError::get_str() は "ERROR: " を前置するのでワイヤには使えない。 */
  virtual sPtr<stdString> error_message() { return get_str(); }
  /* 制御フロー信号(return/break/continue)の種別。-1=制御でない。pigDataControl が override。
   * while/関数の評価器が「エラーチェック時に」これを見て分岐する(その他の文脈では is_error で
   * 通常エラーとして伝播=ループ/関数の外に出ると "outside loop/function" エラーになる)。 */
  virtual int control_kind()    { return -1; }
  virtual sPtr<pigData> control_value() { return thThis; }   /* return の値(既定は自分) */
  virtual INTEGER64       get_int()  { return 0; }
  virtual double          get_flt()  { return 0.0; }
  virtual int             get_bool() { return 0; }
  virtual sPtr<stdString> get_str() = 0;     /* 生の表示/連結用文字列(round-trip 不可) */
  /* round-trip 可能な値リテラル直列形(VALUE モードパーサで読み戻せる)。get_str とは別。
   * string はクォート、float は必ず小数点、array/hash は再帰直列。ソースとワイヤで共有する正準形。 */
  virtual sPtr<stdString> serialize();
  virtual pHashKeyType    get_hashkey();     /* typeid + get_str の FNV-1a */

  /* 四則(型依存): a.OP(b)=b->p_OP(a の生値)。既定は非対応エラー。エラーは pigDataError が短絡 */
  virtual sPtr<pigData> add(sPtr<pigData>);
  virtual sPtr<pigData> sub(sPtr<pigData>);
  virtual sPtr<pigData> mul(sPtr<pigData>);
  virtual sPtr<pigData> div(sPtr<pigData>);
  virtual sPtr<pigData> rem(sPtr<pigData>);
  virtual sPtr<pigData> p_add(INTEGER64 dd);
  virtual sPtr<pigData> p_add(double dd);
  virtual sPtr<pigData> p_add(sPtr<stdString> dd);
  virtual sPtr<pigData> p_sub(INTEGER64 dd);
  virtual sPtr<pigData> p_sub(double dd);
  virtual sPtr<pigData> p_mul(INTEGER64 dd);
  virtual sPtr<pigData> p_mul(double dd);
  virtual sPtr<pigData> p_div(INTEGER64 dd);
  virtual sPtr<pigData> p_div(double dd);
  virtual sPtr<pigData> p_rem(INTEGER64 dd);

  /* 論理(&& || !)・ビット(& | ^ ~ << >>): get_bool()/get_int() に一律強制(型非依存)。
   * base p_OP 1 実装が働き、pigDataError だけが short-circuit override する。 */
  virtual sPtr<pigData> band(sPtr<pigData> o) { return o->p_band(get_bool()); }
  virtual sPtr<pigData> bor(sPtr<pigData> o)  { return o->p_bor(get_bool()); }
  virtual sPtr<pigData> bxor(sPtr<pigData> o) { return o->p_bxor(get_bool()); }
  virtual sPtr<pigData> bnot();
  virtual sPtr<pigData> aand(sPtr<pigData> o) { return o->p_aand(get_int()); }
  virtual sPtr<pigData> aor(sPtr<pigData> o)  { return o->p_aor(get_int()); }
  virtual sPtr<pigData> axor(sPtr<pigData> o) { return o->p_axor(get_int()); }
  virtual sPtr<pigData> anot();
  virtual sPtr<pigData> ashl(sPtr<pigData> o) { return o->p_ashl(get_int()); }
  virtual sPtr<pigData> ashr(sPtr<pigData> o) { return o->p_ashr(get_int()); }
  virtual sPtr<pigData> p_band(int dd);
  virtual sPtr<pigData> p_bor(int dd);
  virtual sPtr<pigData> p_bxor(int dd);
  virtual sPtr<pigData> p_aand(INTEGER64 dd);
  virtual sPtr<pigData> p_aor(INTEGER64 dd);
  virtual sPtr<pigData> p_axor(INTEGER64 dd);
  virtual sPtr<pigData> p_ashl(INTEGER64 dd);   /* dd を、自分(=シフト量) だけシフト */
  virtual sPtr<pigData> p_ashr(INTEGER64 dd);

  /* 比較 3-way プリミティブ(型依存・int 返し)。既定 比較不能。数値/文字列型が override */
  virtual int cmp(sPtr<pigData>)       { return PIG_INCOMP; }
  virtual int p_cmp(INTEGER64)         { return PIG_INCOMP; }
  virtual int p_cmp(double)            { return PIG_INCOMP; }
  virtual int p_cmp(sPtr<stdString>)   { return PIG_INCOMP; }

  /* 比較演算: a.OP(b)=b->p_OP(a)。p_OP は a->cmp(self) で判定。エラーは pigDataError が短絡 */
  virtual sPtr<pigData> eq(sPtr<pigData> o) { return o->p_eq(thThis); }
  virtual sPtr<pigData> ne(sPtr<pigData> o) { return o->p_ne(thThis); }
  virtual sPtr<pigData> lt(sPtr<pigData> o) { return o->p_lt(thThis); }
  virtual sPtr<pigData> gt(sPtr<pigData> o) { return o->p_gt(thThis); }
  virtual sPtr<pigData> le(sPtr<pigData> o) { return o->p_le(thThis); }
  virtual sPtr<pigData> ge(sPtr<pigData> o) { return o->p_ge(thThis); }
  virtual sPtr<pigData> p_eq(sPtr<pigData> left);
  virtual sPtr<pigData> p_ne(sPtr<pigData> left);
  virtual sPtr<pigData> p_lt(sPtr<pigData> left);
  virtual sPtr<pigData> p_gt(sPtr<pigData> left);
  virtual sPtr<pigData> p_le(sPtr<pigData> left);
  virtual sPtr<pigData> p_ge(sPtr<pigData> left);

  /* インデックス(array=整数 / hash=文字列)。既定エラー */
  virtual sPtr<pigData> get_ix(sPtr<pigData> key);
  virtual sPtr<pigData> set_ix(sPtr<pigData> key, sPtr<pigData> val);

  /* cons セル(pigDataPair)用。既定は非 pair → 「非対応」エラー。
   * thNULL を返すと car()->get_str() 等が落ちる。エラー値なら get_str()/cmp が安全に流れる
   * (pigfAgent の delayed 判定 args[i]->car()->get_str()->cmp("delayed") を非 pair でも素通り可)。 */
  virtual sPtr<pigData> car();
  virtual sPtr<pigData> cdr();

  /* キャッシュハンドル(pigDataCache)か? d_cast を使わずに多態で分岐するための述語。
   * 遅延ノード(pigDataDelay)は compact() ゲート、pigDataCache のみ真。 */
  virtual int is_cache() { return 0; }

  /* ★★ 2026-09-21 (ひさ設計): **その値が「整数として書かれたか / 浮動小数点として書かれたか」**。
   *   is_cache() と同じ族の述語 — d_cast を使わずに多態で分岐し、遅延ノードは compact() ゲート。
   *
   *   ⚠ get_int() / get_flt() では答えられない。どちらも **値を返してしまう**ので、3 と 3.0 が
   *     区別できない。「いくつか」ではなく「どう書かれたか」を訊きたいときがある:
   *       points の rand(a,b,n,seed) … a と b が両方 **整数で書かれていたら** 整数乱数
   *   ★ 書かれ方は wire を越えて保たれる (serialize() が float に必ず小数点を付け、
   *     pig_value_parse がそれを読み戻す) ので、process 経路でも in-proc でも同じ答えになる。
   *
   *   ⚠ **両方偽がありうる** — 文字列・配列・ハッシュ・null・cache ハンドルは数ではない。
   *     「is_int でない ⇒ is_flt」と読まないこと。数かどうかは `is_int() || is_flt()`。
   *   ⚠ 文字列 "3" は **偽**。get_int() は 3 を返すが、*整数として書かれてはいない*。
   *     文字列から数を作るのは float() / int() の仕事で、その結果は数の値になる。
   *   ★ inf / nan は pigDataFloat なので is_flt() が真 (浮動小数点として書かれている)。 */
  virtual int is_int() { return 0; }
  virtual int is_flt() { return 0; }

  /* ★ rev4 型ディスパッチ (§9・Phase A): この値の **cacheable 本体型名** (実装型・タグと 1:1)。
   *   ★ P2e: 旧 get_module_tag (カーネル軸: 値の所属 module を返す) は撤去。routing は型軸 (type_name)。
   *   既定 = **0 (untyped 番兵)**: 値 (TEXT)・不透明参照 (D_REF) 等の「型を持たない/変換不能」な
   *   pigData は 0 を返し、decide_executor のディスパッチ対象から外れる (§9.7 Q-D)。
   *   非 0 を返すのは WireCacheStream 表現を持つ本体 (pigDataWireTyped の具象 leaf) のみ。 */
  virtual const char* type_name() { return 0; }

  /* compact(depth): 遅延ノードの不動点解決。depth は再帰上限(循環束縛 var a=a; 等で
   * 無限再帰=スタックオーバーフローするのを防ぐ)。既定値で通常用途は十分。値ノードは thThis。 */
  virtual sPtr<pigData> compact(int depth = PIG_COMPACT_MAX) { return thThis; }
  virtual int           is_compact() { return 1; }

  /* ★ 型取得ゲートウェイ (2026-08-13 ひさ設計): 「配列/ハッシュならそれ自身、違えば thNULL」。
   *   **`sPtr<pigDataArray>::d_cast(v)` (素の RTTI) の代わりにこれを使う**。d_cast は
   *   遅延ノード (pigDataDelay) を渡されると中身が配列でも null になる — 配列/ハッシュは
   *   要素を eager 解決しない設計なので、map/lambda 由来の要素は遅延ノードのまま来る。
   *   process 経路は値がテキスト化 → pig_value_parse で素の値になるので気づかず、
   *   **in-proc でだけ嘘のエラーになる**穴だった (2026-08-12 に mfaTube/mfaPolygon/pipe_proximity で発覚)。
   *   get_int/get_flt/is_error と同じくゲートウェイにしておけば、呼び側が compact を忘れられない。 */
  virtual sPtr<pigDataArray> obt_array();
  virtual sPtr<pigDataHash>  obt_hash();

  /* ★ destroy(): 「この遅延はもう要らない」を **上流へ伝える** (2026-08-11 ひさ設計)。
   * 値ノードは何もしない。pigDataDelay だけが helper を destroy し、委譲先(result)へ再帰する。
   * ★動機: `ptsApp->set_agentError` は **pigfAgent の登録簿しか起こさない** (pigfAgent.cpp:331 の
   *   SHOULD_ABORT)。pigfApply / pigfWhile / pigfSequence 等 **agent 以外の helper を止める経路が
   *   無かった**。map が exit で他要素を畳むときのように、要らなくなった枝を名指しで止めるのに使う。 */
  virtual void          destroy() {}

  /* AST テンプレートの新鮮複製。lambda apply / while / for で body を再評価する際、
   * 遅延ノードはメモ(result/start_flag)を持つので clone で未評価の新ノードに作り直す。
   * 既定 = 自分返し(不変値リテラルは共有可)。Array/Hash/Operator/Function 系が override。 */
  virtual sPtr<pigData> clone() { return thThis; }

  /* クロージャ値捕捉用の複製(snapshot_into が使う)。**破壊的代入(a[i]=v)で変わり得る container =
   * 配列/ハッシュの spine だけを deep copy** し、不変な葉(スカラ/メッシュ継続/lambda)は **共有**する。
   * clone() と違い葉を複製しない(メッシュ継続を clone すると再計算/dedup 喪失になるため)。
   * 既定 = 自分返し(不変値)。pigDataArray/pigDataHash のみ override(要素を再帰 capture_copy)。 */
  virtual sPtr<pigData> capture_copy() { return thThis; }

  /* print(x) ビルトイン用の表示文字列。既定は get_str()。pigDataDelay でゲートウェイ化
   * (未解決の遅延/継続なら compact で解決まで yield)、pigDataPair は継続 cdr を辿る。
   * pigDataCache は get_str()=キャッシュパス(=ハッシュファイル名)をそのまま見せる。 */
  virtual sPtr<stdString> print() { return get_str(); }

  sPtr<pigInfo> get_info() { return info; }
  void          set_info(sPtr<pigInfo> i) { info = i; }   /* parse 時にソース位置(file,line)を刻む */
protected:
  sPtr<pigInfo> info;
};

/* ★ rev4 型ディスパッチ (§9.6・Phase A): cacheable 本体型の marker 基底。
 *   pigData と各具象本体 (cgMesh/mfGeom…) の間に挟む。弁別条件 = **WireCacheStream 表現を持つ本体**
 *   (codec でシリアライズされ wire/cache に乗る)。具象 leaf が type_name() を実装型名で override する
 *   (cgMesh3D="cg-mesh3d" 等)。ここ自体は追加メンバを持たない純 marker (型軸の共通祖先を与えるだけ)。
 *   Phase B で pigTypeRegistry / decide_executor がこの型を第一級の routing キーとして使う。 */
class pigDataWireTyped : public pigData {
public:
  pigDataWireTyped(sPtr<pigInfo> i = thNULL) : pigData(i) {}
};

class pigDataNull : public pigData {
public:
  pigDataNull(sPtr<pigInfo> i = thNULL) : pigData(i) {}
  virtual sPtr<stdString> get_str();
  virtual sPtr<stdString> serialize();   /* "null" */
  /* ★★ #3567 (ひさ 2026-09-21): **null 同士だけ** PIG_EQ。他はすべて PIG_INCOMP のまま。
   *   ⇒ `x == null` が書けるようになる一方、`null == 0` は false のまま・`null < 0` は
   *     従来どおり "incomparable types" のエラーで落ちる (INCOMP は **型違いの比較**全般を
   *     担っていて、"1" == 1 が false なのも同じ仕掛け ⇒ そこは触らない)。
   *   ★ 副作用: `null < null` は EQ 経由で **false**、`null <= null` は **true** になる
   *     (以前は両方エラー)。null を順序に並べたいわけではないが、EQ を返す以上は一貫する。
   *   ⚠ #3555 の `nreq` で省略された末尾を null で埋めるので、body 側が「省略された」を
   *     0 や "" と **区別して** 読めることがこの 1 本に懸かっている ⇒ 真偽だけでは混ざる。 */
  virtual int cmp(sPtr<pigData> o)
  { return sPtr<pigDataNull>::d_cast(o).is_notNull() ? PIG_EQ : PIG_INCOMP; }
};

/* エラーは吸収元: あらゆる演算で自分を返す。各演算から is_error() 分岐を排除するための要。 */
/* ★ #3475: エラーの **属性**。旧 `int fatal` (0/1) では [DERIVED] を表現できないので enum にした。
 *   3 つは **排他** (1 つのエラーはちょうど 1 つ・ひさ確定 2026-09-05) なのでビットではない。
 * ⚠ PE_FATAL = 1 は意図的。既存の `thNEW(pigDataError,(msg, info, 1))` が数十箇所あり、
 *   そのまま「fatal」の意味で通る (引数の型を int のままにしてあるのはこのため。enum 型に
 *   すると int からの暗黙変換が無く、既存の呼び出しが一斉にコンパイルエラーになる)。 */
enum pigErrClass {
  PE_NORMAL  = 0,   /* 既定。幾何の失敗等 -> drain (走り出した計算は完走させる)      */
  PE_FATAL   = 1,   /* 確定的なプログラム/型エラー -> in-flight agent を即撤収  [FATAL]   */
  PE_DERIVED = 2,   /* 前段のエラーの写し (プレースホルダ)。集約しない          [DERIVED] */
  /* ★ #3503: in-proc の実行体が destroy に応じない。**planner が abort する要求**で、
   * 幾何の失敗ではない。撤収そのものは PE_FATAL と同じく即時だが、行き着く先が違う —
   * 子プロセスを持つ agent が 0 になった時点で planner が abort する (ptsMediatorInternal /
   * cgptsPlanner の WAITAGENTS)。⚠ 殺せるスレッドが無いので、これ以外に抜ける道が無い。 */
  PE_PANIC   = 3
};

/* 属性タグの文字列。★ 属性を **文言に載せる**のは wire を跨げる唯一の手段だから
 * (agent プロセスが作ったエラーはテキストとしてパイプを渡るので、フィールドを足す方式だと
 *  属性が落ちる)。⇒ 前置きの付与は **ctor 1 箇所**に寄せる (手で付けると必ず漏れる)。 */
#define PIG_ERRTAG_FATAL	"[FATAL] "
#define PIG_ERRTAG_DERIVED	"[DERIVED] "

class pigDataError : public pigData {
public:
  /* ★ #3475: 文言は **[TAG] module/op: message** の 3 段に統一する。
   *     [TAG]    … 属性 (cls != PE_NORMAL のときだけ付く)。wire を渡るための表現
   *     module   … どのカーネルが出したか (module != 0 のときだけ付く)
   *     op: msg  … 既存の規約 (モジュール横断で定着済み)
   *   組み立てはこの ctor だけが行う。呼び出し側は **自分のモジュール名を明示的に渡す**
   *   (「いまどのモジュールを実行中か」をレジストリ/スレッドローカルに置く案は採らない —
   *    in-proc では全モジュールが 1 プロセスに同居するので、隠れ状態は実行方式に依存して
   *    壊れる。明示なら付け忘れても *名前が出ないだけ* で、*誤った名前が出ることはない*)。
   * cls: pigErrClass の値。PE_FATAL は「待つ意味がない」ので planner が in-flight agent を
   *   即撤収して終了する。PE_NORMAL (既定) の幾何の失敗等は drain。 */
  pigDataError(const char *msg, sPtr<pigInfo> i = thNULL, int cls = PE_NORMAL, const char *module = 0);
  pigDataError(sPtr<stdString> msg, sPtr<pigInfo> i = thNULL, int cls = PE_NORMAL, const char *module = 0);
  virtual int is_error() { return 1; }
  /* ★ 「前段の fatal の写し」は PE_DERIVED なので **偽**を返す。撤収は原因側が既に起動して
   *   いるので、写し側が重ねて起動する必要はない (set_agentError の wake-all を二重に撃たない
   *   という既存の作法と一致する)。 */
  /* ★ #3503: PE_PANIC も **待つ意味が無い**点では PE_FATAL と同じなので真を返す
   *   (in-flight agent の即撤収に乗せる)。行き着く先だけが違う — planner が
   *   子プロセスを持つ agent の消滅を待って abort する。 */
  virtual int is_fatal() { return cls_ == PE_FATAL || cls_ == PE_PANIC; }
  virtual int is_panic() { return cls_ == PE_PANIC; }
  virtual int is_derived() { return cls_ == PE_DERIVED; }
  int err_class() { return cls_; }
  virtual sPtr<stdString> get_str();
  sPtr<stdString> message() { return msg; }
  /* ★★ #3482: **中身をハッシュにする** (catch 内の error() が返す形)。
   *   { message: 文言(タグ抜き), class: "normal"|"fatal"|"derived"|"panic",
   *     file: ソース名, line: 行番号 }
   *   ⇒ この 4 つで pigDataError は完全に復元できる (モジュール名は message に畳まれている
   *     = #3475 の「[TAG] module/op: message」の組み立てが ctor 1 箇所という規約をそのまま使う)。
   * ★ 逆向きは pig_err_from_hash。**往復はこの 2 つだけが知っている** — 片方だけ直すと
   *   `throw error();` が黙って別のエラーになるので、鍵の名前も含めてここで対にしておく。 */
  sPtr<pigData> to_hash();
  /* ★ 位置前置き (ERROR[file,line]) の無いメッセージ。**[TAG] は含む** —
   *   これが wire を渡る文字列そのもので、受け側はタグを見て属性を復元する。 */
  virtual sPtr<stdString> error_message() { return msg; }

#define PE1(n)   virtual sPtr<pigData> n(sPtr<pigData>) { return thThis; }
#define PE0(n)   virtual sPtr<pigData> n() { return thThis; }
#define PEP(n,T) virtual sPtr<pigData> n(T) { return thThis; }
  PE1(add) PE1(sub) PE1(mul) PE1(div) PE1(rem)
  PE1(band) PE1(bor) PE1(bxor) PE0(bnot)
  PE1(aand) PE1(aor) PE1(axor) PE0(anot) PE1(ashl) PE1(ashr)
  PE1(eq) PE1(ne) PE1(lt) PE1(gt) PE1(le) PE1(ge)
  PE1(get_ix)
  virtual sPtr<pigData> set_ix(sPtr<pigData>, sPtr<pigData>) { return thThis; }
  PEP(p_add,INTEGER64) PEP(p_add,double) PEP(p_add,sPtr<stdString>)
  PEP(p_sub,INTEGER64) PEP(p_sub,double)
  PEP(p_mul,INTEGER64) PEP(p_mul,double)
  PEP(p_div,INTEGER64) PEP(p_div,double)
  PEP(p_rem,INTEGER64)
  PEP(p_band,int) PEP(p_bor,int) PEP(p_bxor,int)
  PEP(p_aand,INTEGER64) PEP(p_aor,INTEGER64) PEP(p_axor,INTEGER64)
  PEP(p_ashl,INTEGER64) PEP(p_ashr,INTEGER64)
  PEP(p_eq,sPtr<pigData>) PEP(p_ne,sPtr<pigData>) PEP(p_lt,sPtr<pigData>)
  PEP(p_gt,sPtr<pigData>) PEP(p_le,sPtr<pigData>) PEP(p_ge,sPtr<pigData>)
#undef PE1
#undef PE0
#undef PEP
protected:
  sPtr<stdString> msg;
  int cls_;        /* pigErrClass */
};

/* ★★ #3482: pigDataError::to_hash() の逆。ハッシュから pigDataError を **復元**する。
 *   復元できない値 (0・非ハッシュ・message 欠け) を渡されたら、
 *   **「復元できない」という pigDataError** を返す (呼び手はどちらもそのまま伝播させればよい)。
 *   fallbackInfo … ハッシュが位置を持たないときに使う位置 (throw 文の位置)。 */
sPtr<pigData> pig_err_from_hash(sPtr<pigData> h, sPtr<pigInfo> fallbackInfo = thNULL);

/* 制御フロー信号: return / break / continue。pigDataError を継承し、あらゆる演算を吸収して
 * 評価チェーンを上方に伝播する(エラーと同じ性質)。while/関数の評価器が control_kind() で捕捉し、
 * 捕捉されずループ/関数の外に出ると msg がそのまま表示される("break outside loop" 等)。 */
enum { CTRL_RETURN = 0, CTRL_BREAK = 1, CTRL_CONTINUE = 2, CTRL_EXIT = 3 };
class pigDataControl : public pigDataError {
public:
  pigDataControl(int k, sPtr<pigData> v = thNULL, sPtr<pigInfo> i = thNULL)
    : pigDataError( (k == CTRL_BREAK) ? "break outside loop"
                  : (k == CTRL_CONTINUE) ? "continue outside loop"
                  : (k == CTRL_EXIT) ? "exit"   /* 通常はトップレベルで捕捉され表示されない */
                  : "return outside function", i ),
      kind(k), val(v) {}
  virtual int control_kind()            { return kind; }
  virtual sPtr<pigData> control_value() { return val.is_notNull() ? val : sPtr<pigData>(thNEW(pigDataNull,())); }
  virtual sPtr<pigData> clone()         { return thThis; }   /* 不変(値は評価済み)→ 共有可 */
protected:
  int kind;
  sPtr<pigData> val;   /* return の値(break/continue は thNULL) */
};

class pigDataInteger : public pigData {
public:
  pigDataInteger(INTEGER64 v, sPtr<pigInfo> i = thNULL) : pigData(i), d(v) {}
  virtual int       is_int()   { return 1; }
  virtual INTEGER64 get_int()  { return d; }
  virtual double    get_flt()  { return (double)d; }
  virtual int       get_bool() { return d != 0; }
  virtual sPtr<stdString> get_str();
  virtual sPtr<stdString> serialize() { return get_str(); }   /* 整数は get_str と同じ */
  virtual sPtr<pigData> add(sPtr<pigData> o) { return o->p_add(d); }
  virtual sPtr<pigData> sub(sPtr<pigData> o) { return o->p_sub(d); }
  virtual sPtr<pigData> mul(sPtr<pigData> o) { return o->p_mul(d); }
  virtual sPtr<pigData> div(sPtr<pigData> o) { return o->p_div(d); }
  virtual sPtr<pigData> rem(sPtr<pigData> o) { return o->p_rem(d); }
  virtual sPtr<pigData> p_add(INTEGER64 dd);
  virtual sPtr<pigData> p_add(double dd);
  virtual sPtr<pigData> p_add(sPtr<stdString> dd);
  virtual sPtr<pigData> p_sub(INTEGER64 dd);
  virtual sPtr<pigData> p_sub(double dd);
  virtual sPtr<pigData> p_mul(INTEGER64 dd);
  virtual sPtr<pigData> p_mul(double dd);
  virtual sPtr<pigData> p_div(INTEGER64 dd);
  virtual sPtr<pigData> p_div(double dd);
  virtual sPtr<pigData> p_rem(INTEGER64 dd);
  using pigData::p_cmp;
  virtual int cmp(sPtr<pigData> o) { return o->p_cmp(d); }
  virtual int p_cmp(INTEGER64 dd)  { return dd < d ? PIG_LT : (dd > d ? PIG_GT : PIG_EQ); }
  virtual int p_cmp(double dd)     { double s = (double)d; return dd < s ? PIG_LT : (dd > s ? PIG_GT : PIG_EQ); }
protected:
  INTEGER64 d;
};

class pigDataFloat : public pigData {
public:
  pigDataFloat(double v, sPtr<pigInfo> i = thNULL) : pigData(i), d(v) {}
  virtual int       is_flt()   { return 1; }
  virtual INTEGER64 get_int()  { return (INTEGER64)d; }
  virtual double    get_flt()  { return d; }
  virtual int       get_bool() { return d != 0.0; }
  virtual sPtr<stdString> get_str();
  virtual sPtr<stdString> serialize();   /* 必ず小数点付き(整数と区別) */
  virtual sPtr<pigData> add(sPtr<pigData> o) { return o->p_add(d); }
  virtual sPtr<pigData> sub(sPtr<pigData> o) { return o->p_sub(d); }
  virtual sPtr<pigData> mul(sPtr<pigData> o) { return o->p_mul(d); }
  virtual sPtr<pigData> div(sPtr<pigData> o) { return o->p_div(d); }
  virtual sPtr<pigData> p_add(INTEGER64 dd);
  virtual sPtr<pigData> p_add(double dd);
  virtual sPtr<pigData> p_add(sPtr<stdString> dd);
  virtual sPtr<pigData> p_sub(INTEGER64 dd);
  virtual sPtr<pigData> p_sub(double dd);
  virtual sPtr<pigData> p_mul(INTEGER64 dd);
  virtual sPtr<pigData> p_mul(double dd);
  virtual sPtr<pigData> p_div(INTEGER64 dd);
  virtual sPtr<pigData> p_div(double dd);
  using pigData::p_cmp;
  virtual int cmp(sPtr<pigData> o) { return o->p_cmp(d); }
  virtual int p_cmp(INTEGER64 dd)  { double l = (double)dd; return l < d ? PIG_LT : (l > d ? PIG_GT : PIG_EQ); }
  virtual int p_cmp(double dd)     { return dd < d ? PIG_LT : (dd > d ? PIG_GT : PIG_EQ); }
protected:
  double d;
};

class pigDataString : public pigData {
public:
  pigDataString(const char *s, sPtr<pigInfo> i = thNULL)
    : pigData(i) { d = thNEW(stdString, (s)); }
  pigDataString(sPtr<stdString> s, sPtr<pigInfo> i = thNULL)
    : pigData(i), d(s) {}
  virtual INTEGER64 get_int()  { return d->get_int(); }
  virtual double    get_flt()  { return d->get_flt(); }
  virtual int       get_bool() { return d->get_str()[0] != 0; }   /* 非空=真 */
  virtual sPtr<stdString> get_str() { return d; }
  virtual sPtr<stdString> serialize();   /* クォート + エスケープ */
  virtual sPtr<pigData> add(sPtr<pigData> o) { return o->p_add(d); }   /* 連結 */
  virtual sPtr<pigData> p_add(INTEGER64 dd);
  virtual sPtr<pigData> p_add(double dd);
  virtual sPtr<pigData> p_add(sPtr<stdString> dd);
  using pigData::p_cmp;
  virtual int cmp(sPtr<pigData> o) { return o->p_cmp(d); }
  virtual int p_cmp(sPtr<stdString> dd) { int c = dd->cmp(d); return c < 0 ? PIG_LT : (c > 0 ? PIG_GT : PIG_EQ); }
protected:
  sPtr<stdString> d;
};

/* cons セル(car . cdr)。pigfAgent の遅延継続返り値 ("delayed" . promise) と、
 * 遅延引数(pair の cdr が実値)の表現に使う。car/cdr は base override(compact ゲート無し)。 */
class pigDataPair : public pigData {
public:
  pigDataPair(sPtr<pigData> car_, sPtr<pigData> cdr_, sPtr<pigInfo> i = thNULL)
    : pigData(i), _car(car_), _cdr(cdr_) {}
  virtual sPtr<pigData> car() { return _car; }
  virtual sPtr<pigData> cdr() { return _cdr; }
  virtual sPtr<stdString> get_str();   /* "(car . …)" 風 repr(cdr が未解決でもブロックしない) */
  virtual sPtr<stdString> print();     /* 継続(カーネル名.promise)なら cdr を辿って実値まで解決 */
  /* NB(2026-07-29 メモ 1.): 旧 set_module_tag/get_module_tag は廃止。カーネルは car の
   *   カーネル名文字列そのものが運ぶ (pig_module_from_name(car) で読む・pigData 無改変)。 */
protected:
  sPtr<pigData> _car;
  sPtr<pigData> _cdr;
};

/* 計算結果キャッシュのハンドル(opaque)。hashkey=「何の演算結果か」、path=キャッシュファイル。
 * get_str() はパスを返す → 下流 pigfAgent が C_ARG_PATH(入力キャッシュパス)として送れる。
 * mesh バイナリ等「値として読み込まない」結果に使う(テキスト結果は pigDataString)。
 * NB: 例の「同一キャッシュを重複生成しない global dedup list」は未実装(TODO)。 */
class pigDataCache : public pigData {
public:
  pigDataCache(pHashKeyType hk, sPtr<stdString> path_, sPtr<pigInfo> i = thNULL)
    : pigData(i), hashkey(hk), path(path_) {}
  virtual sPtr<stdString> get_str()       { return path; }
  virtual pHashKeyType    get_hashkey()   { return hashkey; }
  virtual int             is_cache()      { return 1; }
  sPtr<stdString>         get_path()      { return path; }
  /* ★ 2026-08-19: 旧 type_name() override (先頭 D_META の 4CC → 型名) は **撤去**した。
   *   cache は基底の type_name() = 0 (型を名乗らない)。理由:
   *     ① routing は型スタンプ (type_stamp) だけを見るようになり、呼ぶ人が居なくなった
   *     ② 4CC → 型の逆引きは「同じ形式を複数モジュールが名乗ったら先勝ち」で**嘘をつく**
   *        (形式を共有するのは正常。例: geogram と manifold はどちらも "MFM3")
   *     ③ 基底 type_name() の意味は **型名** (本文が名乗る実装型) で、cache だけ形式名を返すと
   *        同じ virtual に 2 つの意味が同居する
   *   形式 (4CC) を主語にした診断は describe() が担い、そこは「その形式を読めるモジュールが
   *   出せる型を**全部**」列挙する (先勝ちで 1 つ選ばない)。 */
  /* ★ 2026-08-19: **型スタンプ** — 継続 pair の car に載るものと **同じ文字列**
   *   (単一型 "cg-mesh3d" / 多候補 CSV "d3-mesh3d,d3-cross2d")。プランナがこのノードの
   *   出力型として計画したもの。
   *   ★これを持たせないと **cold と warm で routing が変わる**: MISS では継続 pair に
   *   スタンプが載るのに、HIT では生のハンドルが返り、下流が 4CC から型を引き直していた
   *   (4CC が型と 1:1 でなくなった瞬間に別カーネルへ静かに流れる。実測で確認)。
   *   ★routing はこのスタンプ **だけ** を見る。**無ければエラー** (4CC へのフォールバックは
   *   しない — 「たまたま引けた型」で走ってしまうのを禁じる・ひさ設計 2026-08-19)。 */
  void                    set_type_stamp(sPtr<stdString> t) { typeStamp = t; }
  sPtr<stdString>         type_stamp() const { return typeStamp; }
  /* この cache が **ストリーム本体** (mesh 等) を指しているか。値キャッシュ (D_META "TEXT") と
   * 形式不明 (未書込 / ファイル不在) は 0。routing が「型スタンプが無いのは異常か」を判定するのに使う
   * (値キャッシュは型を持たないのが正常なので区別が要る)。非ブロッキング。 */
  int                     is_stream_cache();
  /* ★ #3433: 診断用の自己記述 ("形式 'NEF3'" / 引けたときは "形式 'NEF3' = 型 'nf-mesh3d'" /
   *   値キャッシュは "値 (形式 'TEXT')")。cache の識別は 4CC と型名の 2 段だが、**4CC は
   *   どのプロセスでも読める / 型名は per-binary** (その .so を積んだ実行体しか引けない) という
   *   非対称がある。呼び出し側がこれを毎回書くと判別ロジックが散るのでここへ閉じ込める。
   *   非ブロッキング (peek_tag は同期 read・未メタ/file 不在なら形式不明)。必ず何か返す。 */
  sPtr<stdString>         describe();

  /* ★ in-memory body(#3406, 2026-0727 メモ §2 / 2026-07-29 メモ 3. で抽象化完成):
   *   ディスク上のキャッシュ本文と等価なデータ(mesh/値)をメモリ上に持ち回る。
   *   同一キャッシュ = 同一データの前提(ファイル名 = 演算+引数ハッシュ)。
   *   **public はこの 3 つだけ** (ディスク I/O は専用 helper ptsDataCache が内包し、reader/writer
   *   の選択は pigCacheCodec テーブル。詳細は docs/mediator_design.md §3.2):
   *   - set_body : 計算結果をセットし保存 helper を即起動。呼んだ caller は暗黙 listener になり
   *                TSE_ASSERT (メタ書込済 = valid 成立・下流 attach 可) と TSE_DESTROY (完了) が届く。
   *                二重セットは無視 (同一キャッシュ = 同一データ)。
   *   - get_body : body 有→返す / helper 走行中→listen+sException (TSE_DESTROY で再評価) /
   *                未着手→読み出し helper を起動して listen+sException / CV_INVALID→thNULL
   *                (呼び元は is_valid で判定してエラー化)。
   *   - is_valid : ディスク上にキャッシュが存在しメタデータ保存済みか (本体書込中でも真)。
   *                未検査なら ::access で即検査。書込時は writer の TSE_ASSERT で成立。 */
  void          set_body(sPtr<pigData> d);
  /* ★ get_body は「欲しい型の候補リスト」1 実装のみ (2026-08-12 統一・docs/cross_module_conversion_design.md)。
   *   消費者は自分が扱える型の候補を宣言し、cache は ① converted に居る候補を即返す (走行中は
   *   相乗り = 型ごと single-flight/dedup) ② 無ければ file 形式 (D_META 4CC / 値) × codec 表
   *   (reader_for) で**読める候補**を選んで reader を起動 ③ 読める候補が無ければ thNULL。
   *   「mesh cache に value は無い」等も特殊ケースでなくこの規則の帰結。
   *   単型版 = 候補 1 個・無引数版 = 候補 {"value"} (A_SAVE_BEGIN payload 判定用) の退化形。 */
  sPtr<pigData> get_body();
  /* ★ 2026-08-28 (ABI v12): 型名で欲しい型を並べる公開 overload
   *   (get_body(const char*) / get_body(const char* const*, int)) は **撤去**した。
   *   呼び手は配線版 1 つだけになり、型名は配線クラスの type_name() から出る。 */

  /* ★ 2026-08-28 (ひさ設計・ABI v12): **配線された本体クラスで実体化する** get_body。
   *   want = op の引数に配線された本体クラスの create_for_meta (pigOpEntry.h の pigWireFactoryFn)。
   *   この file の 4CC を渡して「そのクラスが受け取れるか」を訊き、受け取れるなら**返ってきた
   *   具象インスタンスの type_name()** を欲しい型として上の実装へ渡す。
   *   ★ 型名の一覧をモジュールから申告させる必要がない (旧 pgts_consumable_types / types_of_module) —
   *     欲しい型は op が d_cast するクラスそのもので、そのクラスだけが知っている。
   *   want == 0 (幾何を要求しない引数) は無変換の get_body() と同じ。
   *   受け取れない形式なら thNULL (呼び側が明示エラーにする)。 */
  sPtr<pigData> get_body(const pigWireClass* want);

private:
  /* 上の 2 つの実装本体。wire != 0 なら reader をその階層に固定する (配線経路)。 */
  sPtr<pigData> get_body_impl(const char* const* wantTypes, int n, const pigWireClass* wire);
public:
  int           is_valid();
  /* ★ A_SAVE_BEGIN 受信 (= 生産者の「メタ書込済」宣言) を planner 側ハンドルへ反映する。
   *   External 生産者は planner 側に writer helper がいないため、これが唯一の valid 成立経路
   *   (Internal は ptsDataCache SAVE の TSE_ASSERT が validState を直接立てるのと対)。
   *   ★leaf 生産者では ACT_START の HIT 判定 (is_valid) が MISS 時に CV_INVALID を焼き込み、
   *   その同一インスタンスが promise 経由で消費者へ渡る — ここで上書きしないと消費者の
   *   get_body が「cache not valid and no writer」で panic する (2026-08-12 leaf-gating)。 */
  void          mark_valid();
  /* ★ 保存/読み出しの helper が走り終えたか (2026-08-02 メモ §5.3)。
   *   is_valid : メタ書込済 = 下流が attach してよい (本体はまだ書込中でもよい)
   *   is_complete : **本体の書込/読込まで完了**。A_SAVE_DONE を出してよい条件。
   * 保存を始めていない (CV_UNKNOWN かつ helper 無し) 場合は完了とはみなさない。 */
  int           is_complete();
  /* ★ P4 (conv リスト化): 変換 helper (ptsDataCache_) が完了時に **自分の target_type のエントリ**へ
   *   結果を書き戻す口。共有の単一スロットを持たないので、異型の helper が並走しても互いに干渉しない。
   *   friend の ptsDataCache_ だけが呼ぶ (型名で該当エントリを引く・無ければ無視)。 */
  void          conv_set_body(const char* type, sPtr<pigData> b);   /* 変換 body を該当エントリへ */
  void          conv_finish(const char* type);                      /* 該当エントリの done=1・helper=null */
  /* ★ #3479: 実体化できなかった理由を該当エントリへ (conv_set_body と対。helper が完了時に書く)。 */
  void          conv_set_error(const char* type, sPtr<stdString> why);
  /* ★ #3479: 記録済みの失敗理由を集めて 1 本の文にする (無ければ thNULL)。
   *   get_body が thNULL を返した後、呼び手 (ptsGenericAgent) が「なぜ」を出すために引く。 */
  sPtr<stdString> load_error();
private:
  friend class ptsDataCache_;   /* 専用 helper (実装クラス) だけが body/validState を直接操作する */
  enum { CV_UNKNOWN = 0, CV_VALID = 1, CV_INVALID = 2 };
  pHashKeyType    hashkey;
  sPtr<stdString> path;
  int             validState = CV_UNKNOWN;   /* CV_VALID = メタ書込済 (下流 attach 可)。CV_INVALID = 不在/破損。 */
  /* ★ 2026-08-12 再設計 (ひさ): canonical/foreign の区別と `body` 単独変数を廃止し、**型ごとの
   *   body-list `converted[]` だけ**で持つ。エントリは一様で**位置に意味はない** (writer は isWriter
   *   フラグで引く。set_body は読みより先に来る運用なので writer_index() は常に 0 か -1 になるだけ)。
   *   「set_body で書かれた」か「file から読んだ」かは区別しない (streaming の原理)。
   *   writer 起動中は is_valid が「メタ書込済」を待つ。型ごとに single-flight。
   *   push_back のみ (erase しない) なので index は安定。value 本文 (type_name()==0) は type="value"。 */
  struct ConvEntry {
    sPtr<stdString> type;         /* 型名 ("value" / "cg-mesh3d" / ...) */
    sPtr<pigData>   body;         /* 本文。thNULL=未/失敗 */
    sPtr<tinyState> helper;       /* 走行中の reader/writer helper (待ち手の listen 先)。完了で thNULL */
    int             done = 0;     /* helper 終了印 (型ごと) */
    int             isWriter = 0; /* 1 = set_body の writer helper (is_valid/is_complete が参照) */
    /* ★ #3479: この型として実体化できなかった **理由**。従来は reader の errCode が
     *   TSE_RETURN の msg_int に載るだけで、body が null になった時点で捨てられていた
     *   (「形式は読めたが値が表現できない」と「codec が無い」を利用者が区別できなかった)。
     *   拒否した本人 (モジュールの decode) が書いた一文をここまで運ぶ。 */
    sPtr<stdString> why;
  };
  std::vector<ConvEntry> converted;
  int  conv_index(const char* type);    /* converted 中の型名一致エントリ index (無ければ -1) */
  int  conv_ensure(const char* type);   /* 無ければ push_back して index を返す (以後 index 不変) */
  int  writer_index();                  /* isWriter エントリの index (無ければ -1) */
  int  peek_tag(unsigned char out[4]);  /* file 先頭 D_META の 4CC を同期で覗く (1=あり/0=無し。
                                           値キャッシュも D_META "TEXT" — 判別は wire_tag_is_text)。
                                           ★成功のみメモ化 (同一 hash = 同一データなので 4CC は不変。
                                           失敗は「メタ未書込かも」なので焼き込まない = 毎回再読) */
  unsigned char tagMemo[4];             /* peek_tag の成功メモ (tagKnown=1 のとき有効) */
  int  tagKnown = 0;
  sPtr<stdString> typeStamp;            /* ★ 2026-08-19: 型スタンプ (set_type_stamp) */
};

class pigDataArray : public pigData {
public:
  pigDataArray(sPtr<pigInfo> i = thNULL) : pigData(i) {}
  virtual sPtr<pigDataArray> obt_array() { return thThis; }   /* 型取得ゲートウェイ (自分が配列) */
  /* ★ push は **エラー検査版**(2026-08-11 ひさ設計)。v がエラーなら配列に積まず v を返す。
   * pigDataControl(exit/return/break/continue)は pigDataError 派生なので、これで制御値も
   * 「配列に埋もれる」ことなく呼び元へ伝播する。正常時は thNULL を返す。
   * ★ 注意: v が未解決の遅延ノードだと is_error() が compact() ゲートで **yield(sException)** する。
   *   呼び元は compact 地点まで冪等(再入耐性)であること。
   * ★ **未起動の AST ノード**を積む場所(parser の arglist/vlist/文列・clone/capture_copy)は
   *   is_error() が compact()→start() を呼んでパース時/捕捉時に式を走らせてしまうので
   *   push_nocheck() を使う。 */
  sPtr<pigData> push(sPtr<pigData> v) {
    if (v->is_error()) return v;
    d.push(v);
    return thNULL;
  }
  /* 無検査 push: 上記のとおり「まだ起動してはいけないノード」専用。 */
  void push_nocheck(sPtr<pigData> v) { d.push(v); }
  int  length() { return d.length(); }
  virtual int get_bool() { return d.length() > 0; }   /* 非空=真 */
  virtual sPtr<stdString> get_str();
  virtual sPtr<stdString> print();       /* print(x) 用: 要素を print() で辿る(mesh 継続→解決しキャッシュパス) */
  virtual sPtr<stdString> serialize();   /* "[e1,e2,...]"(各要素 serialize) */
  virtual sPtr<pigData> clone() {        /* 要素を deep clone(要素が AST 式のことがある) */
    sPtr<pigDataArray> n = thNEW(pigDataArray,());
    for ( int i = 0 ; i < d.length() ; ++i ) n->push_nocheck(d[i]->clone());   /* AST 複製=起動しない */
    return n;
  }
  virtual sPtr<pigData> capture_copy() {  /* spine を deep copy・葉は共有(要素を再帰 capture_copy) */
    sPtr<pigDataArray> n = thNEW(pigDataArray,());
    for ( int i = 0 ; i < d.length() ; ++i ) n->push_nocheck(d[i]->capture_copy());   /* 同上 */
    return n;
  }
  virtual sPtr<pigData> get_ix(sPtr<pigData> key);   /* 要素はそのまま返す(観測で解決) */
  virtual sPtr<pigData> set_ix(sPtr<pigData> key, sPtr<pigData> val);
  /* 要素ごとの算術(+/- /  * / /)。右が配列なら要素ごと(長さ一致必須)、スカラーなら各要素にブロードキャスト。
   * `[a,b]+[c,d]`=[a+c,b+d] / `[a,b]*s`=[a*s,b*s]。ネスト配列は要素の add/... が再帰。 */
  virtual sPtr<pigData> add(sPtr<pigData> o);
  virtual sPtr<pigData> sub(sPtr<pigData> o);
  virtual sPtr<pigData> mul(sPtr<pigData> o);
  virtual sPtr<pigData> div(sPtr<pigData> o);
protected:
  sArray<sPtr<pigData> > d;
};

class pigDataHash : public pigData {
public:
  pigDataHash(sPtr<pigInfo> i = thNULL) : pigData(i) {}
  virtual sPtr<pigDataHash> obt_hash() { return thThis; }   /* 型取得ゲートウェイ (自分がハッシュ) */
  int length() { return keys.length(); }                 /* 要素(キー)数。length() ビルトイン用 */
  virtual int get_bool() { return keys.length() > 0; }   /* 非空=真 */
  virtual sPtr<stdString> get_str();                 /* キーソートの正規形 */
  virtual sPtr<stdString> print();       /* print(x) 用: 値を print() で辿る(mesh 継続→解決) */
  virtual sPtr<stdString> serialize();   /* "{\"k\":v,...}"(キーソート, 値 serialize) */
  virtual sPtr<pigData> get_ix(sPtr<pigData> key);
  virtual sPtr<pigData> set_ix(sPtr<pigData> key, sPtr<pigData> val);
  virtual sPtr<pigData> clone() {        /* キーは共有(不変)、値は deep clone */
    sPtr<pigDataHash> n = thNEW(pigDataHash,());
    for ( int i = 0 ; i < keys.length() ; ++i ) { n->keys.push(keys[i]); n->vals.push(vals[i]->clone()); }
    return n;
  }
  virtual sPtr<pigData> capture_copy() {  /* キーは共有・値は再帰 capture_copy(spine deep copy・葉共有) */
    sPtr<pigDataHash> n = thNEW(pigDataHash,());
    for ( int i = 0 ; i < keys.length() ; ++i ) { n->keys.push(keys[i]); n->vals.push(vals[i]->capture_copy()); }
    return n;
  }
protected:
  int find(sPtr<stdString> key);
  sArray<sPtr<stdString> > keys;
  sArray<sPtr<pigData> >   vals;
};

/* 入力ファイル参照(import 用)。パス式を包み、get_hashkey() を **(path,size,mtime) の安いゲート**
 * にする(stat のみ・内容は読まない。get_str/serialize はパスを委譲)。これを import の引数に被せると、
 * プランナーの compute_arg_hash がこのハッシュでキャッシュキーを作る(ファイルが変われば mtime/size
 * 前進で別キャッシュ=再 import、不変なら HIT)。通常編集の誤りは false-miss 側(無駄な再計算)に倒れ、
 * 真の false-hit は mtime を意図的に据え置く狭い運用のみ。agent へはパス文字列が渡り(serialize=パス)、
 * agent 側が read_polygon_mesh で読む。値ノード(compact=thThis)。 */
class pigDataFileRef : public pigData {
public:
  pigDataFileRef(sPtr<pigData> path_, sPtr<pigInfo> i = thNULL) : pigData(i), pathExpr(path_) {}
  virtual sPtr<stdString> get_str()   { return pathExpr->get_str(); }     /* パス(agent が読む) */
  virtual sPtr<stdString> serialize() { return pathExpr->serialize(); }   /* 文字列リテラルとして */
  virtual pHashKeyType    get_hashkey();   /* ファイル内容の FNV-1a/64(pigData.cpp) */
  virtual sPtr<pigData>   clone()     { return thNEW(pigDataFileRef,(pathExpr->clone())); }
protected:
  sPtr<pigData> pathExpr;
};

/* 実行環境: 変数束縛 + 親チェーン。未定義アクセスはエラー */
class pigEnvironment : public stdObject {
public:
  pigEnvironment(sPtr<pigEnvironment> p = thNULL) : parent(p) {}
  sPtr<pigData> def_var(sPtr<stdString> name, sPtr<pigData> val);
  sPtr<pigData> get_var(sPtr<stdString> name);
  /* ★ #3555 段2: 名前が **束縛されているか**だけを問う (親チェーンを辿る)。
   *   ⚠ get_var は未定義のとき pigDataError を返すので、「未定義」と「エラー値が入っている」の
   *     区別が付かない。予約変数の既定へ倒す判断はこちらで行う (エラー値は素通しして伝播させる)。 */
  int           has_var(sPtr<stdString> name);
  sPtr<pigData> set_var(sPtr<stdString> name, sPtr<pigData> val);
  /* この env から根まで辿り、可視束縛(name→値)を frozen フレームへ値コピー(外側→内側の順=内側 shadow 維持)。
   * クロージャの**値捕捉**に使う(pigDataLambdaExpr::_start): 生成時点の自由変数値を凍結し、以後の
   * set_var/def_var の書き換えを遮断する。
   * ★ #3450 (2026-08-29) で frozen は **親を持たない** 完全なスナップショットになった
   *   (env ⇄ lambda の参照循環を切るため)。⇒ 生成時まだ未束縛の名前は apply 時に遅延解決されず
   *   「未定義変数」の明示エラーになる。再帰は自己適用 f(f,x) で書く。詳細は pigfOps.cpp の
   *   pigDataLambdaExpr::_start。 */
  void snapshot_into(sPtr<pigEnvironment> frozen);
  /* ★★ #3482: **いま自分を囲んでいる try** (thNULL 可)。
   *   ・ try の帰属は **動的** (定義地点ではなく呼び出し元) — ヘルパ lambda を try の外で定義して
   *     中で呼ぶのが普通の書き方なので、レキシカルだと効かない (C++ の例外と同じ直感)。
   *   ・ get_try() は **親チェーンを辿らない O(1) 読み出し**。#3450 で frozen env の親リンクを
   *     切った (参照循環) ので、辿る実装は復活させられない。
   *   ⇒ **env を生成する側が必ず引き継ぐ** (pigfSequence / pigfAsync / pigfApply の 3 箇所。
   *     pigfFunction は親の env をそのまま共有するので何もしなくてよい)。
   * ⚠ frozen (クロージャ捕捉) env には **持たせない** — snapshot_into は束縛だけを写す。
   *
   * ★★ #3564 (2026-09-20): **生ポインタをやめて sPtr にした**。
   *   ⚠⚠ ここには長らく「sPtr にすると TryCatch → statement2 の env → TryCatch で循環し、
   *     参照カウントでは永遠に落ちない」と *理由つきで* 書いてあった。その前提は
   *     **#3450 (2026-08-29) の時点で外れていた** — try を持つ env は全部 pigfFunction_ 系の
   *     状態機械のメンバで、共通 FIN が @env = thNULL@ で明示的に手放すため、env→try の辺は
   *     参照カウント任せではなく **決定的に消える** (さらに #3564 で pigfTryCatch の FIN に
   *     @env->set_try(thNULL)@ も足した = 引き継ぎ先の env が残っていても切れる)。
   *     ★ 理由つきで正しそうに書いてあるコメントほど、前提が動いても疑われずに残る。
   *   ★ 直した動機は @__get()@ (利用禁止・ひさ 2026-09-20) を木から無くすことと、
   *     **@flush()@ / @error()@ の null 検査を本物にする**こと。生ポインタでは
   *     「一度も入っていない」しか拾えず、**「入ったが畳まれた」は素通り**していた
   *     (借りたポインタ越しの操作 = #3560 と同じ家系)。
   *   ⚠ 定義は pigData.cpp — この時点で @pigDataTryCatch@ はまだ不完全型なので、
   *     @sPtr@ の代入/破棄 (relref) をヘッダに書けない。 */
  void                    set_try(sPtr<pigDataTryCatch> t);
  sPtr<pigDataTryCatch>   get_try();
  ~pigEnvironment();   /* ⚠ tryPtr の破棄に完全型が要るので out-of-line (上の ⚠) */
protected:
  int find_local(sPtr<stdString> name);
  sArray<sPtr<stdString> > names;
  sArray<sPtr<pigData> >   values;
  sPtr<pigEnvironment>     parent;
  sPtr<pigDataTryCatch>    tryPtr;   /* ★ #3564: 強参照。thNULL = try の外 */
};

/*
 * pigDataDelay — 遅延ノードの基底。pigDataOperator / (後段)pigDataFunction の共通祖先。
 * pigData の public virtual を全て compact() でゲートウェイ(触れた瞬間に解決)。
 */
class pigDataDelay : public pigData {
public:
  pigDataDelay(sPtr<pigInfo> i = thNULL) : pigData(i), start_flag(0) {}

  virtual int is_error()          { return compact()->is_error(); }
  virtual INTEGER64 get_int()     { return compact()->get_int(); }
  virtual double    get_flt()     { return compact()->get_flt(); }
  virtual int       get_bool()    { return compact()->get_bool(); }
  virtual sPtr<stdString> get_str(){ return compact()->get_str(); }
  virtual sPtr<stdString> print()  { return compact()->print(); }          /* ゲートウェイ(未解決は yield) */
  virtual sPtr<stdString> serialize(){ return compact()->serialize(); }   /* ゲートウェイ */
  virtual pHashKeyType get_hashkey(){ return compact()->get_hashkey(); }
  virtual sPtr<pigDataArray> obt_array() { return compact()->obt_array(); }   /* 型取得もゲートウェイ */
  virtual sPtr<pigDataHash>  obt_hash()  { return compact()->obt_hash(); }

#define PG1(n)   virtual sPtr<pigData> n(sPtr<pigData> o) { return compact()->n(o); }
#define PG0(n)   virtual sPtr<pigData> n() { return compact()->n(); }
#define PGP(n,T) virtual sPtr<pigData> n(T x) { return compact()->n(x); }
  PG1(add) PG1(sub) PG1(mul) PG1(div) PG1(rem)
  PG1(band) PG1(bor) PG1(bxor) PG0(bnot)
  PG1(aand) PG1(aor) PG1(axor) PG0(anot) PG1(ashl) PG1(ashr)
  PG1(eq) PG1(ne) PG1(lt) PG1(gt) PG1(le) PG1(ge)
  PG1(get_ix)
  virtual sPtr<pigData> set_ix(sPtr<pigData> k, sPtr<pigData> v) { return compact()->set_ix(k, v); }
  PGP(p_add,INTEGER64) PGP(p_add,double) PGP(p_add,sPtr<stdString>)
  PGP(p_sub,INTEGER64) PGP(p_sub,double)
  PGP(p_mul,INTEGER64) PGP(p_mul,double)
  PGP(p_div,INTEGER64) PGP(p_div,double)
  PGP(p_rem,INTEGER64)
  PGP(p_band,int) PGP(p_bor,int) PGP(p_bxor,int)
  PGP(p_aand,INTEGER64) PGP(p_aor,INTEGER64) PGP(p_axor,INTEGER64)
  PGP(p_ashl,INTEGER64) PGP(p_ashr,INTEGER64)
  PGP(p_eq,sPtr<pigData>) PGP(p_ne,sPtr<pigData>) PGP(p_lt,sPtr<pigData>)
  PGP(p_gt,sPtr<pigData>) PGP(p_le,sPtr<pigData>) PGP(p_ge,sPtr<pigData>)
#undef PG1
#undef PG0
#undef PGP

  virtual sPtr<pigData> car()             { return compact()->car(); }
  virtual sPtr<pigData> cdr()             { return compact()->cdr(); }
  virtual int           is_cache()        { return compact()->is_cache(); }
  /* ★ 「どう書かれたか」も解決済み下位へ委譲 (is_cache と同じ理由)。これが無いと
   *   map / lambda 由来の遅延ノードが数でも is_int / is_flt がともに偽になり、
   *   ⚠ **process 経路 (値がテキスト化されて素の値に戻る) でだけ正しく、in-proc で嘘**
   *   という obt_array() が踏んだのと同型の穴になる。 */
  virtual int           is_int()          { return compact()->is_int(); }
  virtual int           is_flt()          { return compact()->is_flt(); }
  /* ★ rev4: 解決済み下位 (pigDataCache 等) へ型を委譲 (is_cache と対称)。これが無いと cache に解決した
   *   遅延ノードが is_cache=1 なのに type_name が基底に落ちて typeless になる (arg_type_set が取りこぼす)。
   *   ★ P2e: get_module_tag の委譲は撤去 (カーネル軸 API 廃止・型軸 type_name のみ)。 */
  virtual const char*   type_name()       { return compact()->type_name(); }

  using pigData::p_cmp;
  virtual int cmp(sPtr<pigData> o)        { return compact()->cmp(o); }
  virtual int p_cmp(INTEGER64 dd)         { return compact()->p_cmp(dd); }
  virtual int p_cmp(double dd)            { return compact()->p_cmp(dd); }
  virtual int p_cmp(sPtr<stdString> dd)   { return compact()->p_cmp(dd); }

  /* helper(tinyState)が非同期に結果を確定した時に呼ぶ。result をセットし、helper の
   * listener へ TSE_UPDATED を invoke(= compact で yield した caller を再起動)。helper が
   * 生存したまま結果を返す mid-life 継続(pigfAgent)でも起こせる。
   * flag=1 は「既に result があれば上書きしない」(pigfAgent の delayed 持ち越し用)。 */
  void set_result(sPtr<pigData> r, int flag = 0);

  virtual sPtr<pigData> compact(int depth = PIG_COMPACT_MAX);   /* 不動点解決(depth で上限) */
  virtual int           is_compact();
  /* ★ 上流を止める(基底 pigData::destroy の実装)。helper を destroy し、委譲先(result: varref→
   * 束縛ノード、sequence→最終文ノード等)へ再帰する = compact() が辿るのと同じ連鎖。
   * result が値ノード(継続 pigDataPair 等)なら基底の no-op で止まる。 */
  virtual void          destroy();
protected:
  /* _start を先に実行し、**正常完了してから** start_flag を立てる。_start が yield(sException)した
   * 場合は flag が立たないので、再走で _start を再実行できる(非同期引数を読む同期演算子=Eq/Export
   * 等の _start が compact ゲートで yield しても再開可能になる)。_start は冪等であること。
   * (helper を起こす pigDataFunction::_start は yield しないので二重起動しない。循環は compact の
   *  depth 上限が捕捉。) */
  void start() { if (!start_flag) { _start(); start_flag = 1; } }
  void preprocess();                    /* start; result 未確定 & helper 有 → caller listen + sException yield */
  virtual void _start() = 0;            /* 同期演算子は result 即セット / 非同期は helper を起動 */

  unsigned start_flag : 1;
  sPtr<pigData>   result;
  sPtr<tinyState> helper;               /* 非同期 helper(同期演算子では thNULL) */
};

/* 外部解決される遅延ノード(継続 promise)。helper(= pigfAgent 等)が後から
 * set_result(値) すると、set_result 内の invoke_listen(TSE_UPDATED) で compact 待ちの
 * caller が起きる。_start は何もしない(helper 起動は呼び側が管理)。
 * pigfAgent が ("delayed" . promise) の cdr に入れて非ブロッキング継続を実現する。 */
class pigDataPromise : public pigDataDelay {
public:
  pigDataPromise(sPtr<tinyState> _helper, sPtr<pigInfo> i = thNULL) : pigDataDelay(i) {
    helper = _helper;
  }
protected:
  virtual void _start() {}   /* 自動起動しない(外部 set_result で解決) */
};

/* 軽演算子: args を持ち、_start で同期的に畳む(helper を使わない=遅延しない) */
class pigDataOperator : public pigDataDelay {
public:
  pigDataOperator(sPtr<pigInfo> i = thNULL) : pigDataDelay(i), argsDestroyed(0) {}
  void pushArg(sPtr<pigData> a) { args.push(a); }
  int  argc() { return args.length(); }
  sPtr<pigData> arg(int ix) { return args[ix]; }
  /* 演算子名(pigfAgent が C_OP で送り、cgatsAgent が dispatch する)。front 由来。 */
  void set_op_name(sPtr<stdString> n) { op_name = n; }
  sPtr<stdString> get_op_name() { return op_name; }
  /* ★ #3467: `module::op(...)` のモジュール指名。**文字列に評価される式**を持つ
   *   (リテラル "occt" も 変数 k も同じ形。評価は decide_out_module が compact して行う)。
   *   ⚠ **args には入れない**。入れると compute_arg_hash の argHashes に混ざり、`""::op` の
   *     キーが `op` と変わってしまう (#3467 の不変条件「キーは書かれ方でなく実際に何が
   *     走ったかで決まる」が壊れる)。指名は routing の**絞り込み**であって引数ではない。
   *   未指定は thNULL = 従来どおり planner が決める。 */
  void set_module_expr(sPtr<pigData> m) { module_expr = m; }
  sPtr<pigData> get_module_expr() { return module_expr; }
  /* この演算の出力がキャッシュ(mesh 等のハンドル)か値(インライン)か。pigfAgent が HIT/MISS とも
   * 一貫して結果型を決めるのに使う(HIT は agent 不起動なので planner 単独で判断が要る)。
   * 将来はパーサ/dispatch が op シグネチャから設定。既定 0 = 値(インライン)。 */
  void set_out_cache(int c) { out_cache = c; }
  int  get_out_cache() { return out_cache; }
  /* clean() — result 確定後に args/helper のポインタを切り、生成元 DAG(巨大インライン配列等)を解放。
   * result は保持(観測でこれを返す)。clone は未評価テンプレートに対してのみ起こる(評価済みノードは
   * 再 clone されない)ので args を落として安全。同期 op は _start 末尾、agent helper は FIN で front->clean()。 */
  virtual void clean() { args.length(0); helper = thNULL; }
  /* ★★ #3541②: **撤収を引数へ転送する**。
   *   ⚠⚠ 基底 @c pigDataDelay::destroy() は helper と result しか辿らない。演算子ノードは
   *     同期のものだと helper が thNULL なので、@c print(..., system("sleep 30")) のように
   *     **引数側に走行中の helper がぶら下がっている**と、その helper が **走査経路に入らず**
   *     撤収が届かない ⇒ 子プロセスが最後まで走り続ける (実測 3 機・#3541②)。
   *   ★ 転送してよい根拠: 演算子は @c spark_args() で **全 args を必ず起動する**
   *     (#3419「1 回目の compact で全ての引数の計算が起動される = 並列」)。
   *     *起動しているのは自分* なので、止めるのも自分の責任 — @c pigfApply が実引数へ
   *     転送しているのとまったく同じ理屈。
   *   ⚠ 逐次に意味がある op (@c pigfSequence など) は **helper 側**が自分で選んで転送する。
   *     ここで畳むのは「全部起動する」演算子ノードだけ。
   *   ⚠ 短絡評価する演算子は **存在しない** (パーサの mk_logic に「短絡評価はしない(両辺評価)」
   *     と明記。2026-09-15 に 23 個の演算子クラスを確認) ⇒ 全 args へ送って過剰にならない。
   *   ⚠ 1 度だけ送る (DAG は共有されるので、環で無限再帰しないため)。 */
  virtual void destroy();
  /* ★ #3419 (ひさ設計 2026-08-24): **引数を並列に解決し始める**。
   * 全 args を compact し、未解決の yield (sException) は**握って次の引数へ進み**、最後に投げ直す。
   * ⇒ 1 回目の compact で **全ての引数の計算が起動される** = 並列。
   *   `preprocess()` は throw の**前に** caller を helper の listener に登録し、listen は加算なので、
   *   呼び手は全 helper の listener になり、どれかの完了で起こされる (前進が保証される)。
   * ★ これが「早く、解決された引数が欲しい」の直接の実装。**起動の入口は _start ただ 1 つ**になる。
   *
   * 返り値: **左優先で最初に確定したエラー** (無ければ thNULL)。呼び手はこれを result にして
   *   早期リターンできる (どうせ捨てる右側の計算を起動しない)。
   *   ⚠ 「それより左が全て解決済み」のときだけ返すので、**どのエラーが報告されるかは決定的**。
   *
   * ⚠ 逐次に意味がある op は呼んではいけない (pigfSequence / Hash の兄弟キー参照など)。 */
  sPtr<pigData> spark_args();
protected:
  /* clone 共通: args を deep clone して n に積み、op_name/out_cache を引き継ぐ(n は新ノード=
   * 未評価。delay 状態 result/start_flag/helper はコピーしない)。各 operator 派生の clone から呼ぶ。 */
  sPtr<pigData> copy_to(sPtr<pigDataOperator> n) {
    for ( int i = 0 ; i < args.length() ; ++i ) n->pushArg(args[i]->clone());
    n->op_name = op_name;
    n->out_cache = out_cache;
    /* ★ #3467: モジュール指名も clone する。**deep clone** — 変数形 (k::box) はループ本体の
     *   再評価ごとに別の値へ解決されるので、未評価テンプレートとして複製しないと
     *   `for k in [...]` の 2 周目以降が 1 周目の解決結果を再利用してしまう。 */
    n->module_expr = module_expr.is_notNull() ? module_expr->clone() : sPtr<pigData>(thNULL);
    n->info = info;   /* ソース位置(file,line)を clone に引き継ぐ(ループ/lambda 本体の再評価でも保つ) */
    return n;
  }
  sArray<sPtr<pigData> > args;
  sPtr<stdString> op_name;
  sPtr<pigData> module_expr;   /* ★ #3467: `module::op` の指名 (文字列に評価される式)。thNULL = 未指定 */
  int out_cache = 0;
  unsigned argsDestroyed : 1;   /* ★ #3541②: destroy の転送は 1 度だけ */
};

/* 演算子ノード生成: pigDataOperator<Name> = 遅延ノード(args を畳む/単項適用)。
 * クラス構造は全演算共通(差は _start のみ)。_start 本体は pigData.cpp の同名マクロで定義。 */
#define PIG_DEFOP(Name) \
  class pigDataOperator##Name : public pigDataOperator { \
  public: \
    pigDataOperator##Name(sPtr<pigInfo> i = thNULL) : pigDataOperator(i) {} \
    virtual sPtr<pigData> clone() { return copy_to(thNEW(pigDataOperator##Name,())); } \
  protected: \
    virtual void _start(); \
  };
PIG_DEFOP(Add)  PIG_DEFOP(Sub)  PIG_DEFOP(Mul)  PIG_DEFOP(Div)  PIG_DEFOP(Rem)   /* 四則 */
PIG_DEFOP(Band) PIG_DEFOP(Bor)  PIG_DEFOP(Bxor) PIG_DEFOP(Bnot)                  /* 論理 */
PIG_DEFOP(Aand) PIG_DEFOP(Aor)  PIG_DEFOP(Axor) PIG_DEFOP(Anot)
PIG_DEFOP(Ashl) PIG_DEFOP(Ashr)                                                 /* ビット */
PIG_DEFOP(Eq)   PIG_DEFOP(Ne)   PIG_DEFOP(Lt)   PIG_DEFOP(Gt)
PIG_DEFOP(Le)   PIG_DEFOP(Ge)                                                    /* 比較 */
#undef PIG_DEFOP

/* srava 言語の I/O シンク演算子(export / export_async / flush)は **srava 固有**なので、命名規約
 * (pigcg... = 非 tinyState 系で pigData 子孫の言語固有クラス)に従い pigcgOperators.h へ分離した。
 * pigData(データ層)はこれらを知らない。print(値の表示・言語非依存寄り)は下記 pigData 側に残す。 */

/* 配列構築 `[e0, e1, ...]`(式中の角括弧)= **可変長引数を取って配列を作る関数**。
 * 値リテラル(pigDataArray)ではなく演算子にすることで、_start(compact)時に各要素を
 * **その地点の env で評価**して値配列を作る。これにより `translate(m,[d,0,0])` のような
 * インライン配列内の varref が、スカラ varref 引数と同じ評価経路に乗り、正しい env で解決される。
 * (VALUE モード=ワイヤ値の配列は varref を含まない実値なので従来どおり pigDataArray のまま。) */
class pigDataOperatorArray : public pigDataOperator {
public:
  pigDataOperatorArray(sPtr<pigInfo> i = thNULL) : pigDataOperator(i) {}
  virtual sPtr<pigData> clone() { return copy_to(thNEW(pigDataOperatorArray,())); }
protected:
  virtual void _start();
};

/* (par は撤去 — 配列リテラル [a,b,c]=pigDataOperatorArray が要素を並列評価するので等価。) */

/* ハッシュ構築 `{k0:e0, k1:e1, ...}`(式中の波括弧)= 配列構築と同様に**演算子**。
 * args は [key0, val0, key1, val1, ...] のインターリーブ(key は pigDataString リテラル)。
 * _start(compact)時に各値式を**その地点の env で評価**して pigDataHash を作る。
 * (VALUE モードのワイヤ値ハッシュは実値なので従来どおり pigDataHash のまま。) */
class pigDataOperatorHash : public pigDataOperator {
public:
  pigDataOperatorHash(sPtr<pigInfo> i = thNULL) : pigDataOperator(i) {}
  virtual sPtr<pigData> clone() { return copy_to(thNEW(pigDataOperatorHash,())); }
protected:
  virtual void _start();
};

/* 添字/メンバ参照 a[ix] / a.key。args[0]=被参照(array/hash 等)、args[1]=キー。
 * _start(同期): result = args[0]->get_ix(args[1])(遅延ノードは get_ix ゲートで解決)。 */
class pigDataOperatorIndex : public pigDataOperator {
public:
  pigDataOperatorIndex(sPtr<pigInfo> i = thNULL) : pigDataOperator(i) {}
  virtual sPtr<pigData> clone() { return copy_to(thNEW(pigDataOperatorIndex,())); }
protected:
  virtual void _start();
};

/* a[key] = val / a.key = val — 添字/メンバへの代入。args[0]=被参照(array/hash)、args[1]=キー、
 * args[2]=値。_start: 被参照を compact し set_ix(key, val) で破壊的代入(val は評価地点で compact=
 * ループ変数を捕捉)。代入式の値=代入した値。`screw[i] = …` / `h.key = …`。 */
class pigDataOperatorSetIndex : public pigDataOperator {
public:
  pigDataOperatorSetIndex(sPtr<pigInfo> i = thNULL) : pigDataOperator(i) {}
  virtual sPtr<pigData> clone() { return copy_to(thNEW(pigDataOperatorSetIndex,())); }
protected:
  virtual void _start();
};

/* length(x) — array/hash の要素数(整数)を返す planner 側 op(agent 不要)。
 * _start(同期): args[0] を compact し、pigDataArray なら length()、pigDataHash なら キー数、
 * それ以外は エラー(数値/文字列等は要素数の概念がない)。値専用なので yield しない。 */
class pigDataOperatorLength : public pigDataOperator {
public:
  pigDataOperatorLength(sPtr<pigInfo> i = thNULL) : pigDataOperator(i) {}
  virtual sPtr<pigData> clone() { return copy_to(thNEW(pigDataOperatorLength,())); }
protected:
  virtual void _start();
};

/* float(x) — 値を浮動小数へ変換する planner 側 op(agent 不要)。
 * _start(同期): args[0] を compact し get_flt()(文字列は数値としてパース・整数は昇格・浮動小数はそのまま)。
 * 配列/ハッシュはスカラでないためエラー。値専用なので yield しない。 */
class pigDataOperatorToFloat : public pigDataOperator {
public:
  pigDataOperatorToFloat(sPtr<pigInfo> i = thNULL) : pigDataOperator(i) {}
  virtual sPtr<pigData> clone() { return copy_to(thNEW(pigDataOperatorToFloat,())); }
protected:
  virtual void _start();
};

/* int(x) — 値を整数へ変換する planner 側 op(agent 不要)。
 * _start(同期): args[0] を compact し get_int()(文字列は数値としてパース・浮動小数は 0 方向へ切り捨て・整数はそのまま)。
 * 配列/ハッシュはスカラでないためエラー。値専用なので yield しない。 */
class pigDataOperatorToInt : public pigDataOperator {
public:
  pigDataOperatorToInt(sPtr<pigInfo> i = thNULL) : pigDataOperator(i) {}
  virtual sPtr<pigData> clone() { return copy_to(thNEW(pigDataOperatorToInt,())); }
protected:
  virtual void _start();
};

/* 初等関数(sin/cos/sqrt/atan2/pow/...) — planner 側 op(agent 不要・CGAL 非依存・libm ラップ)。
 * **ベクトル化**: 引数が配列なら要素ごと(スカラはブロードキャスト・配列同士は zip)。角度はラジアン。
 * 関数名は op_name に入れて _start で dispatch。結果は浮動小数(配列)。 */
class pigDataOperatorMath : public pigDataOperator {
public:
  pigDataOperatorMath(sPtr<pigInfo> i = thNULL) : pigDataOperator(i) {}
  virtual sPtr<pigData> clone() { return copy_to(thNEW(pigDataOperatorMath,())); }
protected:
  virtual void _start();
};

/* concat(a, b, ...) — 配列連結。各引数を compact し、配列ならその要素を、配列でなければ
 * その値 1 個を新配列に積む。planner 側 op(agent 不要)。`concat([1,2],[3,4])`=[1,2,3,4]、
 * `concat(a, 5)`=a の要素 + 5。エラー引数はそのまま伝播。 */
class pigDataOperatorConcat : public pigDataOperator {
public:
  pigDataOperatorConcat(sPtr<pigInfo> i = thNULL) : pigDataOperator(i) {}
  virtual sPtr<pigData> clone() { return copy_to(thNEW(pigDataOperatorConcat,())); }
protected:
  virtual void _start();
};

/* transpose(arr) — 矩形の「配列の配列」を入替: `[n][m]→[m][n]`。planner 側 op(agent 不要)。
 * 座標列 `[xs, ys, zs]` ↔ 点列 `[[x,y,z],…]` の変換に使う(curve を vectorized で列計算→点列化)。 */
class pigDataOperatorTranspose : public pigDataOperator {
public:
  pigDataOperatorTranspose(sPtr<pigInfo> i = thNULL) : pigDataOperator(i) {}
  virtual sPtr<pigData> clone() { return copy_to(thNEW(pigDataOperatorTranspose,())); }
protected:
  virtual void _start();
};

/* cumsum(arr) — 累積和 `[a0, a0+a1, a0+a1+a2, …]`(数値配列・浮動小数・同長)。数値積分の核。 */
class pigDataOperatorCumsum : public pigDataOperator {
public:
  pigDataOperatorCumsum(sPtr<pigInfo> i = thNULL) : pigDataOperator(i) {}
  virtual sPtr<pigData> clone() { return copy_to(thNEW(pigDataOperatorCumsum,())); }
protected:
  virtual void _start();
};

/* sum(arr) — 総和(数値配列→浮動小数)。planner 側 op。 */
class pigDataOperatorSum : public pigDataOperator {
public:
  pigDataOperatorSum(sPtr<pigInfo> i = thNULL) : pigDataOperator(i) {}
  virtual sPtr<pigData> clone() { return copy_to(thNEW(pigDataOperatorSum,())); }
protected:
  virtual void _start();
};

/* print(x, ...) — 各引数を print() ゲートウェイで解決して stdout に表示し、最後の値を返す。
 * planner 側 op(agent 不要)。_start は全 args の print() を連結(遅延/継続は yield→再走で解決)
 * してから 1 回だけ ::printf する。yield 時は start_flag が立たない設計なので、printf に到達する
 * のは「全引数が解決済み=もう yield しない」時だけ → 二重表示しない(冪等)。 */
class pigDataOperatorPrint : public pigDataOperator {
public:
  pigDataOperatorPrint(sPtr<pigInfo> i = thNULL) : pigDataOperator(i) {}
  virtual sPtr<pigData> clone() { return copy_to(thNEW(pigDataOperatorPrint,())); }
protected:
  virtual void _start();
};

/* module(so[, opts]) — **記述子の設定を上書きする** planner 側 op (docs §2.4)。指定されたモジュールが
 * 未ロードなら読み込む。★ロード順は変えない (上書き op なので)。
 *   引数 1 個:    module(so, {}) と同じ (ロードのみ・記述子は触らない)
 *   "off":        **実アンロード** (dlclose)。以後 module(so,{}) で読み直せる。文字列オプションは
 *                 これだけ ("on" は無い)。未ロード / 既に使われたモジュールへの off は明示エラー
 *   exec_default: "thread" / "process" (このモジュールの起動方式)
 *   priority:     既定カーネル選択順 (大=優先・**同点の勝敗は不定**)
 * 結果 = モジュール名。DEFAULT_OUTPUT 変数 / SRAVA_INPROC env の置換 (.so 化 Phase 4)。 */
/* ★★ #3595 (ひさ確定仕様 2026-09-24): **候補列の読み方は 1 本**。
 *   `use` / モジュール指名 / `module(配列)` は *同じ* 規則で列を読まなければならない —
 *   確定仕様の不変式 **「use module(L,{}) の候補列は use L と完全に同一 (違いはロードの副作用だけ)」**
 *   は、読み方が 2 つ在った時点で成立しないため。
 *   ⇒ 平坦化 (入れ子) ・ 穴の判定 (null / "" / 0) ・ 擬似モジュール (ハッシュ) の判定 ・
 *     要素のエラー伝播 ・ 深さの上限 は @pig_flatten_cand_array@ (pigfModuleAgent.cpp) に集約し、
 *     呼び手が違うのは **出てきた列をどう使うか** だけにする:
 *         use / 振り分け   穴は **落とす** (= 書かなかったのと同じ)
 *         module(配列)     穴は **その位置に残す** (出力が入力と 1:1 になる)
 * ⚠ ここで返すのは *読んだ形* だけ。名前をモジュール id へ引くのは呼び手の仕事 (registry が要る)。 */
struct pigCandItem {
  sPtr<pigData> val;      /* 要素の値 (compact 済み) */
  std::string   name;     /* 表示名 (擬似モジュールは "{pseudo}")・穴は空 */
  int           hole;     /* 1 = 穴 (null / "" / 0)。★ 空配列 [] は **穴ではない** (要素 0 個) */
  int           pseudo;   /* 1 = 擬似モジュール (ハッシュ) */
  pigCandItem() : hole(0), pseudo(0) {}
};
/* 平坦化した列を out へ積む。0 = 途中でエラー (*errv に値)・1 = 成功。 */
int pig_flatten_cand_array(sPtr<pigDataArray> ar, std::vector<pigCandItem> *out, sPtr<pigData> *errv);

class pigDataOperatorModule : public pigDataOperator {
public:
  pigDataOperatorModule(sPtr<pigInfo> i = thNULL) : pigDataOperator(i) {}
  virtual sPtr<pigData> clone() { return copy_to(thNEW(pigDataOperatorModule,())); }
protected:
  virtual void _start();
};

/* ★★ #3595: `use 式;` の **検査つきノード**。値はそのまま返す (恒等) が、走った時点で
 *   候補列を読み、**1 本もロード済みが無ければエラー**にする。
 *
 *   ★ なぜ use の行で見るか: 従来この検査は「候補列を実際に引くとき」= 最初の幾何 op の
 *     振り分けでしか走らなかったので、*その回に幾何 op が 1 つも無い*と列が丸ごと空振りして
 *     いても黙って通っていた (`use ["nosush"]; print("hello");`)。
 *   ★ op 実行時点の検査は **そのまま残す** (二重・確定仕様)。あちらは op 名まで知っているので
 *     「どれもその op を持たない」まで言える。
 *   ⚠⚠ 捕まえるのは「**丸ごと空振り**」だけ。`use ["cgal","nosush"]` は cgal が居れば **通る**
 *     (「未ロード名は飛ばす」は既存規則で変えない) ⇒ これは *綴り間違いの検出器ではない*。
 *   ★ 恒等なので `use` が DEF (var 相当・ブロックを抜けると外の値へ戻る) である性質は変わらない
 *     — 検査を代入の **右辺**に挟んであるだけで、束縛そのものは従来の pigfAssign が行う。
 *   ★ 実装は pigfModuleAgent.cpp — 列の読み方 (@read_cand_list@) と解決の規則が
 *     そこにあり、**振り分けと同じ判定**を使わないと二重帳簿になるため。 */
class pigDataOperatorUse : public pigDataOperator {
public:
  pigDataOperatorUse(sPtr<pigInfo> i = thNULL) : pigDataOperator(i) {}
  virtual sPtr<pigData> clone() { return copy_to(thNEW(pigDataOperatorUse,())); }
protected:
  virtual void _start();
};

/* module_loaded(so) — その .so が **いまロードされているか** (1/0)。
 * ★ 2026-08-28 (ひさ指摘): module(so,"off") が実アンロードになり、未ロードへの off は
 *   明示エラーになったので、落とす前に確かめる手段が要る。
 *   引数は module() と同じ書き方 (名前だけなら探索路から解決する)。 */
class pigDataOperatorModuleLoaded : public pigDataOperator {
public:
  pigDataOperatorModuleLoaded(sPtr<pigInfo> i = thNULL) : pigDataOperator(i) {}
  virtual sPtr<pigData> clone() { return copy_to(thNEW(pigDataOperatorModuleLoaded,())); }
protected:
  virtual void _start();
};

/* ★ #3477: 実行時の **内省** op 3 本。いまどのモジュールが載っていて、ある式がどのカーネルで
 * 走るのかを **スクリプトから問える**ようにする (カーネルが混ざる式のデバッグで、毎回ソースを
 * 読む必要があった)。エラー文にモジュール名を入れる #3475 が *事後*、この 3 本が *事前* の手段。
 * ★ 実装は pigfModuleAgent.cpp — sig の解析 (parse_sigline) と型スタンプの読み方
 *   (arg_type_set) がそこにあり、**dispatch と同じ判定**を使わないと内省の意味がないため。 */

/* modules() — いま載っているモジュールの **名前の配列** を dispatch 順 (priority 降順・同点は
 * 登録順) で返す。★ #3555 段5: 候補列 (USE_MODULES / `use`) へそのまま渡せる形が主用途なので、
 * 引数なしをこちらにした。`use modules();` は **挙動を変えない** (列の先勝ち = priority 最大)。
 * modules("priority") — 従来の "name:priority" 空白区切り文字列。module() の priority 指定が
 * 効いているかを目で見る用。⚠ こちらは番兵 "delayed" まで出す (#3477) が、配列版は候補に
 * なり得るものだけ (resolve_cands と同じ述語) なので **中身が一致しない**。
 * ★ 組込の "pig" は記述子を持つ実在のモジュールなので **両方に出る** (op が無く勝てないだけ)。 */
class pigDataOperatorModules : public pigDataOperator {
public:
  pigDataOperatorModules(sPtr<pigInfo> i = thNULL) : pigDataOperator(i) {}
  virtual sPtr<pigData> clone() { return copy_to(thNEW(pigDataOperatorModules,())); }
protected:
  virtual void _start();
};

/* ★★ #3595 の続き (ひさ 2026-09-24): `mod_only(a, b)` — **候補列の積**。
 *   a のうち **b に在る名前だけ**を、**a の順のまま**返す (= 優先順位を変えずに絞る)。
 *
 *   用途は「呼び手が敷いた列を、自分が対応しているカーネルへ絞り込む」:
 *       use [ mod_only(USE_MODULES, ["manifold","geogram"]), "cgal" ];
 *         … 呼び手が manifold/geogram を選んでいればそれに従い、そうでなければ cgal で解く
 *
 *   ★ 両辺とも候補列と **同じ規則**で読む (入れ子は平坦化 / スカラは 1 要素)。
 *   ⚠ **擬似モジュール (ハッシュ) と穴 (null / 0 / "") は落とす** — 名前で比較できないため。
 *     ⇒ 返るのは **文字列だけ**の配列。a の重複はそのまま残す (列は優先順位表であって集合ではない)。
 *   ★ 実装は pigfModuleAgent.cpp — 列の読み方 (@pig_flatten_cand_array@) を共有するため。 */
/* ★★ #3595 の続き: `mod_only(sup)` — **1 引数形** (ひさ 2026-09-24)。
 *   「いま解こうとしている列」∩ sup を返す。左辺の決め方が 1 引数形の肝:
 *
 *       呼び手が `use` で **宣言している**   → その列 (= USE_MODULES) ∩ sup
 *       呼び手が **何も宣言していない**      → **modules()** ∩ sup (= 載っているもの・dispatch 順)
 *
 *   ⇒ ライブラリ関数は `use mod_only(sup);` の **1 行**で宣言できる:
 *       ・呼び手の選択は **尊重**する
 *       ・選択が自分の対応範囲と **交差しなければエラー** (黙って独断で解かない)
 *       ・呼び手が何も言っていなければ **載っているもの**から選ぶ (既存のスクリプトはそのまま動く)
 *
 *   ★ 実装は 2 引数形と同じ (@pigDataOperatorModOnly@ と本体を共有)。違いは **左辺の補い方**だけ。
 *   ⚠ パーサが第 1 引数に **`USE_MODULES` の変数参照**を埋める (op からは env を引けないため)。
 *     その値に比較できる名前が 1 つも無ければ「宣言なし」とみなして modules() へ倒す
 *     (`""` / `null` / `0` / `[]` / 全部穴 が全部そこへ落ちる = 「未定義」を別扱いしない)。 */
class pigDataOperatorModOnlyUse : public pigDataOperator {
public:
  pigDataOperatorModOnlyUse(sPtr<pigInfo> i = thNULL) : pigDataOperator(i) {}
  virtual sPtr<pigData> clone() { return copy_to(thNEW(pigDataOperatorModOnlyUse,())); }
protected:
  virtual int   keep_pseudo() const { return 1; }   /* ★ 上の 2 引数形と同じ軸 */
  virtual void _start();
};

/* `mod_only_names(sup)` — 1 引数形。**左辺の補い方は @mod_only@ と同一**で、
 *   違うのは擬似を落とすことだけ (対称にする・ひさ 2026-09-25)。 */
class pigDataOperatorModOnlyNamesUse : public pigDataOperatorModOnlyUse {
public:
  pigDataOperatorModOnlyNamesUse(sPtr<pigInfo> i = thNULL) : pigDataOperatorModOnlyUse(i) {}
  virtual sPtr<pigData> clone() { return copy_to(thNEW(pigDataOperatorModOnlyNamesUse,())); }
protected:
  virtual int keep_pseudo() const { return 0; }
};

class pigDataOperatorModOnly : public pigDataOperator {
public:
  pigDataOperatorModOnly(sPtr<pigInfo> i = thNULL) : pigDataOperator(i) {}
  virtual sPtr<pigData> clone() { return copy_to(thNEW(pigDataOperatorModOnly,())); }
protected:
  /* ★ 擬似モジュールを **通すか**。`mod_only` は通す / `mod_only_names` は落とす。
   *   ⚠ 引数で渡す形は取れない (2 引数形の arity 検査とぶつかる)。メンバに持つ形も取れない
   *     (@clone()@ の @copy_to@ が運ばない) ⇒ **型で持つ** (ひさ 2026-09-25 ・ 派生で覆う)。 */
  virtual int   keep_pseudo() const { return 1; }
  virtual void _start();
};

/* ★★ #3595 の続き (ひさ 2026-09-25): `mod_only_names(a, b)` — **名前だけ**を返す形。
 *   @mod_only@ と積の取り方は同じで、違うのは **a 側の擬似モジュールも落とす**ことだけ。
 *   用途は「ライブラリ関数が呼び手の粒度を *意図的に無視して* 自分で制御したい」場合。
 *   ⇒ 既定は @mod_only@ (= 呼び手が選んだ粒度を尊重する) で、こちらは**明示的に降りる**口。
 *   ★ 名前は関数リファレンスの規約「同じ族だが約束が違うなら *元の op 名* + *_修飾*」に沿う。 */
class pigDataOperatorModOnlyNames : public pigDataOperatorModOnly {
public:
  pigDataOperatorModOnlyNames(sPtr<pigInfo> i = thNULL) : pigDataOperatorModOnly(i) {}
  virtual sPtr<pigData> clone() { return copy_to(thNEW(pigDataOperatorModOnlyNames,())); }
protected:
  virtual int keep_pseudo() const { return 0; }
};

/* type_of(x) — x の **幾何型名**を文字列で返す ("cg-mesh3d" / "mf-mesh3d" / "oc-brep3d" …)。
 * 値は "value"。★ 型が 1 つに絞れていない上流 (polymorphic) では **候補を CSV で全部**返す
 * (1 つ選んで見せると嘘になる)。式の途中でカーネルが変わったことを目で確認できる。 */
class pigDataOperatorTypeOf : public pigDataOperator {
public:
  pigDataOperatorTypeOf(sPtr<pigInfo> i = thNULL) : pigDataOperator(i) {}
  virtual sPtr<pigData> clone() { return copy_to(thNEW(pigDataOperatorTypeOf,())); }
protected:
  virtual void _start();
};

/* kind_of(x) — x の **値の種別**を文字列で返す (ひさ設計 2026-09-21)。planner 側 op (agent 不要)。
 *
 *   "int" / "float" / "string" / "array" / "hash" / "function" / "null" / "cache" / "unknown"
 *
 * ★★ **type_of() と軸が違う**。type_of は *幾何型* の軸 ("cg-mesh3d" / "pt-cloud2d" / "ref"・
 *   非幾何はすべて "value" に潰れる) で、こちらは *値の種別* の軸。2 つで直交する:
 *       kind_of(box(1,1,1))  = "cache"      type_of(box(1,1,1))  = "mf-mesh3d"
 *       kind_of(3)           = "int"        type_of(3)           = "value"
 *       kind_of(3.0)         = "float"      type_of(3.0)         = "value"
 *   ⇒ 「種別を見て → その型で誰が受けるかを見る」は kind_of → type_of → which() でたどれる。
 *
 * ★★ 幾何が "mesh" ではなく **"cache"** なのは (ひさ 2026-09-21)、そのハンドルが持つのは
 *   *計算結果への参照* であって mesh とは限らないから — 点群 (pt-cloud3d) も B-rep (oc-brep3d) も
 *   ボリューム (vd-grid3d) も export の戻り (ref) も、値の種別としては同じ「キャッシュハンドル」。
 *   **何のキャッシュか**は type_of() が答える。⇒ ここで型名を混ぜない。
 *
 * ⚠ **幾何は compact しない** (type_of と同じ理由)。内省したいだけなのに計算を走らせないため、
 *   型スタンプを継続 pair の car から非ブロッキングに読む。値は compact する — 種別を答えるには
 *   評価するしかなく、それは呼び側が承知のうえで訊いている。
 * ⚠ 該当が無ければ **"unknown"**。黙って "value" に寄せない (寄せると *新しい種別が増えたこと*
 *   が誰にも見えなくなる)。 */
class pigDataOperatorKindOf : public pigDataOperator {
public:
  pigDataOperatorKindOf(sPtr<pigInfo> i = thNULL) : pigDataOperator(i) {}
  virtual sPtr<pigData> clone() { return copy_to(thNEW(pigDataOperatorKindOf,())); }
protected:
  virtual void _start();
};

/* which(op [, intype...]) — その op を宣言しているモジュールを **priority 順に全部**返す
 * ("name:priority:sig" の空白区切り)。
 * ★ op 名だけでは答えが 1 つに決まらない (同じ op 名でも **引数の型でディスパッチ先が変わる**:
 *   cg-mesh3d が混じると manifold は候補から外れる — cgal は mf を食えるが manifold は cg を
 *   食えない)。⇒ 「候補を priority 順に全部返す」形にして **なぜそれが選ばれたか**まで読めるようにする。
 * 入力型を続けて渡すと、その型を **すべて**受理できる候補だけに絞る (type_of と組み合わせて
 *   「型を見て → その型で誰が受けるかを見る」で追える)。 */
class pigDataOperatorWhich : public pigDataOperator {
public:
  pigDataOperatorWhich(sPtr<pigInfo> i = thNULL) : pigDataOperator(i) {}
  virtual sPtr<pigData> clone() { return copy_to(thNEW(pigDataOperatorWhich,())); }
protected:
  virtual void _start();
};

/* <op>(...) — 55 個のハードコード builtin(box/union/…)以外の呼び出し名を、eval 時に
 * module op として実行するか、ローカル変数の lambda 適用として実行するかを選ぶ (#3452)。
 *   args[0] = module op 枝 (mk_call が組み立てる pigfModuleAgent ノード)
 *   args[1] = lambda 変数 apply 枝 (同じく mk_call が組み立てる varref+pigfApply ノード)
 * op_name (set_op_name で刻む) を pig_current_registry()->any_supports_op() に問い合わせ、
 * true なら args[0]・false なら args[1] だけを compact する(選ばれなかった枝は評価しない=
 * 副作用なし)。判定内容自体は旧 mk_call の parse 時チェックと同じ — 呼ばれるタイミングが
 * eval 時 (= script 内で先行する module() が実行済みの時点) に変わっただけ。
 * ★ pigDataOperator は「_start が例外で抜けたら start_flag が立たず、次回呼び出しで
 * _start ごとやり直す」設計 (pigData.h の start() 参照) なので、compact() の yield は
 * ここでは何も気にせず素通しでよい(pigDataOperatorArray 等と同じ作法)。 */
class pigDataOperatorCallResolve : public pigDataOperator {
public:
  pigDataOperatorCallResolve(sPtr<pigInfo> i = thNULL) : pigDataOperator(i) {}
  virtual sPtr<pigData> clone() { return copy_to(thNEW(pigDataOperatorCallResolve,())); }
protected:
  virtual void _start();
};

/* return expr — 引数式を評価地点 env で compact し、pigDataControl(CTRL_RETURN, 値)に包む。
 * その値は is_error()=1 として評価チェーンを上方へ伝播 → 最も近い関数(pigfApply)が unwrap。
 * (break/continue は値が無いので演算子不要。文法が pigDataControl 値ノードを直接置く。) */
class pigDataOperatorReturn : public pigDataOperator {
public:
  pigDataOperatorReturn(sPtr<pigInfo> i = thNULL) : pigDataOperator(i) {}
  virtual sPtr<pigData> clone() { return copy_to(thNEW(pigDataOperatorReturn,())); }
protected:
  virtual void _start();
};

/* exit(msg) — プログラム全体を安全に終了する制御文。引数式(メッセージ)を評価し CTRL_EXIT に
 * 包む。break/continue と同じく上方伝播するが、ループ/関数では捕捉されずトップレベル(プランナ)
 * まで貫通し、そこでメッセージ表示 + 正常終了(exit 0)される。先行 export_async は drain される。 */
class pigDataOperatorExit : public pigDataOperator {
public:
  pigDataOperatorExit(sPtr<pigInfo> i = thNULL) : pigDataOperator(i) {}
  virtual sPtr<pigData> clone() { return copy_to(thNEW(pigDataOperatorExit,())); }
protected:
  virtual void _start();
};

/* catch_continue(body) — for ループの desugar 用。body を評価し、結果が CONTINUE 信号なら
 * **握りつぶして** null を返す(→ 囲む seq が step に進める)。break/return/通常値/エラーはそのまま
 * 伝播。これで `for(..){ ..; continue; }` でも step が実行される(while への素通しは plain while 用)。 */
class pigDataOperatorCatchContinue : public pigDataOperator {
public:
  pigDataOperatorCatchContinue(sPtr<pigInfo> i = thNULL) : pigDataOperator(i) {}
  virtual sPtr<pigData> clone() { return copy_to(thNEW(pigDataOperatorCatchContinue,())); }
protected:
  virtual void _start();
};

/* ------------------------------------------------------------------ */
/* ★★ #3482: try/catch 文                                             */
/* ------------------------------------------------------------------ */
/* try { statement1 } [ catch { statement2 } ]
 *   args[0] = statement1 (ブロック = pigfSequence)
 *   args[1] = statement2 (catch 本体。get_has_catch()==0 なら無い)
 *
 * ★ **ノードが持ち主**である理由 (ひさ決定): 待ちリスト (段 2 以降) の持ち主は helper ではなく
 *   この pigDataTryCatch。env はここへの **参照**を 1 本持ち (pigEnvironment::set_try・#3564 で
 *   生ポインタから sPtr へ)、
 *   catch 本体の error() は env->get_try() で O(1) に辿り着く。
 *
 * ⚠ 段 1 (いまここ) の範囲 = **直列系のエラーだけ**:
 *     ・ statement1 を is_error() で評価。コントロール系 (break/continue/return/exit) は
 *       捕まえずそのまま戻り値にする (try は制御の分岐ではない)。
 *     ・ 実エラーなら statement2 を実行し、その値を try/catch の値とする。
 *     ・ catch が無ければ発生したエラーをそのまま戻り値にする
 *       (⇒ `try { s }` は `try { s } catch { error() }` と同じ意味)。
 *   promise に入った後の agent エラー (待ちリスト) は **段 2**。destroy() 関数も段 2
 *   (待ちリストが無いと送る先が無い)。 */
class pigDataTryCatch : public pigDataOperator {
public:
  pigDataTryCatch(sPtr<pigInfo> i = thNULL)
    : pigDataOperator(i), hasCatch(0), errTaken(0) {}
  virtual sPtr<pigData> clone() {
    sPtr<pigDataTryCatch> n = thNEW(pigDataTryCatch,());
    n->set_has_catch(hasCatch);      /* catch の有無は構文の属性 = 複製する */
    return copy_to(n);               /* 収集済みエラーは複製しない (新鮮ノード) */
  }
  void set_has_catch(int h) { hasCatch = h; }
  int  get_has_catch()      { return hasCatch; }
  /* エラーの収集口。**発生順**に積む (集約して 1 件にしない)。statement1 の直列エラーと、
   * 待ちリストの agent が promise 解決後に出したエラー (§4.2 ①) の両方がここへ来る。 */
  void push_error(sPtr<pigData> e);
  /* error() の読み出し口。**3 値**を返す:
   *   未読のエラーがある … 中身のハッシュ (pigDataError::to_hash)
   *   まだ待つ相手が居る … thNULL = 「いま答えられない」 ⇒ 呼び手 (error()) は待ちに入る
   *   もう何も無い       … pigDataInteger 0
   * ★ pigDataError そのものを返さないのは、error 値はあらゆる演算を吸収して上方伝播する
   *   = catch の中で変数に入れた瞬間に catch 自身がそのエラーで抜けてしまうため。
   * ★ 再送は `throw error();` — ハッシュから復元するので、位置もクラスも元のまま。 */
  sPtr<pigData> take_error();
  /* 未読のエラーを **消費せずに**覗く (thNULL = 無い)。try が「待っている間にエラーが来たか」を
   * 見るのに使う — 消費してしまうと catch の中の error() が読めなくなる。 */
  sPtr<pigData> peek_error() { return ( errTaken < errs.length() ) ? errs[errTaken] : sPtr<pigData>(thNULL); }

  /* ---- 待ちリスト (#3482 段 2) ------------------------------------------------
   * ★ 持ち主は **このノード** (helper ではない)。helper は try の評価が終われば畳まれるが、
   *   「誰を待っているか」は try のスコープの性質なのでノード側に置く。
   * ⚠ 参照の向き: try → agent は **強参照のまま置き、agent_leave で落とす**。
   *   agent → try も強参照で、同じ FIN で切れる。
   *   ⇒ 環は張るが **同じ 1 つの出来事 (agent の FIN) で両側とも切れる**ので残らない。
   *   生ポインタにしないのは、agent の寿命が try の評価より長くなる経路 (撤収中) があるため。 */
  /* ★★ #3482 (ひさ 2026-09-19 / 2026-09-20): **登録は伝播させない**。計算が入るのは
   *   **自分を囲む最も内側の try 1 つだけ**で、祖先の chain は辿らず、
   *   ⚠⚠ **根の台帳へ直接入れにも行かない** (2026-09-20 に外した。それまでは「myTry と根」の
   *     2 つに登録していたが、根へ直接登録するのをやめたはずだった所が残っていた)。
   *   ★ 根が全体を知る必要はない — 利用者の try は自分の待ちリストが空になるまで
   *     終わらない (@ACT_pigfTryCatch_WAIT@) ので、入れ子は「try が try を待つ」で閉じる。
   * ★ 外側の try から内側の計算を畳むのは **destroy を pigData の木で伝播させる**方でやる
   *   (ptsFireAndForget → async の front → pigfAsync → 内側の try → その待ちリスト)。
   *   ⇒ 台帳と撤収を別々の仕組みが担う: **数えるのは登録・止めるのは木**。
   * ⇒ @ptsApplication::agent_count()@ が数えるのは「どの利用者 try にも囲まれていない計算」。 */
  void agent_enter(sPtr<tinyState> who);        /* 待ちリストへ入る */
  void agent_leave(sPtr<tinyState> who);        /* 待ちリストから抜ける。★ **冪等** */
  void agent_error(sPtr<pigData> e);            /* 待ち中の agent が出したエラー (§4.2 ①) */
  void destroy_agents();                        /* 待ちリスト全員へ destroy (終了は待たない) */
  /* 待ちリスト全員を **起こす** (destroy とは別)。撤収の理由が立ったときに、イベント待ちで
   * 詰まっている計算にも届かせるための一撃 (旧 ptsApplication::set_agentError の wake-all)。 */
  void wake_agents();
  int  agent_live() { return waitLive; }
  /* ★★ #3482: **この try が畳めと言ったか**。畳まれた agent は自分の「aborted」を
   *   PE_DERIVED で立てる ⇒ planner の集約・報告・終了コードから外れる。
   *   「失敗した」のではなく「言われたとおり畳んだ」を区別するための 1 ビット。
   * ⚠ 一度立ったら下げない — 畳んだ後に届く遅れた撤収の跡も同じ扱いにする。 */
  int  is_tearing_down() { return tearingDown; }
  /* ★★ #3482 段 3/4: **根の見えない try か**。根は構文に現れず・文を持たず・評価されないので、
   *   自分でエラーを報告できない ⇒ 根の下で起きたことは **従来どおり planner が報告する**。
   *   利用者が書いた try は自分が報告経路 (catch が無ければ 2.2 でそのエラーを返す) なので、
   *   そちらは planner に渡さない (**二重報告を避ける**)。 */
  void set_root(int r) { rootFlag = r; }
  int  is_root()       { return rootFlag; }

  /* ---- プログラム全体の撤収の指標と診断台帳 (#3482・**根の try だけが使う**) ----------
   * ★ 本チケット §0 の「tree の根本に見えない try を置いて **全体をそこへ集約する**」の実体。
   *   以前は ptsApplication が持っていたが、置き場所が 2 つ (try と app) に割れていた。
   * ⚠ **意味論は動かしていない** — 先勝ち ・ PE_PANIC だけ先勝ちを覆す ・ 文言で重複排除 ・
   *   上限 16、はすべて ptsApplication から **そのまま**移したもの。
   *   起こす役 (生存中の agent への wake-all) は生存台帳を持つ ptsApplication に残る。 */
  /* 撤収の理由を立てる。戻り値 1 = **これが最初の 1 件** (呼び手はそのときだけ wake-all する)。 */
  int           set_teardown_reason(sPtr<pigData> e);
  sPtr<pigData> teardown_reason() { return tdReason; }
  /* 診断台帳: 落ちた本人の理由を全部溜めて末尾で列挙する (撤収トリガにはしない)。 */
  void          record_reason(sPtr<pigData> e);
  int           reason_count() { return reasons.length(); }
  sPtr<pigData> reason_at(int i) { return ( i >= 0 && i < reasons.length() ) ? reasons[i] : sPtr<pigData>(thNULL); }
  /* error() が「待ち」に入るときに自分を預ける先。状態が動いたら set_result で起こす。 */
  void register_waiter(sPtr<pigDataDelay> w);
  /* ★★ #3482: @flush()@ が「この try の待ちが **全部**なくなるまで」待つときの預け先。
   *   error() が「次の 1 件 or 全部終わる」を待つのに対し、こちらは **空になること**だけを待つ。 */
  void register_drain_waiter(sPtr<pigDataDelay> w);
  /* 未読のエラーの本数と取り出し (planner が根の try から末尾報告するのに使う)。
   * ⚠ take_error と違い **消費しない**・待たない。 */
  int  error_count() { return errs.length() - errTaken; }
  sPtr<pigData> error_at(int i) { int k = errTaken + i;
    return ( k >= 0 && k < errs.length() ) ? errs[k] : sPtr<pigData>(thNULL); }
  /* 待ちに入る error() が listen する先 = **この try の helper**。try の評価中は必ず居る
   * (catch 本体を走らせているのが当の helper だから)。 */
  /* ⚠ 根の try は評価されないので helper を持たない。その場合は waker (= app) を listen 先に
   *   する — 待ち手を宙吊りにしないため (preprocess は helper も result も無いノードを
   *   system error にする)。 */
  sPtr<tinyState> try_helper() { return helper.is_notNull() ? helper : waker; }
  /* ★ 根の try は helper を持たない (評価されない) ので、台帳が動いたときに起こす相手を
   *   外から預かる — ptsApplication が自分を入れる (旧 agent_leave の `countAgent==0 で wakeup()`)。 */
  void set_waker(sPtr<tinyState> w) { waker = w; }
protected:
  virtual void _start();
  /* 待ちリストの状態が動いた (agent が減った / エラーが来た) ⇒ 待っている error() に答え、
   * try 自身の helper も起こす。★ **通知駆動** — 総なめのポーリングはしない (#3414 の決着)。 */
  void wake_waiters();
  int hasCatch;
  int errTaken;                      /* 次に error() が返す位置 (発生順) */
  sArray<sPtr<pigData> > errs;       /* 発生順のエラー */
  sArray<sPtr<tinyState> > waiters;  /* 待ちリスト (登録中の agent helper)。抜けた所は thNULL */
  int waitLive = 0;                  /* 待ちリストの生存数 */
  int tearingDown = 0;               /* この try が destroy を送ったか (#3482) */
  int rootFlag = 0;                  /* 根の見えない try か (#3482 段 3/4) */
  sPtr<tinyState> waker;             /* 台帳が動いたら起こす相手 (根の try では app) */
  sPtr<pigData> tdReason;            /* 撤収の理由 (先勝ち・PANIC だけ覆す)。根の try のみ */
  sArray<sPtr<pigData> > reasons;    /* 診断台帳 (文言で重複排除・上限 16)。根の try のみ */
  sArray<sPtr<pigDataDelay> > errWaiters;    /* 「答えを待っている error() ノード」 */
  sArray<sPtr<pigDataDelay> > drainWaiters;  /* 「空になるのを待っている flush() ノード」 */
};

/* destroy() — 待ちリストの agent へ撤収を送る組み込み関数 (#3482 §2-B)。
 * ★ **終了は待たない** (待つのは error() の役目)。戻り値は送った数。
 * ⚠ 囲む try が無い場所で呼ぶと実行時エラー (送る先が無い)。 */
class pigDataOperatorDestroyAgents : public pigDataOperator {
public:
  pigDataOperatorDestroyAgents(sPtr<pigInfo> i = thNULL) : pigDataOperator(i) {}
  virtual sPtr<pigData> clone() { return copy_to(thNEW(pigDataOperatorDestroyAgents,())); }
protected:
  virtual void _start();
};

/* throw 式; — 値から **エラーを復元して発生させる** 文 (#3482)。
 *   throw error();   … error() が 0 でなければ、捕まえたエラーが **そのまま再現**される
 *                      (位置・モジュール名・エラークラスまで含めて)
 *   throw 0;         … 復元できないので「復元できない」というエラーになる
 * ★ これで `try { s }` は `try { s } catch { throw error(); }` と **同じ意味**になる
 *   (catch 無しの経路 = 発生したエラーをそのまま戻り値にする、と一致する)。 */
class pigDataOperatorThrow : public pigDataOperator {
public:
  pigDataOperatorThrow(sPtr<pigInfo> i = thNULL) : pigDataOperator(i) {}
  virtual sPtr<pigData> clone() { return copy_to(thNEW(pigDataOperatorThrow,())); }
protected:
  virtual void _start();
};

/* error() — catch 本体で **エラーの中身をハッシュで** 1 件ずつ取り出す組み込み関数。
 * env->get_try() で **動的に**囲む try を引く (レキシカルではない)。
 * ⚠ catch の外での呼び出しは **静的に**弾く (パース時。ns_sravaParser.y の check_stray_error)。
 *   in_catch はそのための印で、実行時の意味は持たない。 */
class pigDataOperatorError : public pigDataOperator {
public:
  pigDataOperatorError(sPtr<pigInfo> i = thNULL) : pigDataOperator(i), inCatch(0) {}
  virtual sPtr<pigData> clone() {
    sPtr<pigDataOperatorError> n = thNEW(pigDataOperatorError,());
    n->set_in_catch(inCatch);
    return copy_to(n);
  }
  void set_in_catch(int c) { inCatch = c; }
  int  get_in_catch()      { return inCatch; }
protected:
  virtual void _start();
  int inCatch;
};

/* ------------------------------------------------------------------ */
/* 状態機械を持つ関数ノード(pigDataFunction<T>)                         */
/* compact で helper(T = pigfFunction 系 tinyState)を起動。helper は     */
/* args を処理して front->set_result する。result 未確定なら pigDataDelay */
/* の preprocess が caller を helper の TSE_DESTROY に listen させ yield。 */
/* ------------------------------------------------------------------ */
/* 代入モード(pigfAssign が参照)。
 *  DEF: var あり → def_var(現スコープに新エントリを作る。シャドウ可)
 *  SET: var なし → set_var(既存エントリを上方探索して更新。無ければエラー) */
/* 代入モード。DEF_LIST = 分割代入 `var [a,b,c] = 式;`
 * (args[0] = 名前の pigDataArray・args[1] = 右辺。右辺の配列要素を順に def_var する) */
enum { PIG_ASSIGN_DEF = 0, PIG_ASSIGN_SET = 1, PIG_ASSIGN_DEF_LIST = 2 };

class pigDataFunction_b : public pigDataOperator {
public:
  pigDataFunction_b(sPtr<pigInfo> i = thNULL) : pigDataOperator(i), op_mode(0) {}
  void set_mode(int m) { op_mode = m; }   /* 代入モード等、関数ノード共通のオプション */
  int  get_mode() { return op_mode; }
protected:
  int op_mode;
};

template<class __TYPE>
class pigDataFunction : public pigDataFunction_b {
public:
  pigDataFunction(sPtr<pigInfo> i = thNULL) : pigDataFunction_b(i) {}
  virtual sPtr<pigData> clone() {
    sPtr<pigDataFunction<__TYPE> > n = thNEW(pigDataFunction<__TYPE>,());
    n->set_mode(op_mode);
    return copy_to(n);   /* args を deep clone + op_name/out_cache 引継ぎ。helper 等は未起動 */
  }
protected:
  virtual void _start() {
    /* 現在の状態機械(caller)を実態親に、自分(=front)を渡して helper を起動。
     * ★ #3419 (ひさ指示 2026-08-24): caller を**そのまま d_cast しない**。
     * ⚠ `ts2Parallel` の worker はコルーチンで **`ptsObject` ではない**ため、そこから
     *   helper を作ると parent が null になり落ちる (実測: pigfMap の事前 trigger を外すと
     *   `srava_map` が SEGFAULT)。**親を辿って最初の `ptsObject` を実態親にする**。
     * ⇒ worker の中から helper を作っても env が正しく引ける = 「helper 生成のために
     *   良い文脈で先に trigger する」という回避が不要になる。 */
    sPtr<ptsObject> pp;
    for ( sPtr<tinyState> p = sCallSection::key->caller() ; p.is_notNull() ; p = p->parent ) {
      pp = sPtr<ptsObject>::d_cast(p);
      if ( pp.is_notNull() ) break;
    }
    helper = thNEW(__TYPE, (pp, thThis));
  }
};

/* 変数読み出し演算子: caller(状態機械)の env から args[0](変数名)を引き、
 * 束縛値(未 compact のことが多い)を result にそのまま返す(参照=評価トリガ)。
 * _start は ptsObject を完全型で要するため pigfOps.cpp に定義(循環依存回避)。 */
class pigDataOperatorVariable : public pigDataOperator {
public:
  pigDataOperatorVariable(sPtr<pigInfo> i = thNULL) : pigDataOperator(i) {}
  virtual sPtr<pigData> clone() { return copy_to(thNEW(pigDataOperatorVariable,())); }
protected:
  virtual void _start();
};

/* ------------------------------------------------------------------ */
/* lambda(クロージャ)— clone/thunk 再評価モデル                         */
/* ------------------------------------------------------------------ */
/* lambda 値: {params(名前), body(AST テンプレ。評価しない), captured env(定義時環境)}。
 * 不変値なので clone=自分返し(body は apply 時に clone して新鮮評価する)。
 * apply(pigfApply): 引数を呼び出し側 env で評価 → params を束縛した新 env(parent=captured)を作り
 * body->clone() を新 env で評価。メモ衝突は clone で回避。 */
class pigDataLambda : public pigData {
public:
  pigDataLambda(sPtr<pigEnvironment> env_, sPtr<pigInfo> i = thNULL)
    : pigData(i), capturedEnv(env_) {}
  void push_param(sPtr<stdString> p) { params.push(p); }
  void set_body(sPtr<pigData> b)     { bodyT = b; }
  int  paramc()                      { return params.length(); }
  sPtr<stdString>      param(int ix) { return params[ix]; }
  sPtr<pigData>        body()        { return bodyT; }
  sPtr<pigEnvironment> env()         { return capturedEnv; }
  virtual sPtr<stdString> get_str();   /* "<lambda/N>" */
protected:
  sArray<sPtr<stdString> > params;
  sPtr<pigData>            bodyT;
  sPtr<pigEnvironment>     capturedEnv;
};

/* lambda リテラル `\(a,b){...}` の AST ノード。_start で呼び出し側(caller)の env を捕捉して
 * pigDataLambda 値を生成する(= 定義時環境のクロージャ)。_start は ptsObject 完全型が要るので
 * pigfOps.cpp に定義(pigDataOperatorVariable と同様)。 */
class pigDataLambdaExpr : public pigDataOperator {
public:
  pigDataLambdaExpr(sPtr<pigInfo> i = thNULL) : pigDataOperator(i) {}
  void push_param(sPtr<stdString> p) { params.push(p); }
  void set_body(sPtr<pigData> b)     { bodyT = b; }
  /* ★ #3482: body は args ではないので、木を歩く側(パーサの静的検査)から見えるようにする。 */
  sPtr<pigData> get_body()           { return bodyT; }
  virtual sPtr<pigData> clone() {      /* params 共有 + body テンプレを clone(ネスト lambda 用) */
    sPtr<pigDataLambdaExpr> n = thNEW(pigDataLambdaExpr,());
    for ( int i = 0 ; i < params.length() ; ++i ) n->push_param(params[i]);
    n->set_body(bodyT.is_notNull() ? bodyT->clone() : sPtr<pigData>(bodyT));
    return n;
  }
protected:
  virtual void _start();
  sArray<sPtr<stdString> > params;
  sPtr<pigData>            bodyT;
};

#endif /* PIGDATA_H */
