#!/bin/sh
# ★★ #3527: shell(m, i) — **i 番目の殻** (面の連結成分) の検査。$1 = srava / $2 = モジュール。
#
# ---- 何を見ているか ----
# ★ 殻は **part (塊) とは別物**。中空の箱は 塊 1 個 ・ 殻 2 枚 (外側の箱 + 空洞の境界)。
#   part は入れ子 (どの空洞がどの塊のものか) が要るので cgal では空洞を断るが、
#   shell は面の連結成分そのものなので **入れ子を知らなくても取り出せる**。
#
# ★★★ 向きは **そのまま返す** (案 A・ひさ裁定 2026-09-16)。⇒ 空洞の殻は volume が **負**。
#   ⇒ **符号がそのまま「外殻か空洞か」の判別子**になる。
#   ⇒ そして **Σ 符号つき体積 == 全体の体積** が成り立つ (中空の箱 = 外殻 − 空洞)。
#   ⚠ 向きを揃える案 (B) だと符号が消え、この 2 つとも失われる。
#
# ⚠⚠ **どちらが 0 番かは検査しない**。索引は連結成分の走査順 = 実装依存で、
#   殻の番号は *指し示す* ためではなく **列挙のため** のものだから (#3527 の整理)。
#   ⇒ 見るのは「**集合として**正しいか」と「**和**が合うか」。順序そのものは ④ で別に見る。
. "$(dirname "$0")/srava_hangwatch.sh"

SRAVA="${1:?srava not given}"
SO="${2:-cgal.so}"
D="${SRAVA_CACHE_DIR:-/tmp/srava-shell}"
fails=0
fail() { echo "FAIL: $*"; fails=$((fails+1)); }
run() { SRAVA_CACHE_DIR="$2" SRAVA_SOURCE="$1" "$SRAVA" 2>&1; }

# 中空の箱: 4^3 の中に 2^3 の空洞 ⇒ 体積 56 ・ 殻 2 枚 (+64 と -8)
HOLLOW='var hollow = difference(box(4,4,4), translate(box(2,2,2),[1,1,1]));'

# ---- ① 数えるほう (前提の確認) ----
SRC1='module("'"$SO"'",{priority:99});
'"$HOLLOW"'
print("NS", nshells(hollow));
print("NP", nparts(hollow));
print("V", volume(hollow));
'
O1=$(run "$SRC1" "$D-a")
NS=$(printf '%s\n' "$O1" | sed -n 's/^NS //p'); NP=$(printf '%s\n' "$O1" | sed -n 's/^NP //p')
V=$(printf '%s\n' "$O1" | sed -n 's/^V //p')
[ "$NS" = "2" ] || fail "中空の箱の nshells が 2 でない: '$NS'"
[ "$NP" = "1" ] || fail "中空の箱の nparts が 1 でない: '$NP'"
[ "$V" = "56" ] || fail "中空の箱の volume が 56 でない: '$V'"
echo "      ① 中空の箱: nshells=$NS ・ nparts=$NP ・ volume=$V"

# ---- ② 集合として {+64, -8} か (★ どちらが 0 番かは問わない) ----
SRC2='module("'"$SO"'",{priority:99});
'"$HOLLOW"'
var i = 0;
for ( i = 0 ; i < nshells(hollow) ; i = i + 1 ) { print("SV", volume(shell(hollow,i))); }
'
O2=$(run "$SRC2" "$D-b")
SV=$(printf '%s\n' "$O2" | sed -n 's/^SV //p' | sort -n | tr '\n' ' ')
case "$SV" in
  "-8 64 ") echo "      ② 殻の体積は集合として {+64, -8} (符号が 外殻/空洞 の判別子)" ;;
  *) fail "殻の体積が {+64,-8} でない: [$SV]" ;;
esac

# ---- ③ ★★ Σ 符号つき体積 == 全体の体積 ----
# ★ これが案 A を選んだ理由そのもの。案 B (向きを揃える) だと 64+8=72 になって崩れる。
SUM=$(printf '%s\n' "$O2" | sed -n 's/^SV //p' | awk '{s+=$1} END{printf "%g", s}')
if [ "$SUM" = "56" ]; then
	echo "      ③ ★ Σ 符号つき体積 = $SUM == volume(全体) $V"
else
	fail "Σ 符号つき体積が全体と一致しない: $SUM ≠ $V"
fi

# ---- ④ 索引の再現性 (別キャッシュで計算し直しても並びが同じか) ----
O2b=$(run "$SRC2" "$D-c")
if [ "$(printf '%s\n' "$O2" | sed -n 's/^SV //p')" = "$(printf '%s\n' "$O2b" | sed -n 's/^SV //p')" ]; then
	echo "      ④ 索引の再現性: 別キャッシュで計算し直しても並びが同一"
else
	fail "同じ式・同じ i が別の殻を指した (索引が決定的でない)"
fi

# ---- ⑤ 範囲外 / 2D は **明示エラー** (黙って 0 を返さない) ----
SRC3='module("'"$SO"'",{priority:99});
'"$HOLLOW"'
print("X", volume(shell(hollow, 99)));
'
printf '%s\n' "$(run "$SRC3" "$D-d")" | grep -q 'out of range' \
  && echo "      ⑤ 範囲外: エラーになる" || fail "範囲外の索引がエラーにならなかった"
SRC4='module("'"$SO"'",{priority:99});
print("X", area(shell(rect(2,3), 0)));
'
# ★ 2D は **sig (ルータ) が先に弾く** — op の中の 2D 分岐には通常経路では到達しない。
#   ⇒ 期待する文言は「どのモジュールも実行できない」側。⚠ op 側のメッセージを期待すると
#     *通らない道* を検査していることになる (実際 1 度そう書いて落ちた)。
printf '%s\n' "$(run "$SRC4" "$D-e")" | grep -q "no module can execute op 'shell'" \
  && echo "      ⑤ 2D: sig が弾く (accepted: (cg-mesh3d)->cg-mesh3d)" || fail "2D に shell を当ててもエラーにならなかった"

# ---- ⑥ ★★ part との違い — **和の式が違う** ----
# ⚠⚠ 2026-09-17 に振る舞いが変わった。以前は cgal の part は空洞を **断って**いたが、
#   それは実装の都合 (面の連結成分しか見ていなかった) で、CGAL 本体は
#   volume_connected_components で塊を直接くれる。⇒ 解けるようにした。
# ★ いまの正しい姿:
#     part  … 塊。中空の箱は **1 個**で、その塊は外殻と空洞の **両方**を境界に持つ
#             ⇒ part(m,0) は **m と同じもの** ・ **Σ volume(part) == volume(m)** (符号なし)
#     shell … 面の連結成分。2 枚 ・ **Σ 符号つき体積 == volume(m)** (符号つき)
#   ⇒ **この 2 つが別の式であること**が、part と shell が別物である理由そのもの。
SRC5='module("'"$SO"'",{priority:99});
'"$HOLLOW"'
var i = 0;
for ( i = 0 ; i < nparts(hollow) ; i = i + 1 ) { print("PV", volume(part(hollow,i))); }
'
O5=$(run "$SRC5" "$D-f")
PV=$(printf '%s\n' "$O5" | sed -n 's/^PV //p')
NPV=$(printf '%s\n' "$PV" | grep -c .)
PSUM=$(printf '%s\n' "$PV" | awk '{s+=$1} END{printf "%g", s}')
if [ "$NPV" = "1" ] && [ "$PSUM" = "56" ]; then
	echo "      ⑥ part: 塊 $NPV 個 ・ Σ volume(part) = $PSUM == volume(全体) $V (**符号なし**で一致)"
else
	fail "part の和が全体と一致しない: 塊 $NPV 個 ・ Σ=$PSUM (期待 1 個 ・ 56)"
	printf '%s\n' "$O5" | grep -i error | head -2 | sed 's/^/  /'
fi
# ★ 塊 1 個の形では part(m,0) は **m と同じもの** — 殻 1 枚 (=64) を返してはいけない
P0=$(printf '%s\n' "$PV" | head -1)
[ "$P0" = "56" ] || fail "part(m,0) が元の値と同じでない: $P0 (殻だけ返していないか)"

# ---- ⑦ ★ shell_at — **位置で指す**側 ----
# ★★ なぜ索引と 2 通り要るか: 番号は「列挙のため」で **指す先が無い**。モデルの書き方を
#   変えると殻の集合そのものが変わるので、番号は当然別の殻を指す。⇒ 書き換えても同じ殻を
#   指し続けたいなら **位置で指すしかない** (occt の face / face_at が同じ理由で 2 通りある)。
# ⚠ 同距離の殻が 2 つ以上あるときは **明示エラー**。黙って片方を選ぶと
#   「同じ式に 2 通りの値」になる (#3516 / #3518-1 で潰してきた穴)。
#   ★ 中空の箱の z=0.5 平面上の点がまさにそれ — 外殻の底 (z=0) と空洞の底 (z=1) から等距離。
SRC6='module("'"$SO"'",{priority:99});
'"$HOLLOW"'
print("OUT", volume(shell_at(hollow,[0,0,0])));
print("CAV", volume(shell_at(hollow,[2,2,2])));
'
O6=$(run "$SRC6" "$D-g")
OUT=$(printf '%s\n' "$O6" | sed -n 's/^OUT //p'); CAV=$(printf '%s\n' "$O6" | sed -n 's/^CAV //p')
if [ "$OUT" = "64" ] && [ "$CAV" = "-8" ]; then
	echo "      ⑦ shell_at: 角の点 → 外殻 (+$OUT) ・ 空洞の中心 → 空洞 ($CAV)"
else
	fail "shell_at が位置で選べていない: 角=$OUT 空洞=$CAV"
fi

SRC7='module("'"$SO"'",{priority:99});
'"$HOLLOW"'
print("X", volume(shell_at(hollow,[2,2,0.5])));
'
if printf '%s\n' "$(run "$SRC7" "$D-h")" | grep -q 'equally near'; then
	echo "      ⑦ 同距離の点: **断る** (黙って片方を選ばない)"
else
	fail "同距離の点で shell_at が断らなかった"
fi

if [ "$fails" != "0" ]; then
	echo "FAIL: $fails 件"
	exit 1
fi
echo "SHELL-OK shell(m,i) は向きを保ち和が全体に一致する ・ shell_at は位置で指し同距離は断る"
