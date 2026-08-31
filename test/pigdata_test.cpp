/* pigData 軽パス 単体テスト */
#include "pig/c++/pigData.h"
#include <stdio.h>
#include <string.h>

static int fails = 0;
#define CHECK(c) do { if (!(c)) { \
    printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static sPtr<stdString> S(const char *s) { return thNEW(stdString, (s)); }
static sPtr<pigData>   I(INTEGER64 v)   { return thNEW(pigDataInteger, (v)); }
static sPtr<pigData>   F(double v)       { return thNEW(pigDataFloat, (v)); }
static sPtr<pigData>   Str(const char *s){ return thNEW(pigDataString, (s)); }
static bool isFloat(sPtr<pigData> x) { return is_pigDataType(pigDataFloat, x); }
static bool isInt(sPtr<pigData> x)   { return is_pigDataType(pigDataInteger, x); }
static const char *cs(sPtr<pigData> x) { return x->get_str()->get_str(); }

int main(void)
{
  /* --- スカラ --- */
  CHECK(I(10)->get_int() == 10);
  CHECK(F(2.5)->get_flt() == 2.5);

  /* --- 環境 + 式: (x + y) * 4  (x=10:int, y=2.5:float) -> 50.0 float --- */
  sPtr<pigEnvironment> env = thNEW(pigEnvironment, ());
  env->def_var(S("x"), I(10));
  env->def_var(S("y"), F(2.5));
  CHECK(env->get_var(S("x"))->get_int() == 10);

  sPtr<pigDataOperatorAdd> add = thNEW(pigDataOperatorAdd, ());
  add->pushArg(env->get_var(S("x")));
  add->pushArg(env->get_var(S("y")));
  sPtr<pigDataOperatorMul> mul = thNEW(pigDataOperatorMul, ());
  mul->pushArg(add);
  mul->pushArg(I(4));
  sPtr<pigData> r = mul->compact();
  CHECK(isFloat(r));               /* int+float の昇格が伝播 */
  CHECK(r->get_flt() == 50.0);

  /* --- 整数だけなら整数 (3 - 1) * 2 = 4 --- */
  sPtr<pigDataOperatorSub> sub = thNEW(pigDataOperatorSub, ());
  sub->pushArg(I(3)); sub->pushArg(I(1));
  sPtr<pigDataOperatorMul> m2 = thNEW(pigDataOperatorMul, ());
  m2->pushArg(sub); m2->pushArg(I(2));
  sPtr<pigData> r2 = m2->compact();
  CHECK(isInt(r2));
  CHECK(r2->get_int() == 4);

  /* --- 非可換の順序: 10 - 3 = 7,  10 / 4 = 2(int),  10 % 3 = 1 --- */
  CHECK(I(10)->sub(I(3))->get_int() == 7);
  CHECK(I(10)->div(I(4))->get_int() == 2);
  CHECK(I(10)->rem(I(3))->get_int() == 1);
  CHECK(F(10.0)->div(I(4))->get_flt() == 2.5);   /* float / int -> float */

  /* --- 0 除算 / 0 剰余 -> エラー --- */
  CHECK(I(1)->div(I(0))->is_error());
  CHECK(I(1)->rem(I(0))->is_error());

  /* --- 文字列連結(両方向) --- */
  CHECK(strcmp(cs(Str("foo")->add(Str("bar"))), "foobar") == 0);
  CHECK(strcmp(cs(I(5)->add(Str("x"))), "5x") == 0);   /* number + string */
  CHECK(strcmp(cs(Str("x")->add(I(5))), "x5") == 0);   /* string + number */

  /* --- 未定義変数 / Null 加算 -> エラー --- */
  CHECK(env->get_var(S("nope"))->is_error());
  CHECK(thNEW(pigDataNull, ())->add(I(10))->is_error());

  /* --- 配列 index --- */
  sPtr<pigDataArray> arr = thNEW(pigDataArray, ());
  arr->push(I(1)); arr->push(I(2)); arr->push(I(3));
  CHECK(arr->get_ix(I(1))->get_int() == 2);
  CHECK(arr->get_ix(I(9))->is_error());
  arr->set_ix(I(1), I(20));
  CHECK(arr->get_ix(I(1))->get_int() == 20);

  /* --- ハッシュ index + キーソート正規形 --- */
  sPtr<pigDataHash> h = thNEW(pigDataHash, ());
  h->set_ix(Str("b"), I(2));
  h->set_ix(Str("a"), I(1));
  CHECK(h->get_ix(Str("a"))->get_int() == 1);
  CHECK(h->get_ix(Str("z"))->is_error());
  CHECK(strcmp(cs(h), "{a:1,b:2}") == 0);

  /* --- get_bool 真偽 --- */
  CHECK(I(0)->get_bool() == 0);
  CHECK(I(5)->get_bool() == 1);
  CHECK(F(0.0)->get_bool() == 0);
  CHECK(Str("")->get_bool() == 0);
  CHECK(Str("x")->get_bool() == 1);
  CHECK(thNEW(pigDataNull, ())->get_bool() == 0);

  /* --- 論理演算 (band/bor/bnot) --- */
  CHECK(I(1)->band(I(0))->get_int() == 0);
  CHECK(I(1)->band(I(2))->get_int() == 1);
  CHECK(I(0)->bor(I(0))->get_int() == 0);
  CHECK(I(0)->bor(I(3))->get_int() == 1);
  CHECK(I(0)->bnot()->get_int() == 1);
  CHECK(I(7)->bnot()->get_int() == 0);

  /* --- 論理 XOR --- */
  CHECK(I(1)->bxor(I(0))->get_int() == 1);
  CHECK(I(1)->bxor(I(2))->get_int() == 0);   /* 両方 真 -> 0 */
  CHECK(I(0)->bxor(I(0))->get_int() == 0);

  /* --- ビット演算 (aand/aor/axor/anot) --- */
  CHECK(I(6)->aand(I(3))->get_int() == 2);   /* 110 & 011 = 010 */
  CHECK(I(6)->aor(I(1))->get_int() == 7);    /* 110 | 001 = 111 */
  CHECK(I(6)->axor(I(3))->get_int() == 5);   /* 110 ^ 011 = 101 */
  CHECK(I(0)->anot()->get_int() == -1);      /* ~0 = -1 */

  /* --- シフト (ashl/ashr) + 範囲外エラー --- */
  CHECK(I(1)->ashl(I(4))->get_int() == 16);
  CHECK(I(16)->ashr(I(2))->get_int() == 4);
  CHECK(I(-8)->ashr(I(1))->get_int() == -4); /* 算術右シフト(符号維持) */
  CHECK(I(1)->ashl(I(64))->is_error());
  CHECK(I(1)->ashr(I(-1))->is_error());

  /* --- 論理/ビットの delay ゲートウェイ: (3-1) を band/aand に渡す --- */
  {
    sPtr<pigDataOperatorSub> s = thNEW(pigDataOperatorSub, ());
    s->pushArg(I(3)); s->pushArg(I(1));      /* = 2 (未 compact の演算子) */
    CHECK(I(1)->band(s)->get_int() == 1);    /* 1 && 2 = 1, s は観測で解決 */
    CHECK(I(6)->aand(s)->get_int() == 2);    /* 6 & 2 = 2 */
  }

  /* --- エラー伝播(論理) --- */
  CHECK(I(1)->band(I(1)->div(I(0)))->is_error());

  /* --- エラー伝搬: 元のエラーが保たれ "unsupported" に化けない --- */
  {
    sPtr<pigData> e = I(1)->div(I(0));               /* "division by zero" */
    CHECK(e->is_error());
    CHECK(strstr(cs(e), "division by zero") != 0);
    CHECK(strstr(cs(e->add(I(5))), "division by zero") != 0);   /* 左がエラー */
    CHECK(strstr(cs(e->mul(I(5))), "division by zero") != 0);
    CHECK(strstr(cs(I(5)->add(e)), "division by zero") != 0);   /* 右がエラー(p_OP 経由) */
    CHECK(strstr(cs(I(5)->sub(e)), "division by zero") != 0);
    CHECK(strstr(cs(I(5)->add(e)), "unsupported") == 0);        /* 化けていない */
    /* 吸収元: 論理/ビット/比較の両オペランドでも元エラーを保つ(pigDataError override) */
    CHECK(strstr(cs(e->band(I(1))), "division by zero") != 0);  /* 論理 左 */
    CHECK(strstr(cs(I(1)->band(e)), "division by zero") != 0);  /* 論理 右 */
    CHECK(strstr(cs(I(5)->aand(e)), "division by zero") != 0);  /* ビット 右 */
    CHECK(strstr(cs(e->bnot()), "division by zero") != 0);      /* 単項 */
    CHECK(strstr(cs(I(5)->lt(e)), "division by zero") != 0);    /* 比較 右 */
    CHECK(strstr(cs(e->lt(I(5))), "division by zero") != 0);    /* 比較 左 */
  }

  /* --- 比較演算 (eq/ne/lt/gt/le/ge) --- */
  CHECK(I(3)->lt(I(5))->get_int() == 1);
  CHECK(I(5)->lt(I(3))->get_int() == 0);
  CHECK(I(3)->le(I(3))->get_int() == 1);
  CHECK(I(3)->ge(I(3))->get_int() == 1);
  CHECK(I(5)->gt(I(3))->get_int() == 1);
  CHECK(I(5)->eq(F(5.0))->get_int() == 1);   /* int == float の数値比較 */
  CHECK(I(5)->ne(F(5.0))->get_int() == 0);
  CHECK(F(2.5)->gt(I(2))->get_int() == 1);
  CHECK(Str("abc")->lt(Str("abd"))->get_int() == 1);   /* 辞書順 */
  CHECK(Str("abc")->eq(Str("abc"))->get_int() == 1);
  CHECK(I(5)->eq(Str("5"))->get_int() == 0);  /* 型不一致 -> 非等価 */
  CHECK(I(5)->ne(Str("5"))->get_int() == 1);
  CHECK(I(5)->lt(Str("x"))->is_error());      /* 比較不能型の順序 -> エラー */

  /* --- delay を左オペランドにした論理/比較/シフト(pigDataDelay の全 gateway) --- */
  {
    sPtr<pigDataOperatorAdd> a = thNEW(pigDataOperatorAdd, ());
    a->pushArg(I(2)); a->pushArg(I(3));         /* = 5(未 compact の演算子) */
    CHECK(a->band(I(1))->get_int() == 1);
    CHECK(a->bnot()->get_int() == 0);           /* !5 = 0 */
    CHECK(a->aand(I(6))->get_int() == 4);       /* 5 & 6 = 4 */
    CHECK(a->ashl(I(1))->get_int() == 10);      /* 5 << 1 */
    CHECK(a->eq(I(5))->get_int() == 1);
    CHECK(a->lt(I(10))->get_int() == 1);
    CHECK(a->ge(I(5))->get_int() == 1);
  }

  /* --- 演算子ノード 全種(論理/ビット/比較/単項)--- */
  {
    sPtr<pigDataOperatorLt> op = thNEW(pigDataOperatorLt, ());
    op->pushArg(I(3)); op->pushArg(I(5));
    CHECK(op->compact()->get_int() == 1);              /* 3 < 5 */
  }
  {
    sPtr<pigDataOperatorBand> op = thNEW(pigDataOperatorBand, ());
    op->pushArg(I(1)); op->pushArg(I(0));
    CHECK(op->compact()->get_int() == 0);
  }
  {
    sPtr<pigDataOperatorAand> op = thNEW(pigDataOperatorAand, ());
    op->pushArg(I(6)); op->pushArg(I(3));
    CHECK(op->compact()->get_int() == 2);              /* 6 & 3 */
  }
  {
    sPtr<pigDataOperatorAnot> op = thNEW(pigDataOperatorAnot, ());  /* 単項 */
    op->pushArg(I(0));
    CHECK(op->compact()->get_int() == -1);             /* ~0 */
  }
  {
    sPtr<pigDataOperatorBnot> op = thNEW(pigDataOperatorBnot, ());  /* 単項 */
    op->pushArg(I(5));
    CHECK(op->compact()->get_int() == 0);              /* !5 */
  }
  {
    /* ネスト: (2 < 3) && (4 >= 4) を演算子ノードで組む */
    sPtr<pigDataOperatorLt> l = thNEW(pigDataOperatorLt, ());
    l->pushArg(I(2)); l->pushArg(I(3));
    sPtr<pigDataOperatorGe> g = thNEW(pigDataOperatorGe, ());
    g->pushArg(I(4)); g->pushArg(I(4));
    sPtr<pigDataOperatorBand> b = thNEW(pigDataOperatorBand, ());
    b->pushArg(l); b->pushArg(g);
    CHECK(b->compact()->get_int() == 1);
  }

  /* --- ★ 2026-08-19: キャッシュの型は **型スタンプだけ** (routing の唯一の根拠) ---
   * routing (arg_type_set) は「継続 pair のスタンプ / キャッシュの type_stamp()」しか見ない。
   * 以前はキャッシュだけ **ファイルの 4CC から型を引き直して**いたため、MISS (継続) と
   * HIT (キャッシュ) で型の出どころが違い、cold と warm で routing が変わりうる状態だった。
   * ★キャッシュは **型を名乗らない** (type_name() の override は撤去した)。形式 (4CC) は
   * 複数モジュールが共有してよいので、そこから型を 1 つ引く API は必ずどこかで嘘をつく。
   * 形式を主語にした診断は describe() が持ち、型を見せるときは**読める型を全部**列挙する。 */
  {
    sPtr<pigDataCache> c = thNEW(pigDataCache, ((pHashKeyType)12345, S("/nonexistent/srava-test-cache")));
    CHECK(c->type_stamp() == thNULL);            /* 素の状態ではスタンプ無し */
    CHECK(c->type_name() == 0);                  /* cache は型を名乗らない (基底の 0) */
    CHECK(c->is_stream_cache() == 0);            /* 形式不明はストリーム扱いしない */
    c->set_type_stamp(S("gg-mesh3d"));
    CHECK(c->type_stamp() != thNULL &&
          ::strcmp(c->type_stamp()->get_str(), "gg-mesh3d") == 0);
    /* ★スタンプを載せても type_name() は 0 のまま = 「型を名乗る」のはスタンプの仕事。 */
    CHECK(c->type_name() == 0);
  }

  /* --- ハッシュキー: 同値同ハッシュ / 型違いは別ハッシュ(typeid 分離) --- */
  CHECK(I(5)->get_hashkey() == I(5)->get_hashkey());
  CHECK(I(5)->get_hashkey() != Str("5")->get_hashkey());

  printf(fails ? "PIGDATA TEST: %d FAIL\n" : "PIGDATA TEST: all pass\n", fails);
  return fails ? 1 : 0;
}
