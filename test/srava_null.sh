#!/bin/sh
# `null` リテラルと比較 (#3567)。
# $1 = srava 実行体。env: SRAVA_AGENT, SRAVA_CACHE_DIR。
#
# ★ 何を守るテストか
#   ① `null` が **リテラルとして書ける** (以前は `undefined variable: null`)
#   ② `x == null` が「値が無い」の判定になる — null / 0 / "" / [] / {} を **区別する**
#      (以前の手段は真偽 `if (x)` だけで、これらを全部ひとまとめにしていた)
#   ③ ★★ 陰性対照: **INCOMP の他の用途は触っていない**
#      `"1" == 1` は false のまま / `null < 0` は "incomparable types" のまま
#   ④ null が出てくる 3 つの口 (var 宣言だけ / `return;` / 添字伸長の穴) が **同じ null**
#   ⑤ `null` は **予約語** — 識別子には使えない
SRAVA="${1:?srava binary not given}"
D="${SRAVA_CACHE_DIR:?SRAVA_CACHE_DIR not set}"
# ★★ #3522: ハングの番犬 (共通・常時 ON)。詳細は test/srava_hangwatch.sh。
. "$(dirname "$0")/srava_hangwatch.sh"
NG=0
say() { echo "$1"; }
bad() { echo "FAIL($1)"; NG=1; }

run() {   # run <cache dir> <source> → stdout そのまま
	rm -rf "$1"
	SRAVA_CACHE_DIR="$1" SRAVA_SOURCE="$2" "$SRAVA" 2>&1
}
# eq <id> <式> <期待値>  … print("R", 式) の値を見る
eq() {
	o=$(run "$D-$1" "print(\"R\", $2);" | sed -n 's/^R //p')
	[ "$o" = "$3" ] || bad "$2 → '$o' (期待 '$3')"
}

# ---- ① リテラルとして書ける ----
eq l1 'null'               'null'
o=$(run "$D-l2" 'var x; print("R", x);' | sed -n 's/^R //p')
[ "$o" = "null" ] || bad "var x; が null を印字しない '$o'"
say "  1) null リテラルが書ける (印字は 'null')"

# ---- ② 「値が無い」の判定になる。0 / "" / [] / {} と区別する ----
eq e1 'null == null'       '1'
eq e2 'null != null'       '0'
eq e3 'null == 0'          '0'
eq e4 'null == ""'         '0'
eq e5 '0 == null'          '0'
eq e6 '"" == null'         '0'
# ★ 配列・ハッシュも null ではない (真偽だとどちらも偽で混ざる相手)
eq e7 '[] == null'         '0'
eq e8 '{} == null'         '0'
# ★★ これが本題: 真偽では混ざる 5 つを、== で **分けられる**
o=$(run "$D-e9" '
var n;
var z = 0;
var s = "";
var a = [];
var h = {};
print("R", n == null, z == null, s == null, a == null, h == null);' | sed -n 's/^R //p')
[ "$o" = "1 0 0 0 0" ] || bad "null だけを選り分けられない '$o' (期待 '1 0 0 0 0')"
# ★ 陰性対照: 真偽では **5 つとも偽** = 区別できない (この検定が意味を持つ前提)
o=$(run "$D-e10" '
var n; var z = 0; var s = ""; var a = []; var h = {};
var r = "";
if (n) { r = r + "n"; } else { r = r + "-"; }
if (z) { r = r + "z"; } else { r = r + "-"; }
if (s) { r = r + "s"; } else { r = r + "-"; }
if (a) { r = r + "a"; } else { r = r + "-"; }
if (h) { r = r + "h"; } else { r = r + "-"; }
print("R", r);' | sed -n 's/^R //p')
[ "$o" = "-----" ] || bad "真偽の陰性対照が崩れた '$o' (期待 '-----' = 5 つとも偽)"
say "  2) null / 0 / \"\" / [] / {} を == で分けられる (真偽では 5 つとも偽)"

# ---- ③ 陰性対照: INCOMP の他の用途を壊していない ----
eq n1 '"1" == 1'           '0'
eq n2 '1 == 1'             '1'
eq n3 '"a" == "a"'         '1'
eq n4 '0 == 0.0'           '1'
out=$(run "$D-n5" 'print("R", null < 0);')
echo "$out" | grep -q 'incomparable types' || { bad "null < 0 が incomparable エラーでない"; echo "$out" | head -2; }
out=$(run "$D-n6" 'print("R", "a" < 1);')
echo "$out" | grep -q 'incomparable types' || { bad "型違いの順序比較が incomparable エラーでない"; echo "$out" | head -2; }
# ★ null 同士の順序比較は EQ 経由で決まる (エラーではなくなった — cmp を持たせた帰結)
eq n7 'null < null'        '0'
eq n8 'null <= null'       '1'
say "  3) \"1\"==1 は false のまま / 型違いの < は incomparable のまま"

# ---- ④ null が出てくる 3 つの口が同じ null ----
o=$(run "$D-o1" '
var a;                      // ① 宣言だけ
var f = \() { return; };    // ② 値なし return
var g = f();
var arr = [1];
arr[3] = 9;                 // ③ 添字伸長の穴 (arr[1], arr[2] が null)
print("R", a == null, g == null, arr[1] == null, arr[3] == null);' | sed -n 's/^R //p')
[ "$o" = "1 1 1 0" ] || bad "null の 3 つの口が揃わない '$o' (期待 '1 1 1 0')"
say "  4) 宣言だけ / return; / 添字伸長の穴 が同じ null"

# ---- ⑤ null は予約語 ----
out=$(run "$D-k1" 'var null = 1; print("R", null);')
echo "$out" | grep -qi 'parse error\|syntax' || { bad "null が識別子として通る (予約語になっていない)"; echo "$out" | head -2; }
say "  5) null は予約語 (識別子に使えない)"

[ "$NG" -eq 0 ] && echo "NULL-LITERAL-OK"
exit "$NG"
