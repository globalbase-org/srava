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

class pigData : public stdObject {
public:
  pigData(sPtr<pigInfo> _info = thNULL) : info(_info) {}
  virtual ~pigData() {}

  virtual int is_error() { return 0; }
  /* 致命エラーか (planner が in-flight agent を即撤収するか drain するかの判断)。
   * is_error と対の多態述語 (pigDataError が override)。 */
  virtual int is_fatal() { return 0; }
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
};

/* エラーは吸収元: あらゆる演算で自分を返す。各演算から is_error() 分岐を排除するための要。 */
class pigDataError : public pigData {
public:
  /* fatal=1: **確定的なプログラム/型エラー**(mesh+mesh・未定義変数・引数不一致等)。待つ意味がないので
   *   planner は in-flight agent を即撤収して終了する。fatal=0(既定): 幾何の失敗等は drain(走り出した
   *   計算は完走させキャッシュ化)。 */
  pigDataError(const char *msg, sPtr<pigInfo> i = thNULL, int fatal = 0);
  pigDataError(sPtr<stdString> msg, sPtr<pigInfo> i = thNULL, int fatal = 0);
  virtual int is_error() { return 1; }
  virtual int is_fatal() { return fatal_; }
  virtual sPtr<stdString> get_str();
  sPtr<stdString> message() { return msg; }
  virtual sPtr<stdString> error_message() { return msg; }   /* 生メッセージ(前置なし) */

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
  int fatal_;
};

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
  sPtr<pigData> set_var(sPtr<stdString> name, sPtr<pigData> val);
  /* この env から根まで辿り、可視束縛(name→値)を frozen フレームへ値コピー(外側→内側の順=内側 shadow 維持)。
   * クロージャの**値捕捉**に使う(pigDataLambdaExpr::_start): 生成時点の自由変数値を凍結し、以後の
   * set_var/def_var の書き換えを遮断する。frozen->parent を caller env にしておけば、生成時まだ未束縛の
   * 名前(自己再帰の関数名等)は frozen を素通りして apply 時に親で遅延解決される(= 再帰維持)。 */
  void snapshot_into(sPtr<pigEnvironment> frozen);
protected:
  int find_local(sPtr<stdString> name);
  sArray<sPtr<stdString> > names;
  sArray<sPtr<pigData> >   values;
  sPtr<pigEnvironment>     parent;
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
  pigDataOperator(sPtr<pigInfo> i = thNULL) : pigDataDelay(i) {}
  void pushArg(sPtr<pigData> a) { args.push(a); }
  int  argc() { return args.length(); }
  sPtr<pigData> arg(int ix) { return args[ix]; }
  /* 演算子名(pigfAgent が C_OP で送り、cgatsAgent が dispatch する)。front 由来。 */
  void set_op_name(sPtr<stdString> n) { op_name = n; }
  sPtr<stdString> get_op_name() { return op_name; }
  /* この演算の出力がキャッシュ(mesh 等のハンドル)か値(インライン)か。pigfAgent が HIT/MISS とも
   * 一貫して結果型を決めるのに使う(HIT は agent 不起動なので planner 単独で判断が要る)。
   * 将来はパーサ/dispatch が op シグネチャから設定。既定 0 = 値(インライン)。 */
  void set_out_cache(int c) { out_cache = c; }
  int  get_out_cache() { return out_cache; }
  /* clean() — result 確定後に args/helper のポインタを切り、生成元 DAG(巨大インライン配列等)を解放。
   * result は保持(観測でこれを返す)。clone は未評価テンプレートに対してのみ起こる(評価済みノードは
   * 再 clone されない)ので args を落として安全。同期 op は _start 末尾、agent helper は FIN で front->clean()。 */
  virtual void clean() { args.length(0); helper = thNULL; }
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
    n->info = info;   /* ソース位置(file,line)を clone に引き継ぐ(ループ/lambda 本体の再評価でも保つ) */
    return n;
  }
  sArray<sPtr<pigData> > args;
  sPtr<stdString> op_name;
  int out_cache = 0;
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
class pigDataOperatorModule : public pigDataOperator {
public:
  pigDataOperatorModule(sPtr<pigInfo> i = thNULL) : pigDataOperator(i) {}
  virtual sPtr<pigData> clone() { return copy_to(thNEW(pigDataOperatorModule,())); }
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
