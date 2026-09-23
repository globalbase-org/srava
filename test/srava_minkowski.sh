#!/bin/sh
# minkowski (Minkowski 和・#3511 / #3440) の **閉形式**回帰。
# $1 = srava 実行体。$2 = モジュール (.so 名)。$3 = 許容相対誤差。
# env: SRAVA_AGENT, SRAVA_CACHE_DIR。
#
# ★ kernel_agree には入れられない — あちらは基準が常に cgal で、**cgal.so は minkowski を
#   持たない** (CGAL の minkowski_sum_3 は Nef_polyhedron_3 の上の関数なので、持ち主は nef)。
#   ⇒ カーネル合議ではなく **真値**と突き合わせる。
#
# 見ているもの:
#   ① 箱 ⊕ 箱 は箱          (a,b,c) ⊕ (d,e,f) = (a+d)(b+e)(c+f) — **厳密**
#   ② 向きを変えても同じ      2x3x4 ⊕ 1x1x1 = 3*4*5 = 60
#   ③ 可換                    A ⊕ B = B ⊕ A
#   ④ ★ Steiner の上界        箱 ⊕ 内接測地球 は、箱 ⊕ 真球 (Steiner の公式) を **越えない**
#      V(d) = 8 + 24d + 6*pi*d^2 + (4/3)*pi*d^3      (box(2,2,2) の場合)
#      ⚠ 内接多面体は真球より小さいので **必ず下回る**。等号にはならない (近似の向きが決まって
#        いることを見るのが目的で、精度を見ているのではない)。細分を上げると真値へ寄る。
#   ⑤ 空との和は空            A ⊕ ∅ = ∅
SRAVA="${1:?srava binary not given}"
SO="${2:?module .so not given}"
TOL="${3:-1e-12}"
D="${SRAVA_CACHE_DIR:?SRAVA_CACHE_DIR not set}"
# ★★ #3522: ハングの番犬 (共通・常時 ON)。詳細は test/srava_hangwatch.sh。
. "$(dirname "$0")/srava_hangwatch.sh"
M="module(\"$SO\",{priority:99});"
fails=0

near() {   # near <got> <want> <name>
	ok=$(awk -v g="$1" -v w="$2" -v t="$TOL" 'BEGIN{
		d = g - w; if (d < 0) d = -d;
		r = (w < 0 ? -w : w); if (r < 1) r = 1;
		print (d / r <= t) ? 1 : 0 }')
	if [ "$ok" = "1" ]; then echo "ok    $3 = $1"
	else echo "FAIL: $3 = $1 (期待 $2・許容 $TOL)"; fails=$((fails+1)); fi
}

rm -rf "$D-m"
OUT=$(SRAVA_CACHE_DIR="$D-m" SRAVA_SOURCE="$M
  print(\"A\", volume(minkowski(box(2,2,2), box(1,1,1))));
  print(\"B\", volume(minkowski(box(2,3,4), box(1,1,1))));
  print(\"C\", volume(minkowski(box(1,1,1), box(2,3,4))));
  print(\"D\", volume(minkowski(box(2,2,2), icosphere(0.1,3))));
  print(\"E\", volume(minkowski(box(2,2,2), empty3d())));" "$SRAVA" 2>&1)
A=$(echo "$OUT" | sed -n 's/^A //p'); B=$(echo "$OUT" | sed -n 's/^B //p')
C=$(echo "$OUT" | sed -n 's/^C //p'); Dv=$(echo "$OUT" | sed -n 's/^D //p')
E=$(echo "$OUT" | sed -n 's/^E //p')
if [ -z "$A" ] || [ -z "$B" ] || [ -z "$C" ] || [ -z "$Dv" ] || [ -z "$E" ]; then
	echo "FAIL: 値が出ない A=$A B=$B C=$C D=$Dv E=$E"; echo "$OUT"; exit 1
fi
near "$A" 27 "① 箱 ⊕ 箱 (3x3x3)"
near "$B" 60 "② 箱 ⊕ 箱 (3x4x5)"
near "$C" "$B" "③ 可換 (A⊕B = B⊕A)"
near "$E" 0  "⑤ 空との和は空"
# ④ Steiner の上界。d=0.1 の真値 10.5926843494 を **越えない**が、1% 以内には居ること。
ok=$(awk -v v="$Dv" 'BEGIN{ e=10.5926843494; print (v <= e && v >= e*0.99) ? 1 : 0 }')
if [ "$ok" = "1" ]; then echo "ok    ④ Steiner の上界 (内接球なので下回る) = $Dv <= 10.5926843494"
else echo "FAIL: ④ 箱 ⊕ 内接球 = $Dv (Steiner の真値 10.5926843494 の 0.99〜1.00 倍であるべき)"; fails=$((fails+1)); fi

if [ "$fails" = "0" ]; then echo "MINKOWSKI-OK ($SO)"; else echo "MINKOWSKI-FAIL ($fails fail)"; fi
exit "$fails"
