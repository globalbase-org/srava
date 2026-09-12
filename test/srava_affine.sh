#!/bin/sh
# アフィン変換 4 op (rotate / scale / mirror / transform) の **位置と向き** の回帰 (#3486)。
#
# $1 = srava 実行体 / $2 = モジュール名 (module() に渡す .so) / $3 = 許容誤差
# $4 = openvdb だけに要る dx (省略時は無し) / $5 = "cg" なら向きを cgal 側で検算する。
# env: SRAVA_AGENT, SRAVA_CACHE_DIR。
#
# ---- なぜ体積だけでは足りないか ----
# ★★ **体積は剛体変換で不変**なので、rotate / mirror / translate が何もしていなくても
#   volume は正しい値を返す。#3474 で occt の box / prism の置き場所を **2 件見逃した**のは
#   まさにこれで、kernel_agree (体積の突き合わせ) を通っていた。
#   ⇒ ここでは **原点から離した小さな箱で切り取って位置を値に出す** (box_cut と同じ手)。
#     probe は変換後の立体の **内部に完全に入る**ように置くので、期待値は probe 自身の体積
#     0.6^3 = 0.216 ちょうど。位置が違えば 0 になる (半端な値にはならない = 判定が鋭い)。
#   ⚠ probe を面でちょうど接するように置いてはいけない。cherchi は測度 0 の接触で
#     誤値を返す既知の限界がある (modules/cherchi/c++/chMesh.h 参照)。
#
# ---- 向き (面の法線) をどう見るか ----
# ★ 反射 (det<0) は面の向きを裏返す。裏返ったままだと立体の内外が入れ替わるが、
#   **体積の絶対値は変わらない**ので volume だけでは検出できない。そこで
#     ① mirror した立体から内部の probe を **引く** (= 3 - 0.216)
#   を見る。向きが裏返っていると「立体の外側」から引くことになり値が合わない。
#
# ---- 回転の符号と軸 ----
# ★ rotate("z", +90) と rotate("z", -90) を **別々の場所**で捕まえる。片方だけだと
#   「回ってはいるが逆向き」を通してしまう。軸違い (x まわり) も 1 本入れる。
SRAVA="$1"
SO="${2:?module .so not given}"
TOL="${3:-1e-9}"
DX="$4"                     # openvdb だけ: 生成器の末尾引数 (",0.05" の形で連結する)
CGCHK="$5"                  # "cg" = 向きを cgal へ渡して検算する (下の ⑭)
D="${SRAVA_CACHE_DIR:?SRAVA_CACHE_DIR not set}"

# ボクセルカーネル (openvdb) は生成 op の末尾に dx が要る。他は空。
if [ -n "$DX" ]; then A=",$DX"; else A=""; fi

BAR="box(3,1,1$A)"
P="box(0.6,0.6,0.6$A)"

SRC=$(cat <<EOF
module("$SO",{priority:99});
var bar = $BAR;
// ① 素の体積 (前提: 箱は [0,3]x[0,1]x[0,1] = 角が原点)
print("VAL", volume(bar));
print("VAL", volume(bar &&& translate($P,[2.2,0.2,0.2])));

// ② rotate("z",+90): (x,y) -> (-y,x) なので [-1,0]x[0,3]x[0,1] へ移る
print("VAL", volume(rotate(bar,"z",90)));
print("VAL", volume(rotate(bar,"z",90) &&& translate($P,[-0.8,2.2,0.2])));
// ③ rotate("z",-90): [0,1]x[-3,0]x[0,1]。**符号**を固定する
print("VAL", volume(rotate(bar,"z",-90) &&& translate($P,[0.2,-2.8,0.2])));
// ④ rotate("x",90): (y,z) -> (-z,y) なので [0,3]x[-1,0]x[0,1]。**軸**を固定する
print("VAL", volume(rotate(bar,"x",90) &&& translate($P,[2.2,-0.8,0.2])));
// ⑤ 軸をベクトルで書いても "z" と同じ
print("VAL", volume(rotate(bar,[0,0,1],90) &&& translate($P,[-0.8,2.2,0.2])));

// ⑥ scale([2,1,1]): [0,6]x[0,1]x[0,1]・体積は 2 倍
print("VAL", volume(scale(bar,[2,1,1])));
print("VAL", volume(scale(bar,[2,1,1]) &&& translate($P,[5.2,0.2,0.2])));
// ⑦ scale(スカラ) は均等
print("VAL", volume(scale(bar,2) &&& translate($P,[5.2,1.2,1.2])));

// ⑧ mirror("x"): [-3,0]x[0,1]x[0,1]・体積は不変
print("VAL", volume(mirror(bar,"x")));
print("VAL", volume(mirror(bar,"x") &&& translate($P,[-2.8,0.2,0.2])));
// ⑨ ★ 向き: 反射した立体から probe を **端から差し込んで**引く (3 - 0.4*0.36 = 2.856)。
//   面の向きが裏返ったままだと内外が入れ替わるのでこの値にならない。
//   ⚠ probe を立体の **内部に完全に入れて**はいけない: 空洞になるが openvdb (level set)
//     は狭帯域の外に空洞を表現できず値が変わらない。端から差し込めば 7 カーネル共通に効く。
print("VAL", volume(difference(mirror(bar,"x"), translate($P,[-3.2,0.2,0.2]))));
// ⑩ mirror は法線ベクトルでも書ける
print("VAL", volume(mirror(bar,[0,1,0]) &&& translate($P,[2.2,-0.8,0.2])));

// ⑪ transform: 行優先 12 要素 (3x4) の平行移動
print("VAL", volume(transform(bar,[1,0,0,10, 0,1,0,0, 0,0,1,0]) &&& translate($P,[12.2,0.2,0.2])));
// ⑫ 16 要素 (4x4) でも同じ (最終行は読み飛ばす)
print("VAL", volume(transform(bar,[1,0,0,10, 0,1,0,0, 0,0,1,0, 0,0,0,1]) &&& translate($P,[12.2,0.2,0.2])));
// ⑬ transform に回転行列を入れると rotate("z",90) と同じ
print("VAL", volume(transform(bar,[0,-1,0,0, 1,0,0,0, 0,0,1,0]) &&& translate($P,[-0.8,2.2,0.2])));
EOF
)

# 期待値 (すべて閉形式。probe は一辺 0.6 の箱 = 0.216)。
EXP="3 0.216 3 0.216 0.216 0.216 0.216 6 0.216 0.216 3 0.216 2.856 0.216 0.216 0.216 0.216"

rm -rf "$D-af"
OUT=$(SRAVA_CACHE_DIR="$D-af" SRAVA_SOURCE="$SRC" "$SRAVA" 2>&1)
GOT=$(echo "$OUT" | sed -n 's/^VAL //p')

n=0
set -- $EXP
for g in $GOT; do
	n=$((n+1))
	e="$1"; shift
	[ -n "$e" ] || { echo "FAIL: 出力が期待より多い ($n 個目 = $g)"; echo "$OUT"; exit 1; }
	ok=$(awk -v g="$g" -v e="$e" -v t="$TOL" 'BEGIN{
		d=g-e; if(d<0)d=-d; s=(e<0?-e:e); if(s<1)s=1;
		print (g!="" && d/s <= t) ? 1 : 0 }')
	[ "$ok" = "1" ] || { echo "FAIL: $n 個目が $g (期待 $e ・許容 $TOL)"; echo "$OUT"; exit 1; }
done
[ "$#" = 0 ] || { echo "FAIL: 出力が足りない ($n 個しか出ていない・残り $*)"; echo "$OUT"; exit 1; }

# ---- ⑭ 向きを **別カーネルで**検算する (cg へ cast して符号付き体積を見る) ----
# ★★ **geogram の volume() は絶対値** (mesh_enclosed_volume が fabs して返す) なので、面が
#   裏返っていても 3 のままで、上の ⑨ を含めて**このカーネル内のどの検査も向きを見られない**。
#   実測 (2026-09-05・向き補正を外したビルド): geogram は 17 検査すべて通り、cherchi だけが
#   volume=-3 で落ちた。⇒ 裏返ったまま **他のカーネルへ渡ると初めて誤りが出る**。
#   そこで cgal (符号付き体積) へ cast して +3 を確かめる。
#   ⚠ occt / openvdb は飛ばす: occt は cg への cast 経路が無く (oc-brep3d → cg は宣言されて
#     いない)、openvdb は距離場なので面の向きという概念そのものが無い。
if [ "$CGCHK" = "cg" ]; then
	rm -rf "$D-ac"
	#   ★ #3499: nef_snc は「常に SNC だけ」を書くので、cg への変換は **橋モジュール**
	#     nef_cg.so が持つ (cgal.so は CGAL Nef 非依存で SNC を読めない)。他のカーネルには
	#     不要だが optional:1 なので無害 — 在れば載り、無ければ黙って飛ぶ。
	V=$(SRAVA_CACHE_DIR="$D-ac" SRAVA_SOURCE="module(\"cgal.so\",{priority:50}); module(\"nef_cg.so\",{optional:1}); module(\"$SO\",{priority:99});
	    print(\"V\", volume(cast(\"cg-mesh3d\", mirror($BAR,\"x\"))));" "$SRAVA" 2>&1)
	G=$(echo "$V" | sed -n 's/^V //p')
	ok=$(awk -v g="$G" 'BEGIN{ d=g-3; if(d<0)d=-d; print (g!="" && d<1e-9) ? 1 : 0 }')
	[ "$ok" = "1" ] || { echo "FAIL: cg へ渡した mirror の符号付き体積が $G (期待 3・負なら面が裏返っている)"; echo "$V"; exit 1; }
	n=$((n+1))
fi

# ---- エラー文がカーネル間で **同じ**であること (common/affine.h に集約した効果) ----
# ★ 揃えたのは受け付ける書き方だけでなく **拒否の理由**。ここが割れていると、同じ書き間違いを
#   しても走ったカーネルによって違う説明が返る。
rm -rf "$D-ae"
E1=$(SRAVA_CACHE_DIR="$D-ae" SRAVA_SOURCE="module(\"$SO\",{priority:99}); print(\"V\", volume(rotate($BAR,\"q\",10)));" "$SRAVA" 2>&1)
echo "$E1" | grep -q "unknown axis 'q'" || { echo "FAIL: 未知の軸のエラー文が違う"; echo "$E1"; exit 1; }
rm -rf "$D-ae2"
E2=$(SRAVA_CACHE_DIR="$D-ae2" SRAVA_SOURCE="module(\"$SO\",{priority:99}); print(\"V\", volume(transform($BAR,[1,2,3])));" "$SRAVA" 2>&1)
echo "$E2" | grep -q "matrix must have 12 (3x4) or 16 (4x4) elements" || { echo "FAIL: 行列長のエラー文が違う"; echo "$E2"; exit 1; }
rm -rf "$D-ae3"
E3=$(SRAVA_CACHE_DIR="$D-ae3" SRAVA_SOURCE="module(\"$SO\",{priority:99}); print(\"V\", volume(mirror($BAR,[0,0,0])));" "$SRAVA" 2>&1)
echo "$E3" | grep -q "degenerate normal vector \[0,0,0\]" || { echo "FAIL: 退化した法線のエラー文が違う"; echo "$E3"; exit 1; }

echo "AFFINE-OK $SO ($n checks)"
