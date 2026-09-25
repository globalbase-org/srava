#!/bin/sh
# #3595 — `use` を強くする / `module(配列)` で列をまとめてロードする (ひさ確定仕様 2026-09-24)。
# $1 = srava 実行体。env: SRAVA_AGENT, SRAVA_CACHE_DIR。
#
# ★ 何を守るテストか
#   ① `use` は **走った行で**列を検査する — 1 本もロード済みが無ければエラー。
#      ⚠ 捕まえるのは「**丸ごと空振り**」だけ。1 本でも居れば通る (= 綴り間違いの検出器ではない)
#      ★ 陰性対照 3 つ: 1 本居れば通る / `use ""` は指名なし / `var USE_MODULES = …` は **検査しない**
#        (検査が *use の行* に付いていることの証拠。変数代入まで見ていたら陰性対照が落ちる)
#   ② 空配列 / 全部穴 は明示エラー (文言に穴の件数が出る)
#   ③ **DEF の性質は壊れていない** — block / lambda の中だけ差し替わり、抜けると外の値へ戻る
#      (検査は代入の *右辺* に挟んであるだけ、という実装の要点をここで留める)
#   ④ `module(配列, opts)` は **記述子名**の列を読み、平坦化後の列と **1:1** の配列を返す
#      穴は null のまま同じ位置へ / 擬似モジュール (ハッシュ) もそのまま同じ位置へ
#   ⑤ ★★★ 不変式: **`use module(L,{})` の候補列は `use L` と完全に同一** (違いはロードの副作用だけ)
#   ⑥ 端: [] / 重複 / {optional:1} の不在は穴 / "off" は明示エラー /
#      **記述子名でないもの** (".so" つき・パス区切り) は素の不在。文言に **探した綴り**が出る
#   ⑦ op 実行時点の検査は **残っている** (二重)。use を経由しない USE_MODULES で確認する
#   ⑧ **lambda を呼ぶと呼び出し元の USE_MODULES が中へ引き継がれる** (動的・apply のたび)
#      ★ 陰性対照: 引き継ぐのは USE_MODULES **だけ** (他の変数はレキシカルのまま)
#      ★ 関数が自分で use を書けばそちらが勝つ
#   ⑨ mod_only(a,b) = 候補列の **積**。a の順のまま・穴は落ちる・**a の擬似は通す**・重複は残る
#      (2026-09-25 の仕様変更に追随。⑨ の本文は直っていたがこの行が古いままだった)
#   ⑩ mod_only_names(a[,b]) = **名前だけ**返す形。mod_only との違いは **擬似を落とす**ことだけ
#      ★ 左辺の補い方 (1 引数形) は mod_only と **同一**であることも見る
#      ★ use の診断の札が **実際に書いた op 名**になること (派生を先に見ないと mod_only に化ける)
SRAVA="${1:?srava binary not given}"
D="${SRAVA_CACHE_DIR:?SRAVA_CACHE_DIR not set}"
# ★★ #3522: ハングの番犬 (共通・常時 ON)。詳細は test/srava_hangwatch.sh。
. "$(dirname "$0")/srava_hangwatch.sh"
NG=0
say() { echo "$1"; }
bad() { echo "FAIL($1)"; NG=1; }

run() {   # run <cache dir> <source> → stdout そのまま
	rm -rf "$1"
	SRAVA_CACHE_DIR="$1" SRAVA_CACHE_RETAIN=all SRAVA_SOURCE="$2" "$SRAVA" 2>&1
}
val() { run "$1" "$2" | sed -n 's/^VAL //p'; }
errtest() {   # errtest <id> <source> <期待する語>
	o=$(run "$D-e$1" "$2" | grep -c "$3")
	[ "$o" -ge 1 ] || { bad "エラー文に '$3' が出ない: $2"; run "$D-e$1" "$2" | grep -i 'error' | head -2; }
}
MOD='module("cgal.so",{}); module("occt.so",{});'

# 前提 — 球は occt が **厳密**なのでメッシュ系と値が構造的に違う ⇒ どちらが走ったかが値で分かる。
CG=$(val "$D-a1" "$MOD print(\"VAL\", volume(\"cgal\"::sphere(1.5)));")
OC=$(val "$D-a2" "$MOD print(\"VAL\", volume(\"occt\"::sphere(1.5)));")
[ -n "$CG" ] && [ -n "$OC" ] && [ "$CG" != "$OC" ] || bad "前提が崩れた cgal='$CG' occt='$OC'"

# ---- ① use の行で検査する ----
#   ★ 幾何 op が 1 つも無い走行。従来はここが **黙って通っていた** (振り分けまで気づけない)
errtest 1 'use ["nosush"]; print("hello");'          'no such module is loaded'
#   ★ 陰性対照 1: 1 本でも居れば通る (未ロード名は従来どおり飛ばす)
o=$(run "$D-b1" "$MOD use [\"cgal\",\"nosush\"]; print(\"HI\");" | grep -c '^HI$')
[ "$o" -eq 1 ] || bad "1 本でも居れば通る、が成り立たない (綴り間違いの検出器になっている)"
#   ★ 陰性対照 2: use "" は「planner に任せる」= 検査するものが無い
o=$(run "$D-b2" 'use ""; print("HI");' | grep -c '^HI$')
[ "$o" -eq 1 ] || bad 'use "" が通らない (指名なしへ戻す口を塞いでいる)'
#   ★ 陰性対照 3: **検査は use の行に付いている** — 同じ列を変数へ入れるだけなら落ちない
o=$(run "$D-b3" 'var USE_MODULES = ["nosush"]; print("HI");' | grep -c '^HI$')
[ "$o" -eq 1 ] || bad "var USE_MODULES = [...] まで検査している (検査の位置が use の行でない)"
say "  1) use の行で丸ごと空振りを捕まえる (陰性対照 3 件込み)"

# ---- ② 空 / 全部穴 ----
errtest 2 'use []; print("hello");'                  'candidate list is empty'
errtest 3 'use ["",0,null]; print("hello");'         'all 3 element(s) are holes'
say "  2) 空配列 / 全部穴 は明示エラー (穴は件数を言う)"

# ---- ③ DEF の性質は壊れていない (スコープ 5 地点 + "" 復帰) ----
SU=$(run "$D-c1" "$MOD use [\"occt\",\"cgal\"];
print(\"VAL\", volume(sphere(1.5)));
{ use [\"cgal\"]; print(\"VAL\", volume(sphere(1.5))); }
print(\"VAL\", volume(sphere(1.5)));
var f = \\(){ use [\"cgal\"]; return volume(sphere(1.5)); };
print(\"VAL\", f());
print(\"VAL\", volume(sphere(1.5)));" | sed -n 's/^VAL //p')
SE="$OC
$CG
$OC
$CG
$OC"
[ "$SU" = "$SE" ] || { bad "use のスコープが DEF (var) でなくなった"; echo "--- got ---"; echo "$SU"; echo "--- want ---"; echo "$SE"; }
say "  3) use は DEF のまま (block / lambda を抜けると外の値へ戻る・5 地点)"

# ---- ④ module(配列) は 1:1 ----
L1=$(run "$D-d1" 'print(module(["cgal","occt"],{}));' | head -1)
[ "$L1" = "[cgal,occt]" ] || bad "module(配列) が記述子名の列を返さない '$L1'"
#   ★ 平坦化 + 穴の位置。入れ子は開き、穴は **その位置に null** で残る (落とすと位置がずれる)
L2=$(run "$D-d2" 'print(module([["cgal",["occt"]],0,null,""],{}));' | head -1)
[ "$L2" = "[cgal,occt,null,null,null]" ] || bad "平坦化 / 穴の位置が 1:1 でない '$L2'"
#   ★ 擬似モジュール (ハッシュ) もそのまま同じ位置へ (ロードするものが無い)
L3=$(run "$D-d3" 'var p = { type:"pseudo_module", name:"glue", ops:[] };
print(length(module(["cgal", p, "", "occt"],{})));' | head -1)
[ "$L3" = "4" ] || bad "擬似モジュール / 穴を含む列の長さが 1:1 でない '$L3'"
say "  4) module(配列) は平坦化後の列と 1:1 (穴・擬似はその位置へ)"

# ---- ⑤ ★★★ 不変式: use module(L,{}) ≡ use L ----
#   A = 文字列形で先に読んでおいて `use L` / B = **何もロードせず** `use module(L,{})`
#   ⇒ 同じ L (穴と入れ子を含む) から同じカーネルが答えるなら、候補列は同一である。
IL='var L = ["occt", ["",""], "cgal"];'
IA=$(val "$D-d4" "$MOD $IL use L; print(\"VAL\", volume(sphere(1.5)));")
IB=$(val "$D-d5" "$IL use module(L,{}); print(\"VAL\", volume(sphere(1.5)));")
[ "$IA" = "$OC" ] || bad "A 側 (use L) が occt に解決されない '$IA'"
[ "$IB" = "$IA" ] || bad "不変式が破れた: use module(L,{})='$IB' vs use L='$IA'"
#   ★ 逆向きの対照: 列の順を変えれば答えも変わる (上の一致が「どちらでも同じ値」ではない証拠)
IC=$(val "$D-d6" 'var L = ["cgal","occt"]; use module(L,{}); print("VAL", volume(sphere(1.5)));')
[ "$IC" = "$CG" ] || bad "順を変えても答えが動かない (一致の検定になっていない) '$IC'"
say "  5) 不変式 use module(L,{}) ≡ use L (順を変えると答えは動く = 検定になっている)"

# ---- ⑥ 端 ----
L4=$(run "$D-e10" 'print(module([],{}));' | head -1)
[ "$L4" = "[]" ] || bad "module([],{}) が [] を返さない '$L4'"
L5=$(run "$D-e11" 'print(module(["cgal","cgal"],{}));' | head -1)
[ "$L5" = "[cgal,cgal]" ] || bad "重複が 1:1 にならない '$L5'"
L6=$(run "$D-e12" 'print(module(["nosuch","cgal"],{optional:1}));' | head -1)
[ "$L6" = "[null,cgal]" ] || bad "{optional:1} の不在がその位置の穴にならない '$L6'"
errtest 13 'print(module(["cgal"],"off"));'          'cannot unload'
#   ★ 記述子名でないものは **素の不在**。文言に *探した綴り* が出る (書き手が自分で気づける)
#   ⚠ 推測生成の拡張子は **機種で違う** (Linux/mac = .so ・ Windows/Cygwin = .dll) ので、
#     `cgal.so.so` と決め打ちすると Windows で落ちる (2026-09-25 に box で実測)。
#     ⇒ **機械に訊く** — 在るはずのない名前を 1 度引いて、文言から拡張子を取り出す。
MODEXT=$(run "$D-e14x" 'print(module(["zzz_nosuch_probe"],{}));' |
         sed -n 's/.*looked for "zzz_nosuch_probe\(\.[A-Za-z0-9]*\)".*/\1/p' | head -1)
[ -n "$MODEXT" ] || bad "モジュール拡張子を文言から取れない (不在エラーの形が変わった?)"
errtest 14 'print(module(["cgal.so"],{}));'          "cgal.so$MODEXT"
errtest 15 'print(module(["/opt/x/mymod"],{}));'     'path separator'
say "  6) 端 6 件 ([] / 重複 / optional の穴 / off / 記述子名でない ($MODEXT を機械から取得) / パス区切り)"

# ---- ⑦ op 実行時点の検査は残っている (二重) ----
#   ⚠ use は行で落ちるので、**use を経由しない**書き方 (変数代入) で確認する。
errtest 16 "$MOD var USE_MODULES = [\"nosush\"]; print(volume(box(2,2,2)));" 'no such module is loaded'
errtest 17 "$MOD var u = [\"nosush\"]; print(volume(u::box(2,2,2)));"        'no such module is loaded'
say "  7) op 実行時点の検査は残っている (二重・2 件)"

# ---- ⑧ apply は呼び出し元の USE_MODULES を引き継ぐ ----
#   ★ lambda を `use` より **前** に定義する = クロージャの値捕捉には入らない形。
#     ここで呼び手の列が届くなら、引き継ぎは *定義時の写し* ではなく **呼び出し元**から来ている。
IN=$(val "$D-f1" "$MOD var f = \\(){ type_of(sphere(1.5)); };
use [\"occt\"]; print(\"VAL\", f()); print(\"VAL\", type_of(sphere(1.5)));")
[ "$IN" = "oc-brep3d
oc-brep3d" ] || { bad "呼び出し元の USE_MODULES が lambda へ引き継がれない"; echo "$IN"; }
#   ★ 関数が自分で use を書けばそちらが勝つ (ライブラリの宣言が効き続けることの担保)
#   ⚠ この本が module() しているのは cgal と occt だけ。関数側の use に載っていない名前を
#     書くと ①の検査で落ちる (そこは ①で見ている) ので、ここは **載っている 2 本**で振る。
OWN=$(val "$D-f2" "$MOD var g = \\(){ use [\"cgal\"]; type_of(sphere(1.5)); };
use [\"occt\"]; print(\"VAL\", g());")
[ "$OWN" = "cg-mesh3d" ] || bad "関数自身の use が呼び出し元の引き継ぎに負けている '$OWN'"
#   ★ 陰性対照: 引き継ぐのは **USE_MODULES だけ**。ほかの変数はレキシカルのまま
LEX=$(run "$D-f3" "var v = \"outer\"; var h = \\(){ v; }; { var v = \"inner\"; print(\"VAL\", h()); }" | sed -n 's/^VAL //p')
[ "$LEX" = "outer" ] || bad "USE_MODULES 以外まで動的になっている (v='$LEX')"
say "  8) apply は呼び出し元の USE_MODULES を引き継ぐ (自分の use が勝つ・他の変数はレキシカル)"

# ---- ⑨ mod_only(a, b) = 候補列の積 ----
mo() { val "$D-g$1" "$MOD use [\"occt\",\"cgal\"]; print(\"VAL\", $2);"; }
[ "$(mo 1 'mod_only(USE_MODULES, ["manifold","cgal"])')" = "[cgal]" ] || bad "mod_only: 積が違う"
[ "$(mo 2 'mod_only(["a",["b","c"],"d"], [["c"],"a"])')" = "[a,c]" ] || bad "mod_only: 入れ子の平坦化 / a の順が保たれない"
#   ★★ 仕様変更 (ひさ 2026-09-25): **a 側の擬似 (ハッシュ) は a の位置のまま通す**。
#     穴 (null / 0 / "") は従来どおり落とす。b 側の擬似も落とす (名前が無いので「許す名前」に
#     なれない)。⇒ 「穴も擬似も落ちる」という旧仕様をここで固定していた。
#     ⚠ 理由: 擬似が落ちると **粒度の既定値がライブラリ関数の境界で消える**。実測 (改訂前):
#       use [ pm_cgal({seg:64}) ] の下で op 直呼び 384 面 / lib 関数経由 192 面 (= 擬似なしと同じ)。
#       落ちずに値が返るので気づけない形だった (spiral の指摘・#3595)。
[ "$(mo 3 'mod_only(["a",null,0,"","b"], ["a","b"])')" = "[a,b]" ] || bad "mod_only: 穴が落ちていない"
#   ★ 擬似は通る (位置も保つ) — ハッシュがそのまま出るので、**前後の名前と個数**で見る
MO3=$(mo 3b 'mod_only(["a",{type:"pseudo_module",name:"pm_x"},"b"], ["a","b"])')
case "$MO3" in
	'[a,{'*'},b]') : ;;
	*) bad "mod_only: a の擬似が位置のまま通っていない '$MO3'" ;;
esac
#   ★ 陰性対照: b 側の擬似は「許す名前」にならない (a の名前を通さない)
[ "$(mo 3c 'mod_only(["a","b"], [{type:"pseudo_module",name:"pm_x"}])')" = "[]" ] || bad "mod_only: b 側の擬似が名前として効いている"
[ "$(mo 4 'mod_only("", ["cgal"])')" = "[]" ] || bad "mod_only: スカラの穴が [] にならない"
[ "$(mo 5 'mod_only(["cgal","cgal"], ["cgal"])')" = "[cgal,cgal]" ] || bad "mod_only: a の重複が落ちている"
[ "$(mo 6 'mod_only(["cgal"], [])')" = "[]" ] || bad "mod_only: b が空なら [] になるはず"
#   ★ 1 引数形: 左辺を補う (宣言があればそれ / 無ければ modules())
[ "$(mo 7 'mod_only(["cgal","manifold"])')" = "[cgal]" ] || bad "mod_only(1 引数): 宣言 [occt,cgal] ∩ [cgal,manifold] にならない"
O1=$(val "$D-g8" "$MOD print(\"VAL\", mod_only([\"cgal\",\"occt\"]));")
[ "$O1" = "[cgal,occt]" ] || bad "mod_only(1 引数): 宣言が無いとき modules() ∩ sup にならない '$O1'"
O2=$(val "$D-g9" "$MOD use \"\"; print(\"VAL\", mod_only([\"cgal\"]));")
[ "$O2" = "[cgal]" ] || bad "mod_only(1 引数): use \"\" (穴) が「宣言なし」に倒れていない '$O2'"
say "  9) mod_only = 積 (a の順・平坦化・穴は落ちる・**a の擬似は通す**・重複は残る) + 1 引数形 3 件"

# ---- ⑩ mod_only_names — mod_only との違いは **擬似を落とす**ことだけ ----
mn() { val "$D-h$1" "$MOD use [\"occt\",\"cgal\"]; print(\"VAL\", $2);"; }
#   ★ 対で取る: 同じ入力で mod_only は通し / mod_only_names は落とす
PS='["a",{type:"pseudo_module",name:"pm_x"},"b"]'
[ "$(mn 1 "mod_only_names($PS, [\"a\",\"b\"])")" = "[a,b]" ] || bad "mod_only_names: 擬似が落ちていない"
case "$(mn 2 "mod_only($PS, [\"a\",\"b\"])")" in
	'[a,{'*'},b]') : ;;
	*) bad "対照が崩れた: mod_only 側が擬似を通していない" ;;
esac
#   ★ 左辺の補い方は mod_only と **同一** (1 引数形・宣言あり / 宣言なし)
[ "$(mn 3 'mod_only_names(["cgal","manifold"])')" = "[cgal]" ] || bad "mod_only_names(1 引数): 宣言を尊重しない"
N1=$(val "$D-h4" "$MOD print(\"VAL\", mod_only_names([\"cgal\",\"occt\"]));")
N2=$(val "$D-h5" "$MOD print(\"VAL\", mod_only([\"cgal\",\"occt\"]));")
[ "$N1" = "$N2" ] || bad "1 引数形の左辺の補い方が mod_only と違う names='$N1' only='$N2'"
#   ★ use の診断の札が **実際に書いた op 名**になる (派生を先に見ているか)
o=$(run "$D-h6" "$MOD var f = \\(){ use mod_only_names([\"nosuch\"]); type_of(sphere(1.5)); };
use [\"cgal\"]; print(f());")
echo "$o" | grep -q "use mod_only_names(...): none of the modules in effect" ||
	{ bad "診断の札が mod_only_names になっていない"; echo "$o" | grep -i error | head -1; }
#   ★ 陰性対照: mod_only 側は mod_only と名乗る
o=$(run "$D-h7" "$MOD var g = \\(){ use mod_only([\"nosuch\"]); type_of(sphere(1.5)); };
use [\"cgal\"]; print(g());")
echo "$o" | grep -q "use mod_only(...): none of the modules in effect" ||
	{ bad "mod_only 側の札が変わってしまった"; echo "$o" | grep -i error | head -1; }
say " 10) mod_only_names = 擬似を落とす形 (対で 2 件 + 左辺の補い方が同一 + 診断の札 2 件)"

[ "$NG" -eq 0 ] && echo "USE-LIST-OK"
exit "$NG"
