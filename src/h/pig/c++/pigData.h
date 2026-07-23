#ifndef PIGDATA_H
#define PIGDATA_H
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
#include "ts2/c++/ts_types.h"    /* INTEGER64 (tinyState v2: ts2/c/ 廃止→c++/ に inline) */
#include "ts2/c++/stdObject.h"
#include "ts2/c++/sPtr.h"
#include "ts2/c++/stdString.h"
#include "ts2/c++/sArray.h"
#include "ts2/c++/tinyState.h"   /* pigDataDelay の非同期 helper: listen/invoke_listen/sException */
#include "ts2/c++/sCallSection.h" /* pigDataFunction<T>::_start で caller() を使う */

class ptsObject;   /* tinyState 系 元祖(pig/c++/ptsObject)。pigDataFunction の helper の実態親 */

typedef INTEGER64 pHashKeyType;

/* v を compact(=観測で解決) してから __TYPE か判定。true=その型である。 */
#define is_pigDataType(__TYPE, v)  sPtr<__TYPE>::d_cast((v)->compact()).is_notNull()

/* 3-way 比較の結果 */
enum pigCmp { PIG_LT = -1, PIG_EQ = 0, PIG_GT = 1, PIG_INCOMP = 2 };

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

class pigData : public stdObject {
public:
  pigData(sPtr<pigInfo> _info = thNULL) : info(_info) {}
  virtual ~pigData() {}

  virtual int is_error() { return 0; }
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

  /* trigger() — 評価結果を待たずに「起動だけ」する(並列 spark)。値ノードは何もしない。
   * 遅延ノード(pigDataDelay)は start()=helper 起動を蹴るだけで result は待たない。
   * 全 args を評価する同期演算子(Add/Eq 等)が _start 頭で全 args を trigger すると、
   * volume(a)==volume(b) のような独立 op の agent が逐次でなく並列に走る。 */
  virtual void trigger() {}

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

  /* compact(depth): 遅延ノードの不動点解決。depth は再帰上限(循環束縛 var a=a; 等で
   * 無限再帰=スタックオーバーフローするのを防ぐ)。既定値で通常用途は十分。値ノードは thThis。 */
  virtual sPtr<pigData> compact(int depth = PIG_COMPACT_MAX) { return thThis; }
  virtual int           is_compact() { return 1; }

  /* AST テンプレートの新鮮複製。lambda apply / while / for で body を再評価する際、
   * 遅延ノードはメモ(result/start_flag)を持つので clone で未評価の新ノードに作り直す。
   * 既定 = 自分返し(不変値リテラルは共有可)。Array/Hash/Operator/Function 系が override。 */
  virtual sPtr<pigData> clone() { return thThis; }

  /* クロージャ値捕捉用の複製(snapshot_into が使う)。**破壊的代入(a[i]=v)で変わり得る container =
   * 配列/ハッシュの spine だけを deep copy** し、不変な葉(スカラ/メッシュ継続/lambda)は **共有**する。
   * clone() と違い葉を複製しない(メッシュ継続を clone すると再計算/dedup 喪失になるため)。
   * 既定 = 自分返し(不変値)。pigDataArray/pigDataHash のみ override(要素を再帰 capture_copy)。 */
  virtual sPtr<pigData> capture_copy() { return thThis; }

  /* 静的正規化(1.2.3 可変ソート)。parse 直後にプランナーが tree->normalize() を呼ぶ。
   * 評価(compact/dispatch)を起こさず AST を書き換える純静的パス。
   * recipe_hash() = 評価を起こさない構造ハッシュ(可換 op の引数ソートキー)。値は get_hashkey
   * でよい(値の get_hashkey は評価不要)。遅延 op は pigDataOperator で構造ハッシュに override。
   * normalize() = 子を再帰正規化し、可換 op(union/intersection)の引数を recipe_hash 順に並べ替える
   * (a|||b と b|||a を同一キャッシュキーに → 再利用)。値/葉は no-op。 */
  virtual pHashKeyType recipe_hash() { return get_hashkey(); }
  virtual void         normalize() {}

  /* print(x) ビルトイン用の表示文字列。既定は get_str()。pigDataDelay でゲートウェイ化
   * (未解決の遅延/継続なら compact で解決まで yield)、pigDataPair は継続 cdr を辿る。
   * pigDataCache は get_str()=キャッシュパス(=ハッシュファイル名)をそのまま見せる。 */
  virtual sPtr<stdString> print() { return get_str(); }

  sPtr<pigInfo> get_info() { return info; }
  void          set_info(sPtr<pigInfo> i) { info = i; }   /* parse 時にソース位置(file,line)を刻む */
protected:
  sPtr<pigInfo> info;
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
  int is_fatal() { return fatal_; }
  virtual sPtr<stdString> get_str();
  sPtr<stdString> message() { return msg; }

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
  virtual sPtr<stdString> print();     /* 継続("delayed".promise)なら cdr を辿って実値まで解決 */
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
protected:
  pHashKeyType    hashkey;
  sPtr<stdString> path;
};

class pigDataArray : public pigData {
public:
  pigDataArray(sPtr<pigInfo> i = thNULL) : pigData(i) {}
  void push(sPtr<pigData> v) { d.push(v); }
  int  length() { return d.length(); }
  virtual int get_bool() { return d.length() > 0; }   /* 非空=真 */
  virtual sPtr<stdString> get_str();
  virtual sPtr<stdString> print();       /* print(x) 用: 要素を print() で辿る(mesh 継続→解決しキャッシュパス) */
  virtual sPtr<stdString> serialize();   /* "[e1,e2,...]"(各要素 serialize) */
  virtual sPtr<pigData> clone() {        /* 要素を deep clone(要素が AST 式のことがある) */
    sPtr<pigDataArray> n = thNEW(pigDataArray,());
    for ( int i = 0 ; i < d.length() ; ++i ) n->push(d[i]->clone());
    return n;
  }
  virtual sPtr<pigData> capture_copy() {  /* spine を deep copy・葉は共有(要素を再帰 capture_copy) */
    sPtr<pigDataArray> n = thNEW(pigDataArray,());
    for ( int i = 0 ; i < d.length() ; ++i ) n->push(d[i]->capture_copy());
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
  /* 並列 spark: 起動だけ蹴る(result は待たない)。start() は冪等(start_flag)なので二度押し安全。 */
  virtual void          trigger() { start(); }
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
  virtual pHashKeyType recipe_hash();   /* 構造ハッシュ(typeid+op_name+各 arg の recipe_hash。評価なし) */
  virtual void         normalize();     /* 子を再帰正規化 + 可換 op の引数を recipe_hash 順にソート */
  /* clean() — result 確定後に args/helper のポインタを切り、生成元 DAG(巨大インライン配列等)を解放。
   * result は保持(観測でこれを返す)。clone は未評価テンプレートに対してのみ起こる(評価済みノードは
   * 再 clone されない)ので args を落として安全。同期 op は _start 末尾、agent helper は FIN で front->clean()。 */
  virtual void clean() { args.length(0); helper = thNULL; }
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
enum { PIG_ASSIGN_DEF = 0, PIG_ASSIGN_SET = 1 };

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
    /* 現在の状態機械(caller)を実態親に、自分(=front)を渡して helper を起動 */
    helper = thNEW(__TYPE, (sPtr<ptsObject>::d_cast(sCallSection::key->caller()), thThis));
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
