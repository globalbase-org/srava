#!/bin/sh
# ★★ #3511: **loft / loft_ruled** — 断面の列を通る立体。
#
# $1 = srava 実行体 / $2 = モジュール名 (.so) / $3 = 許容誤差
#
# ---- ★★ 断面の置き場所は op が決めない ----
# 利用者が transform で空間に置いた 2D をそのまま受ける。起票時 (#3511) は
#   (A) 高さの列を渡す / (B) 断面ごとに変換行列 / (C) 3D 閉曲線の新しい型
# で迷っていたが、#3518 の 1 で oc-face3d が z=0 平面の外へ出られるようになったので
# **(B) が型を増やさずに書けるようになった**。⇒ loft は位置を一切引数に取らない。
#
# ---- ★ なぜ loft と loft_ruled を分けるか ----
# 線織面は三角形で厳密に表せる = メッシュ系でも実装できるが、なめらかな方は解析曲面が要る。
# ⇒ 別 op にすると「どのカーネルがどちらを持つか」が #3510 の表に出せる。
# ★★ **2 つが本当に違うことを検査する** — 同じ値なら区別が空振りになる。
SRAVA="$1"
SO="${2:?module .so not given}"
TOL="${3:-1e-9}"
D="${SRAVA_CACHE_DIR:?SRAVA_CACHE_DIR not set}"
# ★★ #3522: ハングの番犬 (共通・常時 ON)。詳細は test/srava_hangwatch.sh。
. "$(dirname "$0")/srava_hangwatch.sh"

run() { rm -rf "$D-$1"; SRAVA_CACHE_DIR="$D-$1" SRAVA_SOURCE="module(\"$SO\",{priority:99});
$2" "$SRAVA" 2>&1; }
chk() {
	ok=$(awk -v g="$2" -v e="$3" -v t="$TOL" 'BEGIN{
		d=g-e; if(d<0)d=-d; s=(e<0?-e:e); if(s<1)s=1; print (g!="" && d/s <= t) ? 1 : 0 }')
	[ "$ok" = "1" ] || { echo "FAIL: $1 が $2 (期待 $3)"; exit 1; }
	n=$((n+1))
}
n=0

# ---- ① 閉形式 ----
#   円柱     pi r^2 h                          = pi*1*4          = 12.566370614359172
#   円錐台   (pi h/3)(r1^2 + r1 r2 + r2^2)     = (pi*4/3)(1+2+4) = 29.321531433504734
#   角柱     2*3*4                             = 24
#   3 枚     円錐台 2 つ (h=2 ずつ)             = 29.321531433504734
O=$(run l1 '
var C0 = circle(1);
var R0 = rect(2,3);
print("VAL", volume(loft(C0, translate(C0,[0,0,4]))));
print("VAL", volume(loft_ruled(C0, translate(circle(2),[0,0,4]))));
print("VAL", volume(loft(R0, translate(R0,[0,0,4]))));
print("VAL", volume(loft_ruled(R0, translate(R0,[0,0,4]))));
print("VAL", volume(loft_ruled(C0, translate(circle(2),[0,0,2]), translate(C0,[0,0,4]))));
print("VAL", valid(loft(C0, translate(C0,[0,0,4]))));
print("VAL", nfaces(loft(C0, translate(C0,[0,0,4]))));
')
# ⚠⚠ **値の分割だけ glob を止める** (2026-09-22)。
#   `set -- $(…)` の $( ) は **クォートできない** (単語分割が要る) のでパス名展開に掛かる。
#   比べる値は `[1,1,0]` 形 = glob の **文字クラス**なので、カレント (ビルド dir) に `1` という
#   名前のファイルが 1 つあるだけで `1` に化け、**「値が違う」という、それらしい文言で赤くなる**
#   (2026-09-21 に実際に踏んだ。⚠ PIG_TIMING は *ファイルパス* を取る env なので
#   `PIG_TIMING=1` と打つと `1` というファイルができる)。
#   ★★ **実行と分割を分ける**のが肝。`set -f; set -- $(cmd)` と囲うと -f が $( ) の中まで効き、
#     cmd がシェル関数だと *その中の glob* まで殺す (実測: count_tus → objs_of の
#     `CMakeFiles/*/link.txt` が読めず「全 0 本」になった)。⇒ 先に変数へ取ってから分割する。
_SPLIT_=$(echo "$O" | sed -n 's/^VAL //p')
set -f
set -- $_SPLIT_
set +f
chk "円柱 (pi r^2 h)"              "$1" 12.566370614359172
chk "円錐台 ruled ((pi h/3)(…))"   "$2" 29.321531433504734
chk "角柱 (2*3*4)"                 "$3" 24
chk "角柱 ruled"                   "$4" 24
chk "3 枚 = 円錐台 2 つ"            "$5" 29.321531433504734
chk "立体として妥当"                "$6" 1
chk "面は 3 枚 (側面 + 蓋 2)"       "$7" 3

# ---- ② ★★ loft と loft_ruled が **本当に違う** (区別が空振りしていないこと) ----
#   断面 3 枚 (1 → 2 → 1) なら、なめらかな方は膨らむので体積が大きくなる。
#   曲面種も違う: なめらか = bspline / 線織 = cone。
O=$(run l2 '
var C0 = circle(1);
var C2 = translate(circle(2),[0,0,2]);
var C4 = translate(C0,[0,0,4]);
print("VAL", volume(loft(C0,C2,C4)));
print("VAL", volume(loft_ruled(C0,C2,C4)));
print("T", surface_type(face(loft(C0,C2,C4),0)));
print("T", surface_type(face(loft_ruled(C0,C2,C4),0)));
')
_SPLIT_=$(echo "$O" | sed -n 's/^VAL //p')
set -f
set -- $_SPLIT_
set +f
chk "なめらか (3 枚)" "$1" 36.023595135065484
chk "線織 (3 枚)"     "$2" 29.321531433504734
BIG=$(awk -v a="$1" -v b="$2" 'BEGIN{print (a>b*1.1)?1:0}')
[ "$BIG" = "1" ] && n=$((n+1)) || { echo "FAIL: loft と loft_ruled が同じ形になっている ($1 / $2)"; exit 1; }
_SPLIT_=$(echo "$O" | sed -n 's/^T //p')
set -f
set -- $_SPLIT_
set +f
[ "$1" = "bspline" ] || { echo "FAIL: なめらかな loft の側面が $1 (bspline のはず)"; exit 1; }
[ "$2" = "cone" ]    || { echo "FAIL: 線織 loft の側面が $2 (cone のはず)"; exit 1; }
n=$((n+2))

# ---- ③ ★ 断面を **傾けて置ける** (#3518 の 1 のおかげ・メッシュ系では書けない形) ----
O=$(run l3 '
var R0 = rect(2,3);
print("VAL", volume(loft_ruled(R0, translate(rotate(R0,"x",20),[0,0,4]))));
print("VAL", valid(loft_ruled(R0, translate(rotate(R0,"x",20),[0,0,4]))));
')
_SPLIT_=$(echo "$O" | sed -n 's/^VAL //p')
set -f
set -- $_SPLIT_
set +f
chk "傾けた断面の loft"     "$1" 26.354492739361916
chk "傾けても立体になる"     "$2" 1

# ---- ④ 断らせる ----
echo "$(run e1 'print("VAL", volume(loft(circle(1))));')" | grep -q "at least two cross sections" || {
	echo "FAIL: 断面 1 枚の loft が通った"; exit 1; }
n=$((n+1))
echo "$(run e2 'print("VAL", volume(loft(circle(1), box(1,1,1))));')" | grep -q "no module can execute op 'loft'" || {
	echo "FAIL: 立体を断面として渡せてしまった"; exit 1; }
n=$((n+1))
#   ⚠ 複数の面を持つ断面はどの輪をどの輪につなぐか決まらない ⇒ 明示エラー
#   ★ 確実に複数面になる断面を使う: 縦置きトーラスを柱で切ると 6 面になる (#3518 で実測)。
#     ⚠ ここで 1 面の断面を使うと検査が空振りする (最初そうなっていた)。
E3=$(run e3 '
var T = rotate(torus(2,0.5),"x",90);
var CUT = face(T,0) &&& translate(extrude(translate(rect(0.4,0.4),[-0.2,-0.2]),20),[0,0,-9]);
print("NF", nfaces(CUT));
print("VAL", volume(loft(rect(2,3), CUT)));')
NF=$(echo "$E3" | sed -n 's/^NF //p')
[ "$NF" -gt 1 ] || { echo "FAIL: 検査が空振り — 断面が $NF 面しかない"; exit 1; }
echo "$E3" | grep -q "loft needs one closed outline per section" || {
	echo "FAIL: 複数面の断面が黙って通った ($NF 面)"; echo "$E3"; exit 1; }
n=$((n+2))

# ---- ⑤ ★ 断面は 2 枚に限らない (5 枚) ----
O=$(run l5 '
var C0 = circle(1);
print("VAL", volume(loft_ruled(C0, translate(circle(2),[0,0,1]), translate(C0,[0,0,2]),
                               translate(circle(2),[0,0,3]), translate(C0,[0,0,4]))));
')
_SPLIT_=$(echo "$O" | sed -n 's/^VAL //p')
set -f
set -- $_SPLIT_
set +f
chk "5 枚 (円錐台 4 つ)" "$1" 29.321531433504734

# ---- ⑥ ★★ 断面は **平面**でなければならない ----
#   ⚠ oc-face3d は #3518 から *曲面の上* にも居られるので、円柱側面を切り取った patch を
#     そのまま断面に渡せてしまう。ところが ThruSections は isSolid=true で **断面そのものを
#     蓋にする** ので、蓋が平面でないと内外が決まらず符号つき体積が打ち消して 0 になる。
#   ⚠⚠ 2026-09-13 まではここで **volume = -8.88e-16 が黙って返っていた** (valid は 0 だった)。
#     #3518 の 5 (掃引の検査) と同じ形の穴が loft にも在った。
P='face_at(cylinder(1,4),[5,0,0]) &&& translate(box(9,9,1),[-4.5,0,-0.5])'
E5=$(run l6 "
var P = $P;
print(\"NF\", nfaces(P));
print(\"T\", surface_type(P));
print(\"VAL\", volume(loft_ruled(P, translate(P,[0,0,6]))));")
[ "$(echo "$E5" | sed -n 's/^T //p')" = "cylinder" ] || {
	echo "FAIL: 検査が空振り — 断面が曲面になっていない"; echo "$E5"; exit 1; }
echo "$E5" | grep -q "is not flat" || { echo "FAIL: 曲面の断面が黙って通った"; echo "$E5"; exit 1; }
n=$((n+2))
#   ⚠ 同じ位置の 2 枚も立体にならない (OCCT 自身が断る経路)
echo "$(run l7 'print("VAL", volume(loft(rect(2,3), rect(2,3))));')" | grep -q "occt/loft:" || {
	echo "FAIL: 同じ位置の 2 枚が黙って通った"; exit 1; }
n=$((n+1))

# ---- ⑧ ★★ 断面を **cache の配列 1 個**で渡せる (#3511・sig の "[]" 宣言) ----
#   ★ 断面は式で生成するもの (翼型を N 枚・船体の肋骨を M 枚) なので、手で並べるのでは
#     道具として弱い。⇒ sig の可変部に "[]" を書いた op だけ、評価時に n 項へ展開する。
#   ★★ **展開後は普通の n 項呼び出しと完全に同じ** — 値もキャッシュキーも一致することを見る
#     (別の書き方が別のキャッシュ実体を作らないこと)。
O=$(run a1 '
var A = [circle(1), translate(circle(2),[0,0,2]), translate(circle(1),[0,0,4])];
print("VAL", volume(loft(A)));
print("VAL", volume(loft(circle(1), translate(circle(2),[0,0,2]), translate(circle(1),[0,0,4]))));
print("VAL", volume(loft_ruled(A)));
print("VAL", volume(loft_ruled(circle(1), translate(circle(2),[0,0,2]), translate(circle(1),[0,0,4]))));
')
_SPLIT_=$(echo "$O" | sed -n 's/^VAL //p')
set -f
set -- $_SPLIT_
set +f
chk "配列で loft"        "$1" 36.023595135065484
[ "$1" = "$2" ] || { echo "FAIL: 配列形と並べた形で値が違う ($1 / $2)"; exit 1; }
chk "配列で loft_ruled"  "$3" 29.321531433504734
[ "$3" = "$4" ] || { echo "FAIL: ruled の配列形と並べた形で値が違う ($3 / $4)"; exit 1; }
n=$((n+2))

#   ★ キャッシュキーが一致すること = **配列形で作った実体を並べた形が HIT で拾う**
rm -rf "$D-a2"
SRAVA_CACHE_DIR="$D-a2" SRAVA_SOURCE="module(\"$SO\",{priority:99});
var A = [circle(1), translate(circle(2),[0,0,2]), translate(circle(1),[0,0,4])];
print(\"V\", volume(loft(A)));" "$SRAVA" >/dev/null 2>&1
H=$(SRAVA_CACHE_DIR="$D-a2" SRAVA_SOURCE="module(\"$SO\",{priority:99});
print(\"V\", volume(loft(circle(1), translate(circle(2),[0,0,2]), translate(circle(1),[0,0,4]))));" "$SRAVA" 2>&1 |
	sed -n 's/.*cache: \([0-9]*\) hit(s), \([0-9]*\) miss(es).*/\1 \2/p')
set -- $H
[ "$2" = "0" ] && [ "$1" -gt 0 ] || {
	echo "FAIL: 配列形と並べた形でキャッシュキーが違う (hit=$1 miss=$2)"; exit 1; }
n=$((n+1))

#   ⚠ 配列を受けると **宣言していない** op は従来どおり断る (この仕組みが漏れていないこと)
echo "$(run a3 'print("VAL", volume([box(1,1,1)]));')" | grep -q "配列が来ました" || {
	echo "FAIL: 配列宣言の無い op が配列を受けてしまった"; exit 1; }
n=$((n+1))

echo "OCCT-LOFT-OK $SO ($n checks)"
