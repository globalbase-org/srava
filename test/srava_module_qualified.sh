#!/bin/sh
# `module::op(...)` — op ごとのモジュール指名 (#3467)。
# $1 = srava 実行体。env: SRAVA_AGENT, SRAVA_CACHE_DIR。
#
# ★ 何を守るテストか
#   ① 指名が実際に効く (別モジュールに解決される = 値が変わる)
#   ② ★★ キャッシュキーは **どう書かれたか** ではなく **実際に何が走ったか** で決まる
#      ""::op == op / "cgal"::op == op (既定が cgal のとき) / 既定が変われば別キー
#   ③ 解決できないときは **明示エラー**。原因 3 種を分けて言う (直す場所が違うため)
#   ④ 変数形が **1 プロセスの中で** 反復ごとに解決される
#      = priority による切替 (プロセス全体で 1 カーネル) では書けなかったものが書ける
#   ⑤ ★★ #3568: 指名を書いても cast / import / export の **専用診断**に届く
#      指名の汎用文言 (「入力型を受け付けない」) が専用診断を覆っていた
SRAVA="${1:?srava binary not given}"
D="${SRAVA_CACHE_DIR:?SRAVA_CACHE_DIR not set}"
# ★★ #3522: ハングの番犬 (共通・常時 ON)。詳細は test/srava_hangwatch.sh。
. "$(dirname "$0")/srava_hangwatch.sh"
MOD='module("cgal.so",{}); module("manifold.so",{}); module("occt.so",{});'
NG=0
say() { echo "$1"; }
bad() { echo "FAIL($1)"; NG=1; }

run() {   # run <cache dir> <source>  → stdout そのまま
	rm -rf "$1"
	SRAVA_CACHE_DIR="$1" SRAVA_CACHE_RETAIN=all SRAVA_SOURCE="$2" "$SRAVA" 2>&1
}
val() { run "$1" "$2" | sed -n 's/^VAL //p'; }
hitmiss() {   # hitmiss <cache dir(消さない)> <source> → "H M"
	SRAVA_CACHE_DIR="$1" SRAVA_CACHE_RETAIN=all SRAVA_SOURCE="$2" "$SRAVA" 2>&1 |
		sed -n 's/.*cache: \([0-9]*\) hit(s), \([0-9]*\) miss(es).*/\1 \2/p'
}

# ---- ① 指名が効く: occt の球は **厳密**なので、メッシュ系と値が構造的に違う ----
CG=$(val "$D-a1" "$MOD print(\"VAL\", volume(\"cgal\"::sphere(1.5)));")
OC=$(val "$D-a2" "$MOD print(\"VAL\", volume(\"occt\"::sphere(1.5)));")
DF=$(val "$D-a3" "$MOD print(\"VAL\", volume(sphere(1.5)));")
EM=$(val "$D-a4" "$MOD print(\"VAL\", volume(\"\"::sphere(1.5)));")
[ -n "$CG" ] && [ -n "$OC" ] || bad "指名した呼び出しが値を出さない cgal='$CG' occt='$OC'"
[ "$CG" != "$OC" ] || bad "occt 指名が効いていない (cgal と同値 $CG)"
[ "$DF" = "$CG" ]  || bad "既定 (cgal) と \"cgal\":: が別値 '$DF' vs '$CG'"
[ "$EM" = "$DF" ]  || bad "\"\":: が既定と別値 '$EM' vs '$DF'"
say "  1) 指名で解決先が変わる: cgal=$CG occt=$OC"

# ---- ② キャッシュキーの不変条件 ----
#   同じ dir に 2 回流し、2 回目が全 hit なら同一キー / miss を含めば別キー。
P='var m = %sbox(2,2,2) ||| %sbox(1,1,3); print("VAL", volume(m));'
prog() { printf "$P" "$1" "$1"; }
keytest() {   # keytest <id> <module 宣言> <修飾1> <修飾2> <same|diff>
	d="$D-k$1"; rm -rf "$d"
	hitmiss "$d" "$2 $(prog "$3")" >/dev/null
	r=$(hitmiss "$d" "$2 $(prog "$4")")
	h=$(echo "$r" | cut -d' ' -f1); m=$(echo "$r" | cut -d' ' -f2)
	case "$5" in
	same) [ "$m" = "0" ] && [ "$h" != "0" ] || bad "同一キーのはずが miss=$m hit=$h ($3 vs $4)" ;;
	diff) [ "$m" != "0" ] || bad "別キーのはずが全 hit ($3 vs $4)" ;;
	esac
}
MODP='module("cgal.so",{}); module("manifold.so",{priority:99}); module("occt.so",{});'
keytest 1 "$MOD"  ''         '""::'      same   # ""::op == op
keytest 2 "$MOD"  ''         '"cgal"::'  same   # 既定が cgal なら "cgal"::op == op
keytest 3 "$MODP" ''         '"cgal"::'  diff   # 既定が manifold なら別の計算 = 別キー
keytest 4 "$MOD"  '"cgal"::' '"occt"::'  diff   # 別モジュール = 別キー
keytest 5 "$MOD"  '"cgal"::' '"cgal"::'  same   # 同じ指名は当然同一
say "  2) キーは「実際に何が走ったか」で決まる (5 件)"

# ---- ③ エラー 3 種 + planner builtin の指名 ----
errtest() {   # errtest <id> <source> <期待する語>
	o=$(run "$D-e$1" "$MOD $2" | grep -c "$3")
	[ "$o" -ge 1 ] || { bad "エラー文に '$3' が出ない: $2"; run "$D-e$1" "$MOD $2" | grep ERROR | head -1; }
}
errtest 1 'print(volume("nosuch"::box(2,2,2)));'                       'no such module is loaded'
errtest 2 'print(volume("manifold"::fillet(box(2,2,2), 0.1)));'        'does not implement op'
errtest 3 'var a = "manifold"::box(2,2,2); print(volume("occt"::union(a, "occt"::box(1,1,1))));' \
                                                                       'does not accept input type'
errtest 4 'print("x"::length([1,2,3]));'                               'is not a module op'
say "  3) エラー 4 種を分けて言う"

# ---- ④ 変数形: **1 プロセスの中で** 全カーネルを回して突き合わせる ----
#   ★ priority による切替はプロセス全体に効くので、この形は `::` でしか書けない。
OUT=$(run "$D-loop" "$MOD
var ks = [\"cgal\",\"manifold\",\"occt\"];
for (var i = 0; i < 3; i = i + 1) { var k = ks[i]; print(\"VAL\", k, volume(k::box(2,2,3))); }")
N=$(echo "$OUT" | sed -n 's/^VAL //p' | wc -l)
[ "$N" -eq 3 ] || bad "変数形ループが 3 行出ない ($N 行)"
# box は 3 カーネルとも厳密 (平面 6 枚) なので **完全一致** すべき。
U=$(echo "$OUT" | sed -n 's/^VAL [a-z]* //p' | sort -u | wc -l)
[ "$U" -eq 1 ] || { bad "box が 3 カーネルで一致しない"; echo "$OUT" | sed -n 's/^VAL //p'; }
say "  4) 変数形で 1 プロセス内 3 カーネル一致: $(echo "$OUT" | sed -n 's/^VAL [a-z]* //p' | sort -u)"

# ---- ⑤ 指名は **専用診断を覆わない** (#3568) ----
#   ★ 何が壊れていたか: decide_out_module の中で、指名の汎用診断
#     (「op がその入力型を受け付けない」) が cast / import / export の専用診断より
#     **前**に在り、指名が与えられているだけで早期に return していた。⇒ 指名を書くと
#     *直す場所を指さない* 文言に化けた。要求は **出力型 / 拡張子** の話なのに、
#     返る文言は **入力型** の話になる (import は入力 0 個なのに入力型の話をする)。
#   ★ 検定の形: 同じ誤りを **指名なし / 指名あり** の 2 通りで書き、*同じ文言が出る*
#     ことを見る ⇒ 期待文言を書き写さないので、将来文言を直しても検定は生きる。
sigline() {   # sigline <cache dir> <source> → 最初の ERROR 行だけ
	run "$1" "$2" | sed -n 's/^\*\*\* ERROR\[[^]]*\] //p' | sed 's/ \*\*\*$//' | head -1
}
samediag() {   # samediag <id> <指名なしの式> <指名ありの式> <専用診断に出る語>
	a=$(sigline "$D-s$1-a" "$MOD $2")
	b=$(sigline "$D-s$1-b" "$MOD $3")
	[ -n "$a" ] || { bad "指名なしでエラーが出ない: $2"; return; }
	case "$a" in *"$4"*) ;; *) bad "陰性対照が専用診断でない ($4): $a" ; return ;; esac
	[ "$a" = "$b" ] || bad "指名ありで文言が変わる ($4)
    指名なし: $a
    指名あり: $b"
}
# cast: 存在しない型 ⇒ 「その型を産出できるモジュールが無い」(出力型の話)
samediag 1 'print("V", volume(cast("zz-mesh3d", box(2,2,2))));' \
           'print("V", volume("cgal"::cast("zz-mesh3d", box(2,2,2))));' \
           '産出できるモジュールが無い'
# import: 未対応拡張子 ⇒ 「読めるモジュールが無い」(拡張子の話・入力型は 0 個)
samediag 2 'print("V", volume(import("foo.zzz")));' \
           'print("V", volume("cgal"::import("foo.zzz")));' \
           '読めるモジュールが無い'
# export: `::` は付けられない (planner builtin) ので候補列で指名する
samediag 3 'export("'"$D"'-s3.zzz", box(2,2,2));' \
           'USE_MODULES = ["cgal","manifold","occt"]; export("'"$D"'-s3.zzz", box(2,2,2));' \
           '書けるモジュールが無い'
# ★ 陽性対照: 指名の汎用診断は **普通の op では生きている** (並べ替えで殺していない)
errtest 5 'var a = "manifold"::box(2,2,2); print(volume("occt"::union(a, "occt"::box(1,1,1))));' \
                                                                       'does not accept input type'
errtest 6 'USE_MODULES = ["occt"]; print(volume("cgal"::sphere(1.5)));' \
                                                                       'in any candidate'
say "  5) 指名を書いても cast / import / export の専用診断に届く (#3568)"

[ "$NG" -eq 0 ] && echo "MODULE-QUALIFIED-OK"
exit "$NG"
