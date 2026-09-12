#!/bin/sh
# 素性を訊く op (bbox / centroid / area / valid) の回帰 (#3487)。
#
# $1 = srava 実行体 / $2 = モジュール名 (.so) / $3 = 許容誤差
# $4 = openvdb だけに要る dx (",0.05" の形で生成 op の末尾へ連結)
# $5 = 自己交差 tube に対する valid の期待値 ("-" = そのカーネルでは検査しない)
# env: SRAVA_AGENT, SRAVA_CACHE_DIR。
#
# ---- 検証は **閉形式** で行う (kernel_agree ではない) ----
# ★ カーネル同士を突き合わせても、両方が同じように間違っていたら気づけない (#3470 の occt tube
#   と同じ理由)。ここは真値が手で書けるものだけを並べる:
#     box(2,3,4) を [1,1,1] へ寄せた立体 … bbox=[[1,1,1],[3,4,5]] / 重心=[2,2.5,3] / 表面積=52
#     prism(6,2,1) (正六角柱・外接半径 1・高さ 2) … 表面積 = 2·(3√3/2) + 6·1·2 = 17.196152422706632
# ⚠ **球は入れない**。occt は厳密な球面 (4πr²)・mesh 系は内接多面体なので、体積と同じく
#   構造的に違う値になる。誤差を緩めて通すのは偽のテストになる (kernel_agree の判断と同じ)。
#
# ---- valid の共通定義 ----
# ① 空でない ∧ ② 閉じている ∧ ③ 自己交差が無い  (src/h/common/meshprops.h の冒頭に根拠)。
#   ★ ここが揃っていなかった 2 件を直した回帰でもある (2026-09-05 実測):
#       cgal     … 空メッシュに **1** を返していた (① が抜けていた)
#       manifold … 自己交差 tube に **1** を返していた (③ が抜けていた。型不変条件は
#                  自己交差を含まない)。他の 4 カーネルは 0 と答えていた。
#   ⚠ openvdb だけ ②③ が **構造的に恒真** (距離場は境界も自己交差も表現できない) なので、
#     自己交差した掃引も 1 になる。第 5 引数で期待値を渡す。
#   ⚠ occt は "-" (検査しない)。occt の tube は **自己交差する背骨を掃引そのものが断る**
#     ので、自己交差した値を作れない (エラーで止まる = それはそれで正しい振る舞い)。
SRAVA="$1"
SO="${2:?module .so not given}"
TOL="${3:-1e-9}"
DX="$4"
TUBEV="${5:-0}"
D="${SRAVA_CACHE_DIR:?SRAVA_CACHE_DIR not set}"

if [ -n "$DX" ]; then A=",$DX"; else A=""; fi
# ⚠ empty3d は引数を 1 つも取らない形なので、dx は **カンマ無し**で渡す (openvdb だけ)。
E="$DX"

# ★ 自己交差する掃引 (#3445 の受け入れモデル)。各カーネル **自身の** tube で作るので、
#   別カーネルを持ち込まずに済む (生成元が変わると見ているものが変わる → #3485)。
SELFX="tube([[[0,0,0],0.8],[[10,0,0],0.8],[[10,0,2],0.8],[[0,0,2],0.8],[[0,0,4],0.8],[[5,0,4],0.8],[[5,0,-2],0.8]], 12$A)"
if [ "$TUBEV" = "-" ]; then
	SELFXLINE=""
	TUBEBIT=""
else
	SELFXLINE="print(\"BIT\", valid($SELFX));"
	TUBEBIT=" $TUBEV"
fi

SRC=$(cat <<EOF
module("$SO",{priority:99});
var b = translate(box(2,3,4$A),[1,1,1]);
var bb = bbox(b);
print("VAL", bb[0][0]); print("VAL", bb[0][1]); print("VAL", bb[0][2]);
print("VAL", bb[1][0]); print("VAL", bb[1][1]); print("VAL", bb[1][2]);
var c = centroid(b);
print("VAL", c[0]); print("VAL", c[1]); print("VAL", c[2]);
print("VAL", area(b));
print("BIT", valid(b));
print("BIT", valid(empty3d($E)));
print("VAL", area(prism(6,2,1$A)));
$SELFXLINE
EOF
)

# VAL = 数値 (許容誤差つき) / BIT = 判定値 (完全一致)。混ぜると bbox の "1" が
# valid の 1 と区別できなくなるので、印を分ける。
EXP="1 1 1 3 4 5 2 2.5 3 52 17.196152422706632"
EXPBIT="1 0$TUBEBIT"

rm -rf "$D-mp"
OUT=$(SRAVA_CACHE_DIR="$D-mp" SRAVA_SOURCE="$SRC" "$SRAVA" 2>&1)
GOT=$(echo "$OUT" | sed -n 's/^VAL //p')
GOTBIT=$(echo "$OUT" | sed -n 's/^BIT //p')

n=0
set -- $EXP
for g in $GOT; do
	n=$((n+1))
	e="$1"; shift
	[ -n "$e" ] || { echo "FAIL: 数値の出力が期待より多い ($n 個目 = $g)"; echo "$OUT"; exit 1; }
	ok=$(awk -v g="$g" -v e="$e" -v t="$TOL" 'BEGIN{
		d=g-e; if(d<0)d=-d; s=(e<0?-e:e); if(s<1)s=1;
		print (g!="" && d/s <= t) ? 1 : 0 }')
	[ "$ok" = "1" ] || { echo "FAIL: 数値の $n 個目が $g (期待 $e ・許容 $TOL)"; echo "$OUT"; exit 1; }
done
[ "$#" = 0 ] || { echo "FAIL: 数値の出力が足りない ($n 個・残り $*)"; echo "$OUT"; exit 1; }

set -- $EXPBIT
for g in $GOTBIT; do
	n=$((n+1))
	e="$1"; shift
	[ -n "$e" ] || { echo "FAIL: 判定値の出力が期待より多い ($g)"; echo "$OUT"; exit 1; }
	[ "$g" = "$e" ] || { echo "FAIL: valid が $g (期待 $e ・判定値なので完全一致)"; echo "$OUT"; exit 1; }
done
[ "$#" = 0 ] || { echo "FAIL: 判定値の出力が足りない (残り $*)"; echo "$OUT"; exit 1; }

echo "MESHPROPS-OK $SO ($n checks)"
