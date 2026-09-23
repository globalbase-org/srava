#!/bin/sh
# 候補列 — 指名を **並び** として読む (#3555 段1)。
# $1 = srava 実行体。env: SRAVA_AGENT, SRAVA_CACHE_DIR。
#
# ★ 何を守るテストか
#   ① 配列の順が **priority を上書き**する (occt=2 < manifold=10 < cgal=20 なので、
#      ["occt",...] が occt に解決されたら順が効いた証拠)
#      ★ 陰性対照: 同じ式を指名なしで回すと従来どおり cgal が答える
#   ② **列に無いモジュールは呼ばれない** (occt しか持たない op を occt 抜きの列で呼ぶ → エラー)
#   ③ 未ロードの名前は **飛ばす** (構成差で列の一部が落ちても残りで走る)
#      ただし **全部未解決ならエラー** ⇒ 誤字が黙って priority 順に戻らない
#   ④ 空の候補列は **必ずエラー** ("" = 指名なし とは別の口)
#   ⑤ 一部が op を持たないだけなら **飛ばす** (候補列は優先順位表であって全員への要求ではない)
#   ⑥ キャッシュキーは **実際に何が走ったか**で決まる (配列で書いても同じ解決なら同じキー)
#   ⑦ n 項の **分解経路** (try_decompose) にも候補列が効く
#   ⑨ 穴 (null / "" / 0) は **読み飛ばす**。全部穴なら空配列と同じエラー (#3555 段5)
#      ★ 文字列 "0" は穴でない (名前として落ちる) = 数値 0 と書き分けられる
#   ⑩ `use 式;` は `var USE_MODULES = 式;` と **同じ**。スコープも var と同じ
#   ⑪ modules() は名前の配列 / modules("priority") は従来の文字列
#      ★★ 不変条件: `use modules();` は **答えを変えない** (列の先勝ち = priority 最大)
#   ⑫ 入れ子は **平坦化** (#3573) — ["a",["b","c"]] ≡ ["a","b","c"]
#      ★ 列の断片を変数にして並べられる = #3574 (擬似モジュール集) の前提
#      ⚠ 空の入れ子 [[]] は穴ではなく「要素 0 個」・深さは上限で止める (自己参照が書けるため)
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

# ---- ① 配列の順が priority を上書きする ----
#   球は occt が **厳密**なのでメッシュ系と値が構造的に違う ⇒ どちらが走ったかが値で分かる。
CG=$(val "$D-b1" "$MOD print(\"VAL\", volume(\"cgal\"::sphere(1.5)));")
OC=$(val "$D-b2" "$MOD print(\"VAL\", volume(\"occt\"::sphere(1.5)));")
DF=$(val "$D-b3" "$MOD print(\"VAL\", volume(sphere(1.5)));")
AO=$(val "$D-b4" "$MOD var u = [\"occt\",\"cgal\"]; print(\"VAL\", volume(u::sphere(1.5)));")
AC=$(val "$D-b5" "$MOD var u = [\"cgal\",\"occt\"]; print(\"VAL\", volume(u::sphere(1.5)));")
[ -n "$CG" ] && [ -n "$OC" ] && [ "$CG" != "$OC" ] || bad "前提が崩れた cgal='$CG' occt='$OC'"
[ "$DF" = "$CG" ] || bad "陰性対照: 指名なしが既定 (cgal) でない '$DF' vs '$CG'"
[ "$AO" = "$OC" ] || bad "[occt,cgal] が occt に解決されない '$AO' vs occt='$OC'"
[ "$AC" = "$CG" ] || bad "[cgal,occt] が cgal に解決されない '$AC' vs cgal='$CG'"
say "  1) 配列の順が priority(cgal=20 > occt=2) を上書き: [occt,..]=$AO [cgal,..]=$AC"

# ---- ②③④⑤ エラーと「飛ばす」の境目 ----
errtest() {   # errtest <id> <source> <期待する語>
	o=$(run "$D-c$1" "$MOD $2" | grep -c "$3")
	[ "$o" -ge 1 ] || { bad "エラー文に '$3' が出ない: $2"; run "$D-c$1" "$MOD $2" | grep -i 'error' | head -2; }
}
# ② fillet は occt だけが持つ ⇒ occt を含まない列では **呼ばれない**
errtest 1 'var u = ["cgal","manifold"]; print(volume(u::fillet("occt"::box(2,2,2), 0.1)));' \
                                                        'none of them implements op'
# ③ 全部未解決 — 1 要素は従来の "ocdt"::op と同じ文言
errtest 2 'var u = ["nosuch"]; print(volume(u::box(2,2,2)));'          'no such module is loaded'
errtest 3 'var u = ["nosuch","alsono"]; print(volume(u::box(2,2,2)));' 'no such module is loaded'
# ④ 空の候補列
errtest 4 'var u = []; print(volume(u::box(2,2,2)));'                  'candidate list is empty'
say "  2) 列に無い / 全部未解決 / 空 は明示エラー (4 件)"

# ③' 未ロードは飛ばす: 先頭が存在しなくても残りで走る (occt に解決される)
SK=$(val "$D-c5" "$MOD var u = [\"nosuch\",\"occt\"]; print(\"VAL\", volume(u::sphere(1.5)));")
[ "$SK" = "$OC" ] || bad "未ロード名を飛ばして occt に落ちない '$SK' vs occt='$OC'"
# ⑤ op を持たないだけなら飛ばす: manifold は fillet を持たないが occt が居るので走る
#   ⚠ fillet の sig は oc-brep3d しか受けないので、入力も occt で作る (cgal の box では
#     どのみち routing できず、「飛ばした」ことの検定にならない)。
FL=$(val "$D-c6" "$MOD var u = [\"manifold\",\"occt\"]; print(\"VAL\", volume(u::fillet(\"occt\"::box(2,2,2), 0.1)));")
FD=$(val "$D-c7" "$MOD print(\"VAL\", volume(fillet(\"occt\"::box(2,2,2), 0.1)));")
[ -n "$FL" ] && [ "$FL" = "$FD" ] || bad "op を持たない候補で止まっている fillet='$FL' 指名なし='$FD'"
say "  3) 未ロード / op 無しの候補は飛ばす: nosuch→occt=$SK fillet=$FL"

# ---- ⑥ キャッシュキーは「実際に何が走ったか」で決まる ----
P='var m = %sbox(2,2,2) ||| %sbox(1,1,3); print("VAL", volume(m));'
prog() { printf "$P" "$1" "$1"; }
keytest() {   # keytest <id> <前置き> <修飾1> <修飾2> <same|diff>
	d="$D-k$1"; rm -rf "$d"
	hitmiss "$d" "$MOD $2 $(prog "$3")" >/dev/null
	r=$(hitmiss "$d" "$MOD $2 $(prog "$4")")
	h=$(echo "$r" | cut -d' ' -f1); m=$(echo "$r" | cut -d' ' -f2)
	case "$5" in
	same) [ "$m" = "0" ] && [ "$h" != "0" ] || bad "同一キーのはずが miss=$m hit=$h ($3 vs $4)" ;;
	diff) [ "$m" != "0" ] || bad "別キーのはずが全 hit ($3 vs $4)" ;;
	esac
}
keytest 1 'var u = ["cgal","occt"];' 'u::'      '"cgal"::' same   # 同じ解決 = 同一キー
keytest 2 'var u = ["occt","cgal"];' 'u::'      '"occt"::' same   # 先頭が occt なら occt のキー
keytest 3 'var u = ["occt","cgal"];' 'u::'      '"cgal"::' diff   # 別モジュール = 別キー
say "  4) 配列で書いても キーは解決結果で決まる (3 件)"

# ---- ⑦ 分解経路 (try_decompose) にも効く ----
#   3 項の ||| は 2 項の木へ分解される。子ノードは指名式 (配列) を引き継ぐ。
T3=$(val "$D-d1" "$MOD var u = [\"occt\",\"cgal\"];
print(\"VAL\", volume(u::box(2,2,2) ||| u::box(1,1,3) ||| u::box(3,1,1)));")
Q3=$(val "$D-d2" "$MOD print(\"VAL\", volume(\"occt\"::box(2,2,2) ||| \"occt\"::box(1,1,3) ||| \"occt\"::box(3,1,1)));")
[ -n "$T3" ] && [ "$T3" = "$Q3" ] || bad "3 項分解で候補列が効かない 配列='$T3' テキスト='$Q3'"
say "  5) n 項の分解でも候補列が効く: $T3"

# ---- ⑧ USE_MODULES (#3555 段2) ----
#   指名を省略したときの候補列。CACHE_DIR と同じ流儀の予約変数 (事前定義 + 代入で上書き)。
UM=$(val "$D-u1" "$MOD USE_MODULES = [\"occt\",\"cgal\"];
print(\"VAL\", volume(sphere(1.5)));")
[ "$UM" = "$OC" ] || bad "USE_MODULES が効かない '$UM' vs occt='$OC'"
#   ★ 陰性対照は ① の DF (未設定なら従来どおり priority 順 = cgal)。
[ "$DF" = "$CG" ] || bad "陰性対照: USE_MODULES 未設定で既定が変わっている"

#   スコープ: **var** で束縛すると block / lambda の中だけ変わり、抜けると戻る。
#   ⚠ var の無い代入は set_var = 外側の束縛を書き換えるので「抜けると戻る」は起きない (言語の規則)。
SC=$(run "$D-u2" "$MOD USE_MODULES = [\"occt\",\"cgal\"];
print(\"VAL\", volume(sphere(1.5)));
{ var USE_MODULES = [\"cgal\"]; print(\"VAL\", volume(sphere(1.5))); }
print(\"VAL\", volume(sphere(1.5)));
var f = \\(){ var USE_MODULES = [\"cgal\"]; return volume(sphere(1.5)); };
print(\"VAL\", f());
print(\"VAL\", volume(sphere(1.5)));" | sed -n 's/^VAL //p')
EXP="$OC
$CG
$OC
$CG
$OC"
[ "$SC" = "$EXP" ] || { bad "USE_MODULES のスコープが効かない"; echo "--- got ---"; echo "$SC"; echo "--- want ---"; echo "$EXP"; }
say "  6) USE_MODULES が効き、var 束縛は block / lambda の中だけ (5 地点)"

#   指名は USE_MODULES に **勝つ** / ""::op は「planner に任せる」= USE_MODULES を見る
#   ⚠⚠ USE_MODULES は **式に出てくる全部の op** に効く。列に volume の実行者が居ないと、
#     中で別カーネルを指名した時点で外の volume が行き場を失う:
#         USE_MODULES = ["occt"]; volume("cgal"::sphere(..))
#           → ERROR: op 'volume' does not accept input type(s) cg-mesh3d in any candidate
#     ⇒ 列には **その計算に登場するカーネルを全部**入れる (ここでは occt と cgal)。
#     ★ これは候補列の規則どおり (列に無いものは呼ばれない) であって、取りこぼしではない。
QW=$(val "$D-u3" "$MOD USE_MODULES = [\"occt\",\"cgal\"]; print(\"VAL\", volume(\"cgal\"::sphere(1.5)));")
EQ=$(val "$D-u4" "$MOD USE_MODULES = [\"occt\",\"cgal\"]; print(\"VAL\", volume(\"\"::sphere(1.5)));")
[ "$QW" = "$CG" ] || bad "指名が USE_MODULES に勝たない '$QW' vs cgal='$CG'"
[ "$EQ" = "$OC" ] || bad "\"\"::op が USE_MODULES を見ていない '$EQ' vs occt='$OC'"

#   環境変数 SRAVA_USE_MODULES (配列を持てないので ',' 区切り)
EV=$(rm -rf "$D-u5"; SRAVA_USE_MODULES="occt, cgal" SRAVA_CACHE_DIR="$D-u5" SRAVA_CACHE_RETAIN=all \
	SRAVA_SOURCE="$MOD print(\"VAL\", volume(sphere(1.5)));" "$SRAVA" 2>&1 | sed -n 's/^VAL //p')
[ "$EV" = "$OC" ] || bad "SRAVA_USE_MODULES が効かない '$EV' vs occt='$OC'"
say "  7) 指名 > USE_MODULES > priority / \"\"::op と環境変数も同じ規則"

#   エラーの主語が **USE_MODULES** になる (直す場所が指名とは違うため)
umerr() {   # umerr <id> <USE_MODULES の値> <期待する語>
	o=$(run "$D-ue$1" "$MOD USE_MODULES = $2; print(volume(box(2,2,2)));" | grep -c "$3")
	[ "$o" -ge 1 ] || { bad "USE_MODULES=$2 のエラー文に '$3' が出ない"; \
		run "$D-ue$1" "$MOD USE_MODULES = $2; print(volume(box(2,2,2)));" | grep -i 'error' | head -1; }
}
umerr 1 '[]'                 'USE_MODULES is empty'
umerr 2 '["ocdt"]'           "USE_MODULES ('ocdt'): no such module is loaded"
say "  8) USE_MODULES 由来のエラーは主語が USE_MODULES (2 件)"

# ---- ⑨ 穴 (null / "" / 0) を読み飛ばす (#3555 段5) ----
#   狙いは「枠を先に敷いて、条件で埋めた所だけ使う」。srava に三項演算子は無く && || は 1/0 を
#   返すので、条件で要素を落とすにはこの形しか書けない。
holetest() {   # holetest <id> <USE_MODULES の値> <期待する値>
	g=$(val "$D-h$1" "$MOD USE_MODULES = $2; print(\"VAL\", volume(sphere(1.5)));")
	[ "$g" = "$3" ] || bad "穴 $2 → '$g' (want '$3')"
}
holetest 1 '["", "occt", "cgal"]'   "$OC"   # 先頭の空文字
holetest 2 '["occt", 0, "cgal"]'    "$OC"   # 途中の数値 0
holetest 3 '[0, "cgal"]'            "$CG"
# 添字伸長が空けた穴 = null
NH=$(val "$D-h4" "$MOD var a = []; a[2] = \"occt\"; USE_MODULES = a;
print(\"VAL\", volume(sphere(1.5)));")
[ "$NH" = "$OC" ] || bad "null の穴を飛ばさない '$NH' vs occt='$OC'"
# 枠を先に敷いて条件で埋める (docs の形そのもの)
FR=$(val "$D-h5" "$MOD var u = [\"\", \"\", \"cgal\"]; u[0] = \"occt\";
print(\"VAL\", volume(sphere(1.5)));
USE_MODULES = u; print(\"VAL\", volume(sphere(1.5)));" | sed -n '2p')
[ "$FR" = "$OC" ] || bad "枠 [\"\",\"\",\"cgal\"] の 0 番を埋めた列が効かない '$FR' vs occt='$OC'"
# ★ スカラの穴は **指名なし** (配列とは意味が違う)
holetest 6 '0'    "$CG"
holetest 7 '""'   "$CG"
# ⚠ 文字列 "0" は穴ではない = 名前として落ちる (数値 0 と書き分けられる)
umerr 3 '"0"'  "USE_MODULES ('0'): no such module is loaded"
# ★ 全部穴 → 空配列と同じエラー。ただし「穴だった」と言う (画面には [] に見えないので)
umerr 4 '["", 0]'  'USE_MODULES is empty'
umerr 5 '["", 0]'  'element(s) are holes'
say "  9) 穴 (null / \"\" / 0) は飛ばす・スカラは指名なし・全部穴はエラー (9 件)"

# ---- ⑩ `use 式;` = `var USE_MODULES = 式;` (#3555 段5) ----
US=$(val "$D-s1" "$MOD use [\"occt\",\"cgal\"]; print(\"VAL\", volume(sphere(1.5)));")
[ "$US" = "$OC" ] || bad "use 文が効かない '$US' vs occt='$OC'"
#   ★ var と同じ DEF なので block / lambda の中だけ差し替わり、抜けると戻る
SU=$(run "$D-s2" "$MOD use [\"occt\",\"cgal\"];
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
[ "$SU" = "$SE" ] || { bad "use のスコープが var USE_MODULES と違う"; echo "--- got ---"; echo "$SU"; echo "--- want ---"; echo "$SE"; }
#   use "" で指名なしへ戻せる
UR=$(val "$D-s3" "$MOD use [\"occt\"]; { use \"\"; print(\"VAL\", volume(sphere(1.5))); }")
[ "$UR" = "$CG" ] || bad "use \"\" が指名なしへ戻らない '$UR' vs cgal='$CG'"
#   ⚠ use は **予約語**: 識別子には使えない
o=$(run "$D-s4" 'var use = 3;' | grep -c 'parse error')
[ "$o" -ge 1 ] || bad "use が予約語になっていない (var use = 3; が通る)"
say " 10) use 文 = var USE_MODULES (スコープ 5 地点 + \"\" 復帰 + 予約語)"

# ---- ⑪ modules() の 2 つの顔 (#3555 段5) ----
AR=$(val "$D-m1" "$MOD print(\"VAL\", modules());")
PR=$(val "$D-m2" "$MOD print(\"VAL\", modules(\"priority\"));")
case "$AR" in
\[*\]) ;; *) bad "modules() が配列を返さない '$AR'" ;;
esac
case "$AR" in *cgal*) ;; *) bad "modules() に cgal が無い '$AR'" ;; esac
case "$AR" in *delayed*) bad "modules() に番兵 delayed が出ている '$AR'" ;; esac
#   ⚠ 組込 "pig" (op 行を持たない = dispatch が構造的に選べない) は配列に出さない。
#     ★ 陰性対照: 文字列版には出る (= 検出器が "pig" を見つけられることの較正)
case "$AR" in *pig*) bad "modules() の配列に組込 pig が出ている '$AR'" ;; esac
case "$PR" in *pig:0*) ;; *) bad "modules(\"priority\") に pig が出ない (検出器が当たらない) '$PR'" ;; esac
#   ★ 列に "pig" を手で書いても診断が壊れないこと (supports_op は -1 を返すが op_row は 0)
o=$(run "$D-m7" "$MOD USE_MODULES = [\"cgal\",\"manifold\",\"pig\"];
print(volume(fillet(\"occt\"::box(2,2,2), 0.1)));" | grep -c "none of them implements op")
[ "$o" -ge 1 ] || { bad "列に pig を入れると 'none of them implements op' が消える"; \
	run "$D-m7" "$MOD USE_MODULES = [\"cgal\",\"manifold\",\"pig\"];
print(volume(fillet(\"occt\"::box(2,2,2), 0.1)));" | grep -i error | head -1; }
#   ★ pig を外した列でも codec 経路 (export / キャッシュ HIT) は無傷
run "$D-m8" "$MOD USE_MODULES = [\"cgal\",\"occt\"]; export(\"$D-m8.stl\", box(2,2,2));" >/dev/null
[ -s "$D-m8.stl" ] || bad "pig を外した列で export が書けていない"
rm -f "$D-m8.stl"
case "$PR" in *cgal:*) ;; *) bad "modules(\"priority\") が name:priority でない '$PR'" ;; esac
case "$PR" in *delayed:0*) ;; *) bad "modules(\"priority\") が従来の形 (delayed を含む) でない '$PR'" ;; esac
#   ★★ 不変条件: use modules(); は **答えを変えない** (列の先勝ち = priority 最大)
#     ⚠ これが崩れると「内省の答えで配線したら結果が変わる」= 内省が実際の routing を映していない。
IV=$(val "$D-m3" "$MOD use modules(); print(\"VAL\", volume(sphere(1.5)));")
[ "$IV" = "$DF" ] || bad "use modules(); が既定の答えを変えた '$IV' vs 指名なし='$DF'"
#     priority を動かしても追随する (manifold を上げると manifold が先頭 → cg でなく mf の答え)
MP=$(val "$D-m4" "$MOD module(\"manifold.so\",{priority:99}); use modules();
print(\"VAL\", volume(sphere(1.5)));")
MD=$(val "$D-m5" "$MOD module(\"manifold.so\",{priority:99});
print(\"VAL\", volume(sphere(1.5)));")
[ -n "$MP" ] && [ "$MP" = "$MD" ] || bad "priority を上げた後の use modules(); が追随しない '$MP' vs '$MD'"
#   ★ use modules(); は「いまの順を **固定**する」= 以降 module() で priority を動かしても
#     候補列は動かない (配列は 1 度だけ評価されるため)。⚠ 陰性対照は use 無しの同じ式。
PIN=$(val "$D-m9" "$MOD use modules(); module(\"occt.so\",{priority:99});
print(\"VAL\", volume(sphere(1.5)));")
NOP=$(val "$D-m10" "$MOD module(\"occt.so\",{priority:99});
print(\"VAL\", volume(sphere(1.5)));")
[ "$PIN" = "$CG" ] || bad "use modules(); の後の priority 変更が候補列を動かした '$PIN' vs cgal='$CG'"
[ "$NOP" = "$OC" ] || bad "陰性対照: use 無しなら priority 99 が効くはず '$NOP' vs occt='$OC'"
#   未知の形は **エラー** (黙って配列に倒さない)
o=$(run "$D-m6" "$MOD print(modules(\"prio\"));" | grep -c "unknown form")
[ "$o" -ge 1 ] || bad "modules(\"prio\") が明示エラーにならない"
say " 11) modules() = 名前の配列 / modules(\"priority\") = 従来の文字列・use modules() は不変"

# ---- ⑫ 入れ子は **平坦化**する (#3573) ----
#   ★ 何を守るか: ["cgal",["occt"],"geogram"] ≡ ["cgal","occt","geogram"]。
#     狙いは *列の断片に名前を付けて並べる* 書き方 (= #3574 の擬似モジュールの前提)。
#   ★★ 陰性対照の取り方: 平坦化の前は入れ子が **1 つの名前**として読まれ
#     ("[occt,cgal]" という名前のモジュールは無い) エラーになっていた。⇒ 下の ne1 は
#     「通るようになった形」そのもの。**通る**ことが変更の当たりで、値が occt であることが
#     「平坦化した順で解決した」ことの証拠になる (先頭が cgal に化けていないか見ている)。
flat() {   # flat <id> <USE_MODULES の値> <期待する値> <説明>
	g=$(val "$D-f$1" "$MOD USE_MODULES = $2; print(\"VAL\", volume(sphere(1.5)));")
	[ "$g" = "$3" ] || bad "平坦化 $4: $2 → '$g' (want '$3')"
}
flat 1 '[["occt","cgal"]]'      "$OC" "入れ子だけ (前はエラーだった形)"
flat 2 '["occt",["cgal"]]'      "$OC" "後ろが入れ子"
flat 3 '[["occt"],"cgal"]'      "$OC" "前が入れ子"
flat 4 '[["cgal"],"occt"]'      "$CG" "前が入れ子 (順が逆なら答えも逆)"
flat 5 '[[[["occt"]]],"cgal"]'  "$OC" "4 重"
#   ★ 平坦化の等価性: 入れ子で書いた列と、手で開いた列が **同じ答え**になる
E1=$(val "$D-f6" "$MOD USE_MODULES = [\"nosuch\",[\"occt\",\"cgal\"]];
print(\"VAL\", volume(sphere(1.5)));")
E2=$(val "$D-f7" "$MOD USE_MODULES = [\"nosuch\",\"occt\",\"cgal\"];
print(\"VAL\", volume(sphere(1.5)));")
[ -n "$E1" ] && [ "$E1" = "$E2" ] || bad "入れ子と手で開いた列が一致しない '$E1' vs '$E2'"
#   ★★ 本題の書き方: **列の断片を変数にして並べる** (これができないと #3574 が書けない)
FR1=$(val "$D-f8" "$MOD var brep = [\"occt\"]; var mesh = [\"cgal\",\"manifold\"];
use [brep, mesh]; print(\"VAL\", volume(sphere(1.5)));")
FR2=$(val "$D-f9" "$MOD var brep = [\"occt\"]; var mesh = [\"cgal\",\"manifold\"];
use [mesh, brep]; print(\"VAL\", volume(sphere(1.5)));")
[ "$FR1" = "$OC" ] || bad "use [brep, mesh] が occt に解決されない '$FR1' vs occt='$OC'"
[ "$FR2" = "$CG" ] || bad "use [mesh, brep] が cgal に解決されない '$FR2' vs cgal='$CG'"
#   ★ `::` 指名でも同じ (口が 2 つある — use と 指名式)
QF=$(val "$D-f10" "$MOD var u = [[\"occt\"],\"cgal\"]; print(\"VAL\", volume(u::sphere(1.5)));")
[ "$QF" = "$OC" ] || bad "指名式 u::op の入れ子が平坦化されない '$QF' vs occt='$OC'"
#   ⚠ 空の入れ子は **穴ではない** — 「要素 0 個の列」なので holes の文言を出さない
o=$(run "$D-f11" "$MOD USE_MODULES = [[]]; print(volume(box(2,2,2)));")
case "$o" in *"USE_MODULES is empty"*) ;; *) bad "[[]] が空の候補列にならない"; echo "$o" | grep -i error | head -1 ;; esac
case "$o" in *"are holes"*) bad "[[]] が穴として数えられている (空配列と穴は別)" ;; esac
#   ⚠ 入れ子の **中**の穴はちゃんと穴として数える (平坦化してから穴を見る)
flat 12 '[["", 0], "cgal"]'  "$CG" "入れ子の中の穴を飛ばす"
umerr 6 '[["", null], 0]'  'USE_MODULES is empty'
umerr 7 '[["", null], 0]'  'all 3 element(s) are holes'
#   ★★ 深さの上限で **止まる** — 自己参照する配列は言語が禁じていないので、
#     ガードが無いとスタックを食い潰す。⚠ 黙って打ち切らずエラーにする。
o=$(run "$D-f13" "$MOD var a = [\"cgal\"]; a[1] = a; USE_MODULES = a;
print(volume(box(2,2,2)));" | grep -c 'nested more than')
[ "$o" -ge 1 ] || { bad "自己参照する候補列が深さ上限で止まらない"; \
	run "$D-f13" "$MOD var a = [\"cgal\"]; a[1] = a; USE_MODULES = a;
print(volume(box(2,2,2)));" | grep -i error | head -1; }
#     ★ 陰性対照: 止まるのが **深さ** のせいであることを、境目の両側で示す。
#       上限 8 = 「入れ子 9 重までは読む」(一番外が深さ 0)。⇒ 9 重は通り、10 重で止まる。
#       ⚠ 括弧は手で数えると必ず間違える (最初に 1 つずれた) ので **組み立てる**。
nest() {   # nest <重さ> → [[...["occt"]...]]
	i=0; s='"occt"'
	while [ "$i" -lt "$1" ]; do s="[$s]"; i=$((i+1)); done
	echo "$s"
}
flat 14 "$(nest 9)"  "$OC" "9 重はまだ通る (上限ちょうど)"
o=$(run "$D-f15" "$MOD USE_MODULES = $(nest 10);
print(volume(box(2,2,2)));" | grep -c 'nested more than')
[ "$o" -ge 1 ] || { bad "10 重が深さ上限で止まらない (上限が効いていない)"; \
	run "$D-f15" "$MOD USE_MODULES = $(nest 10);
print(volume(box(2,2,2)));" | grep -i error | head -1; }
say " 12) 入れ子は平坦化 (等価性 / 断片の合成 / :: / 空と穴の区別 / 深さ上限 18 件)"

[ "$NG" -eq 0 ] && echo "MODULE-CANDLIST-OK"
exit "$NG"
