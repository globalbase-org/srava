#!/bin/sh
# ★★ #3527 段 6: **cast が枠 (置き場所) を落とさない** ことの検査。$1 = srava。
#
# ---- 何を見ているか ----
# #3533 の規約② は「表現力の高→低の落下は **cast のみ**」。⚠ ところが cast 自身が
# *黙って* 降格していた — cg-face3d を mf / gu へ渡すと **枠が捨てられ z=0 へ戻っていた**
# (2026-09-17 に段 6 の cast 監査で発見)。
#
#     bbox(sec)                       [[0,0,2],[4,4,2]]
#     bbox(cast("mf-face3d", sec))    [[0,0],[4,4]]      ← z が消える (直す前)
#
# ★ 機序: PLY2 は regions のあとに **ガイド層の節** を挟み、その後ろに枠 ("MARF") を置く。
#   mf / gu の decode_cross_exact が **regions で止まっていた** ので枠に届かなかった。
#   #3526 で mf→cg の向きは直っていたが、**cg→mf の向きが残っていた**。
#
# ⚠ 「3 成分になったか」だけでは足りない。**高さの違う 2 枚が別の答えになること**まで見る
#   (枠を既定値で埋めても 3 成分にはなるため)。
. "$(dirname "$0")/srava_hangwatch.sh"

SRAVA="${1:?srava not given}"
D="${SRAVA_CACHE_DIR:-/tmp/srava-castframe}"
fails=0
fail() { echo "FAIL: $*"; fails=$((fails+1)); }
# ★ #3569: all.sra (16 本) をやめ、このケースが要る 3 本だけを **ソースの先頭で** 読む
#   (cgal = box / section・manifold = mf-face3d の産出・geomutils = gu-face3d と bbox)。
run() { rm -rf "$2"; SRAVA_CACHE_DIR="$2" SRAVA_SOURCE="$1" "$SRAVA" 2>&1; }

SRC='module("cgal.so",{}); module("manifold.so",{}); module("geomutils.so",{});
var s2 = section(box(4,4,10), [0,0,2], [0,0,1], 0);
var s7 = section(box(4,4,10), [0,0,7], [0,0,1], 0);
print("CG2", bbox(s2));
print("CG7", bbox(s7));
print("MF2", bbox(cast("mf-face3d", s2)));
print("MF7", bbox(cast("mf-face3d", s7)));
print("GU2", bbox(cast("gu-face3d", s2)));
print("GU7", bbox(cast("gu-face3d", s7)));
print("RT_MF", bbox(cast("cg-face3d", cast("mf-face3d", s7))));
print("RT_GU", bbox(cast("cg-face3d", cast("gu-face3d", s7))));
'
O=$(run "$SRC" "$D-a")
get() { printf '%s\n' "$O" | sed -n "s/^$1 //p"; }
EXP2='[[0,0,2],[4,4,2]]'
EXP7='[[0,0,7],[4,4,7]]'
for k in CG2 MF2 GU2; do
	V=$(get $k); [ "$V" = "$EXP2" ] || fail "$k が $EXP2 でない: '$V' (枠が落ちている)"
done
for k in CG7 MF7 GU7 RT_MF RT_GU; do
	V=$(get $k); [ "$V" = "$EXP7" ] || fail "$k が $EXP7 でない: '$V' (枠が落ちている)"
done
# ★ 高さ違いが **別の答え** であること (枠を既定で埋めただけでは通らないようにする)
[ "$(get MF2)" != "$(get MF7)" ] || fail "mf: 高さ違いの断面が同じ bbox を返した"
[ "$(get GU2)" != "$(get GU7)" ] || fail "gu: 高さ違いの断面が同じ bbox を返した"
[ "$fails" = "0" ] && echo "      ① cg / mf / gu の face3d が z を保つ ・ 往復しても戻らない"

# ---- ② ガイドを持つ 2D は **断る** (黙って落とさない) ----
# ★ ガイド (line(...) の開いた折れ線) は mf / gu の表現に無い。黙って捨てると
#   nverts が cgal と食い違う ⇒ 明示エラーにしてある。
for T in mf-cross2d gu-cross2d; do
	printf '%s\n' "$(run 'module("cgal.so",{}); module("manifold.so",{}); module("geomutils.so",{});
	print("X", nverts(cast("'"$T"'", line([[0,0],[1,1],[2,0]]))));' "$D-g$T")" \
	  | grep -q 'guide polylines' || fail "$T: ガイドを持つ値を黙って受けた"
done
[ "$fails" = "0" ] && echo "      ② ガイドを持つ 2D の cast は **断る** (mf / gu とも)"

if [ "$fails" != "0" ]; then
	echo "FAIL: $fails 件"
	exit 1
fi
echo "CASTFRAME_OK cast は置き場所 (枠) を落とさない"
