#!/bin/sh
# ★★ #3518 の 5: **掃引 (extrude / revolve) の検査** — 符号つき体積が 0 なら明示エラー。
#
# $1 = srava 実行体 / $2 = モジュール名 (.so) / $3 = 許容誤差
#
# ---- ★★ なぜ「潰れている」ではないのか ----
# @BRepGProp::VolumeProperties@ が返すのは **境界からのフラックス**。
# @BRepPrimAPI_MakePrism@ は「底面 + 平行移動した天面 + 境界稜を掃いた側面」で境界を組むので、
# 掃引方向が面の中を向いていると底と天の一部が領域の内部に入り、フラックスが厳密に
# 打ち消し合って 0 になる。★ 掃引された領域そのものは体積を持つ (ミンコフスキー和で
# (6+π)x2 = 18.283185 と実測済み) — 壊れているのは **境界表現のほう**。
# ⇒ 「潰れた」と言わずに「掃引の単調性が満たされていない」と言う。
#
# ⚠ これは曲面と無関係に **元から在った穴**。平面の面でも面内方向へ押し出せば 0 になる。
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

# ---- ① 正しい掃引は **通る** (検査が正しいものを潰していないこと) ----
#   ★ 傾けた面の押し出しは意味のある立体になる = 射影面積 x 押し出し長。
O=$(run s1 '
print("VAL", volume(extrude(rect(2,3),1)));                      // 6
print("VAL", volume(extrude(rect(2,3),2)));                      // 12
print("VAL", volume(extrude(rotate(rect(2,3),"x",45),1)));       // 6*cos45
print("VAL", volume(revolve(rect(2,3),360)));                    // パップス 6*2pi*1
print("VAL", volume(revolve(translate(rect(2,3),[2,0]),360)));   // 6*2pi*3
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
chk "extrude 平面 h=1"        "$1" 6
chk "extrude 平面 h=2"        "$2" 12
chk "extrude 45 度 (射影面積)" "$3" 4.242640687119285
chk "revolve 360 (パップス)"   "$4" 37.69911184307751
chk "revolve 離した断面"       "$5" 113.09733552923254

# ---- ② ★★ 掃引が単調でないものは **明示エラー** (以前は体積 0 が黙って通っていた) ----
echo "$(run s2 'print("VAL", volume(extrude(rotate(rect(2,3),"x",90),1)));')" \
	| grep -q "sweep direction lies inside" || {
	echo "FAIL: 面内方向への押し出しが黙って通った (体積 0 の立体になる)"; exit 1; }
n=$((n+1))
echo "$(run s3 'print("VAL", volume(revolve(rotate(rect(2,3),"x",90),360)));')" \
	| grep -q "straddles the axis" || {
	echo "FAIL: 軸をまたぐ回転が黙って通った"; exit 1; }
n=$((n+1))

# ---- ③ OCCT 自身が断る場合も **明示エラー**として出る (黙って 0 にしない) ----
#   ⚠ 断面が軸をまたぐ回転は OCCT が先に StdFail_NotDone を投げる。どちらの経路でも
#     「エラーになる」ことだけを要求する (どちらが先かは OCCT の版で変わりうる)。
for T in 'revolve(translate(rect(2,3),[-1,0]),360)' 'revolve(translate(rect(2,3),[-1,0]),90)'; do
	echo "$(run s4 "print(\"VAL\", volume($T));")" | grep -q "occt/revolve:" || {
		echo "FAIL: 軸をまたぐ断面の回転 ($T) が黙って通った"; exit 1; }
	n=$((n+1))
done

echo "OCCT-SWEEP-OK $SO ($n checks)"
