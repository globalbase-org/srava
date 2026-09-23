#!/bin/sh
# ★★ #3527: vert(m, i) — **i 番目の頂点の座標** の検査。$1 = srava 実行体 / $2 = モジュール (.so)。
#
# ---- 何を見ているか ----
# ★ 「数える」(nverts) と「取り出す」(vert) が **同じ列を同じ順**で見ていること。
#   ⇒ ① 本数が合う ② i を全部回すと *値の集合* が既知の頂点集合と一致する
#      ③ ⚠⚠ **同じ式が同じ i で同じ頂点を返し続ける** (索引が実装依存なので、ここが本体)
#
# ⚠⚠ #3527 が言う「索引には 2 種類ある」の **実装依存** 側。列挙順をなぞるだけなので
#   版・ビルド・入力順で変わりうる。⇒ *キャッシュを跨いで* 同じかを見る (#3518 の face と同じ形)。
#   ★ 値としては常に「正しい頂点のどれか」なので、**壊れても値の検査では捕まらない**。
#     ⇒ 順序そのものを検査に書くしかない。
#
# ⚠ 「座標で返す」のと「構成要素の番号で返す」は別の約束。ここは **座標** だけを見る
#   (番号側は face_verts の担当・未配線)。
. "$(dirname "$0")/srava_hangwatch.sh"

SRAVA="${1:?srava not given}"
SO="${2:-cgal.so}"
D="${SRAVA_CACHE_DIR:-/tmp/srava-vert}"
fails=0
fail() { echo "FAIL: $*"; fails=$((fails+1)); }

run() {   # $1 = source → stdout
	SRAVA_CACHE_DIR="$2" SRAVA_SOURCE="$1" "$SRAVA" 2>&1
}

# ---- ① 本数と、全頂点の集合 ----
# ★ box(2,3,4) の頂点は 8 つで、座標は {0,2}x{0,3}x{0,4} の直積。
#   ⇒ **和** で見る (順序に依らない): Σx = 4*2 = 8 ・ Σy = 4*3 = 12 ・ Σz = 4*4 = 16。
#   ⚠ 和だけだと「同じ頂点を 8 回返す」を見逃すので、**最小・最大**も併せて見る。
SRC='module("'"$SO"'",{priority:99});
var b = box(2,3,4);
print("N", nverts(b));
var i = 0;
for ( i = 0 ; i < nverts(b) ; i = i + 1 ) {
  var v = vert(b,i);
  print("P", v[0], v[1], v[2]);
}
'
OUT=$(run "$SRC" "$D-a")
N=$(printf '%s\n' "$OUT" | sed -n 's/^N //p')
[ "$N" = "8" ] || fail "nverts(box) が 8 でない: '$N'"
PTS=$(printf '%s\n' "$OUT" | sed -n 's/^P //p')
NP=$(printf '%s\n' "$PTS" | grep -c .)
[ "$NP" = "8" ] || fail "vert で取り出せた点が 8 個でない: $NP 個"
SUM=$(printf '%s\n' "$PTS" | awk '{x+=$1;y+=$2;z+=$3} END{printf "%g %g %g", x,y,z}')
[ "$SUM" = "8 12 16" ] || fail "頂点の和が (8,12,16) でない: ($SUM)"
UNIQ=$(printf '%s\n' "$PTS" | sort -u | grep -c .)
[ "$UNIQ" = "8" ] || fail "頂点が 8 通りでない (同じ点を複数回返している?): $UNIQ 通り"
echo "      ① 本数 $N ・ 取り出し $NP ・ 相異なる $UNIQ ・ 和 ($SUM)"

# ---- ② ⚠⚠ 索引の再現性 — **キャッシュを跨いで同じ i が同じ頂点を指すか** ----
# ★ 1 回目とは **別のキャッシュ dir** で 2 回目を回す = 計算し直させる。
#   ⇒ それでも並びが同じなら「実装依存だが決定的」と言える (#3518 の face と同じ形)。
# ⚠ 同じ cache dir で 2 回流すのは **検定にならない** (2 回目は HIT で読み直すだけ)。
OUT2=$(run "$SRC" "$D-b")
PTS2=$(printf '%s\n' "$OUT2" | sed -n 's/^P //p')
if [ "$PTS" = "$PTS2" ]; then
	echo "      ② 索引の再現性: **別キャッシュで計算し直しても並びが同一**"
else
	fail "同じ式・同じ i が別の頂点を指した (索引が決定的でない)"
	printf '  1 回目: %s\n  2 回目: %s\n' "$(printf '%s' "$PTS" | tr '\n' '/')" "$(printf '%s' "$PTS2" | tr '\n' '/')"
fi

# ---- ③ 範囲外は **エラーにする** (黙って 0 を返さない) ----
SRC_OOR='module("'"$SO"'",{priority:99});
var b = box(2,3,4);
print("X", vert(b, 99)[0]);
'
OUT3=$(run "$SRC_OOR" "$D-c")
if printf '%s\n' "$OUT3" | grep -q 'out of range'; then
	echo "      ③ 範囲外: エラーになる"
else
	fail "範囲外の索引がエラーにならなかった"
	printf '%s\n' "$OUT3" | grep -iE 'error|X ' | head -3 | sed 's/^/  /'
fi

# ---- ④ 2D — 枠の中の (x,y) を返す (返り 2 なので長さ 2) ----
# ★ rect(2,3) の 4 点は {0,2}x{0,3} ⇒ Σx = 4 ・ Σy = 6。
SRC2D='module("'"$SO"'",{priority:99});
var r = rect(2,3);
print("N2", nverts(r));
var i = 0;
for ( i = 0 ; i < nverts(r) ; i = i + 1 ) {
  var v = vert(r,i);
  print("Q", v[0], v[1]);
}
'
OUT4=$(run "$SRC2D" "$D-d")
N2=$(printf '%s\n' "$OUT4" | sed -n 's/^N2 //p')
Q=$(printf '%s\n' "$OUT4" | sed -n 's/^Q //p')
SUM2=$(printf '%s\n' "$Q" | awk '{x+=$1;y+=$2} END{printf "%g %g", x,y}')
if [ "$N2" = "4" ] && [ "$SUM2" = "4 6" ]; then
	echo "      ④ 2D: nverts=$N2 ・ 和 ($SUM2)"
else
	fail "2D の vert が合わない: nverts='$N2' 和='($SUM2)'"
	printf '%s\n' "$OUT4" | grep -iE 'error' | head -3 | sed 's/^/  /'
fi

# ---- ④b ★★ face3d (置き場所を持つ 2D) は **world の 3 成分** ----------------------
# ★★ #3527 段 5 (ひさ判断 2026-09-17): 揃える先は #3533 が bbox / centroid で決めた world。
#   ⚠⚠ 揃えるまでは **vert だけ枠の中の 2 成分**で、置き場所が黙って落ちていた:
#       bbox(section(b,[0,0,2],…)) と bbox(section(b,[0,0,7],…)) は違うのに
#       vert(…,0) はどちらも [0,0] ⇒ hull(verts(sec)) が常に z=0 に出ていた。
#   ⇒ ここで見るのは「**z が保たれること**」と「同じ形を別の高さで切ると答えが違うこと」。
#     後者が無いと「3 成分になった」だけで *中身が枠内のまま* でも通ってしまう。
SRCF3='module("'"$SO"'",{priority:99});
var b = box(4,4,10);
var s2 = section(b, [0,0,2], [0,0,1], 0);
var s7 = section(b, [0,0,7], [0,0,1], 0);
print("NF", nverts(s7));
var i = 0;
for ( i = 0 ; i < nverts(s7) ; i = i + 1 ) { var v = vert(s7,i); print("F", v[0], v[1], v[2]); }
for ( i = 0 ; i < nverts(s2) ; i = i + 1 ) { var v = vert(s2,i); print("G", v[0], v[1], v[2]); }
'
OUTF=$(run "$SRCF3" "$D-f3")
NF=$(printf '%s\n' "$OUTF" | sed -n 's/^NF //p')
F=$(printf '%s\n' "$OUTF" | sed -n 's/^F //p')
G=$(printf '%s\n' "$OUTF" | sed -n 's/^G //p')
# 4 点 {0,4}x{0,4} が z=7 に載る ⇒ Σx=8 ・ Σy=8 ・ Σz=28
SUMF=$(printf '%s\n' "$F" | awk '{x+=$1;y+=$2;z+=$3} END{printf "%g %g %g", x,y,z}')
if [ "$NF" = "4" ] && [ "$SUMF" = "8 8 28" ]; then
	echo "      ④b face3d: world の 3 成分 ・ 和 ($SUMF) — z が保たれている"
else
	fail "face3d の vert が world の 3 成分でない: nverts='$NF' 和='($SUMF)' (期待 4 / 8 8 28)"
	printf '%s\n' "$OUTF" | grep -iE 'error' | head -3 | sed 's/^/  /'
fi
# ★ 陽性対照にあたる: 高さだけ違う 2 枚が **別の答え**になること (枠内のままだと同一になる)
if [ "$F" = "$G" ]; then
	fail "z=2 と z=7 の断面が同じ vert を返した (置き場所が落ちている = 枠の中の座標のまま)"
else
	echo "      ④b 別の高さの断面は **別の答え** (置き場所が落ちていない)"
fi

# ---- ⑤ verts(m) — まとめて点群で返す。往復で見る ----
# ★ 値そのものではなく **往復** で見る: box の頂点から凸包を作れば元の体積に戻る。
#   ⇒ 「点が全部在る」「座標が正しい」を 1 つの数で押さえられる。
SRC_VS='module("'"$SO"'",{priority:99});
module("points.so",{});
var b = box(2,3,4);
var c = verts(b);
print("NC", nverts(c));
print("HV", volume(hull(c)));
'
OUT5=$(run "$SRC_VS" "$D-e")
NC=$(printf '%s\n' "$OUT5" | sed -n 's/^NC //p')
HV=$(printf '%s\n' "$OUT5" | sed -n 's/^HV //p')
[ "$NC" = "8" ] || fail "verts(box) の点数が 8 でない: '$NC'"
case "$HV" in 24|23.9*|24.0*) echo "      ⑤ verts: 点数 $NC ・ hull の体積 $HV (= 2*3*4)" ;;
  *) fail "verts → hull の体積が 24 でない: '$HV'" ;; esac

# ---- ⑥ ★★ verts(m) の i 番目 == vert(m,i) ----
# ⚠⚠ **ここが一番壊れやすい**。op_vert と op_verts は *別々に* 列を歩くので、
#   片方だけ順序を変えると **黙ってずれる**。値としてはどちらも「正しい頂点」なので、
#   個別の検査では捕まらない。⇒ **等式そのものを見る**。
SRC_EQ='module("'"$SO"'",{priority:99});
module("points.so",{});
var b = box(2,3,4);
var c = verts(b);
var i = 0;
for ( i = 0 ; i < nverts(b) ; i = i + 1 ) {
  var a = vert(b,i);
  var d = vert(c,i);
  print("EQ", a[0]-d[0], a[1]-d[1], a[2]-d[2]);
}
'
OUT6=$(run "$SRC_EQ" "$D-f")
EQ=$(printf '%s\n' "$OUT6" | sed -n 's/^EQ //p')
NEQ=$(printf '%s\n' "$EQ" | grep -c .)
BAD=$(printf '%s\n' "$EQ" | grep -vc '^0 0 0$' || true)
if [ "$NEQ" = "8" ] && [ "$BAD" = "0" ]; then
	echo "      ⑥ verts(m)[i] == vert(m,i) が 全 $NEQ 点で成立"
else
	fail "verts と vert の並びがずれている ($NEQ 点中 $BAD 点で差)"
	printf '%s\n' "$EQ" | head -4 | sed 's/^/  /'
fi

if [ "$fails" != "0" ]; then
	echo "FAIL: $fails 件"
	exit 1
fi
echo "VERT-OK vert(m,i) / verts(m) は nverts と同じ列を同じ順で見ている"
