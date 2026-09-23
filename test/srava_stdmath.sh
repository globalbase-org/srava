#!/bin/sh
# stdlib の回転ヘルパ (vcross / rotmat_2v / mat34 / rotate_v) の回帰 (#3488)。
# $1 = srava 実行体。env: SRAVA_AGENT, SRAVA_CACHE_DIR, SRAVA_LIB (std の置き場所)。
#
# ---- 検証は **閉形式** ----
# ★ rotmat_2v の定義そのもの: matvec(R, vnorm(v1)) == vnorm(v2)。
# ★ 回転であること (反射が混じっていない): 行が正規直交・det(R) == +1。
#   det は vcross を使って R[0]·(R[1]×R[2]) で出す (追加の行列式ヘルパを持ち込まない)。
# ★ 退化 3 種を明示的に: 同じ向き / 逆向き / 零ベクトル。
# ★ mat34 は「rotmat_* をメッシュへ当てる道」なので、**メッシュに当てて位置で**確かめる
#   (体積は剛体変換で不変なので、体積だけでは何も言えない → #3486 と同じ注意)。
SRAVA="$1"
D="${SRAVA_CACHE_DIR:?SRAVA_CACHE_DIR not set}"
# ★★ #3522: ハングの番犬 (共通・常時 ON)。詳細は test/srava_hangwatch.sh。
. "$(dirname "$0")/srava_hangwatch.sh"
LIB="${SRAVA_LIB:?SRAVA_LIB not set}"

rm -rf "$D-sm"
OUT=$(SRAVA_CACHE_DIR="$D-sm" SRAVA_SOURCE="
include \"$LIB/std/math.sra\";
module(\"cgal.so\",{priority:99});

// ① vcross — 右手系
var c1 = vcross([1,0,0],[0,1,0]);
print(\"VAL\", c1[0]); print(\"VAL\", c1[1]); print(\"VAL\", c1[2]);

// ② 定義そのもの: R·vnorm(v1) == vnorm(v2) (一般の向きの 2 本で)
var v1 = [1.0, 2.0, 3.0];
var v2 = [-4.0, 1.0, 2.0];
var R  = rotmat_2v(v1, v2);
var got = matvec(R, vnorm(v1));
var want = vnorm(v2);
print(\"VAL\", got[0] - want[0]); print(\"VAL\", got[1] - want[1]); print(\"VAL\", got[2] - want[2]);

// ③ 回転であること: 行が正規直交・det = +1 (反射なら -1 になる)
print(\"VAL\", vdot(R[0],R[0]) - 1.0);
print(\"VAL\", vdot(R[1],R[1]) - 1.0);
print(\"VAL\", vdot(R[2],R[2]) - 1.0);
print(\"VAL\", vdot(R[0],R[1]));
print(\"VAL\", vdot(R[0],R[2]));
print(\"VAL\", vdot(R[1],R[2]));
print(\"VAL\", vdot(R[0], vcross(R[1],R[2])) - 1.0);

// ④ 退化: 同じ向き → 単位行列
var I2 = rotmat_2v([1,2,3],[2,4,6]);
print(\"VAL\", I2[0][0] - 1.0); print(\"VAL\", I2[0][1]); print(\"VAL\", I2[1][1] - 1.0); print(\"VAL\", I2[2][2] - 1.0);

// ⑤ 退化: 逆向き → 180 度。R·a == -a で det も +1 (点対称 = det -1 ではない)
var Rop = rotmat_2v([0,0,1],[0,0,-1]);
var g2  = matvec(Rop, [0.0,0.0,1.0]);
print(\"VAL\", g2[0]); print(\"VAL\", g2[1]); print(\"VAL\", g2[2] + 1.0);
print(\"VAL\", vdot(Rop[0], vcross(Rop[1],Rop[2])) - 1.0);
// ★ 決定的であること: 同じ入力なら同じ軸を選ぶ (2 回呼んで一致)
var Rop2 = rotmat_2v([0,0,1],[0,0,-1]);
print(\"VAL\", Rop[0][0] - Rop2[0][0]); print(\"VAL\", Rop[1][1] - Rop2[1][1]);

// ⑥ 退化: 零ベクトル → NaN (黙って単位行列を返さない)
print(\"NAN\", rotmat_2v([0,0,0],[0,0,1])[0][0]);

// ⑦ mat34: rotmat_* を **メッシュへ**当てられる (これが無いと点列専用だった)
var bar = box(3,1,1);
print(\"VAL\", volume(transform(bar, mat34(rotmat_z(rad(90)))) &&& translate(box(0.6,0.6,0.6),[-0.8,2.2,0.2])) - 0.216);
// ⑧ mat34_t: 平行移動つき
print(\"VAL\", volume(transform(bar, mat34_t(rotmat_z(rad(90)), [5,0,0]))
                     &&& translate(box(0.6,0.6,0.6),[4.2,2.2,0.2])) - 0.216);
// ⑨ rotate_v: x 軸の向きを z 軸の向きへ → [-1,0]x[0,1]x[0,3]
print(\"VAL\", volume(rotate_v(bar, [1,0,0], [0,0,1])) - 3.0);
print(\"VAL\", volume(rotate_v(bar, [1,0,0], [0,0,1]) &&& translate(box(0.6,0.6,0.6),[-0.8,0.2,2.2])) - 0.216);
" "$SRAVA" 2>&1)

# ① の 3 つは 0/0/1・他はすべて「0 との差」なので、まとめて |x| <= tol で見る。
EXP="0 0 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0"
TOL=1e-12

GOT=$(echo "$OUT" | sed -n 's/^VAL //p')
n=0
set -- $EXP
for g in $GOT; do
	n=$((n+1))
	e="$1"; shift
	[ -n "$e" ] || { echo "FAIL: 出力が期待より多い ($n 個目 = $g)"; echo "$OUT"; exit 1; }
	ok=$(awk -v g="$g" -v e="$e" -v t="$TOL" 'BEGIN{ d=g-e; if(d<0)d=-d; print (g!="" && d<=t) ? 1 : 0 }')
	[ "$ok" = "1" ] || { echo "FAIL: $n 個目が $g (期待 $e ・許容 $TOL)"; echo "$OUT"; exit 1; }
done
[ "$#" = 0 ] || { echo "FAIL: 出力が足りない ($n 個・残り $*)"; echo "$OUT"; exit 1; }

# 零ベクトルは NaN であること (0 でも 1 でもない = 黙って恒等にならない)
NANV=$(echo "$OUT" | sed -n 's/^NAN //p')
case "$NANV" in
*[Nn][Aa][Nn]*) ;;
*) echo "FAIL: 零ベクトルの rotmat_2v が '$NANV' (NaN であるべき)"; echo "$OUT"; exit 1 ;;
esac
n=$((n+1))

echo "STDMATH-OK ($n checks)"
