#!/bin/sh
# ★★ #3537: **valid は回転で変わらない**。共有述語 self_intersects の誤検出の回帰。
#
#   $1 = srava 実行体 / $2 = モジュール名 (.so) / $3 = 自己交差 tube の valid の期待値
#        ("-" = そのカーネルでは作れない/検査しない)
#
# ---- 何を固定するか ----
# 直したいのは「特定の角度で落ちる」ことではなく **回転で答えが変わること**そのもの。
# ⚠ 角度を並べて期待値を書く形にすると、別の角度で落ちる版が素通りする — 実際 #3537 は
#   起票時に「厳密に表せる角度 (45 度) なら通る」と書かれ、再測で 90/180/270 も落ちて
#   **見立てが崩れた**。⇒ 検査は *不変量* の形にする。
#
#   ① 立体として妥当な形は、どう回しても valid = 1
#   ② 自己交差した形は、どう回しても valid = 0   ★ ①だけだと「常に 1」で通ってしまう
#
# ---- 何が壊れていたか (2026-09-14 の実測) ----
# valid = Status ∧ !empty ∧ !self_intersects (#3487) の最後が src/h/common/meshprops.h の
# double の述語。@revolve@ の立体は **円環の蓋に中心頂点が無い**ので「非隣接なのに同一平面」
# の三角形対を持つ (@cylinder@ の蓋は扇なので全部隣接扱いになり、この穴を踏まない)。
#   軸に揃っているうち  平面距離が厳密に 0 → coplanar 分岐が走り正しい
#   回すと              距離が ±1e-16 の雑音 → 一般分岐 → 除算なし版が桁落ちして誤検出
# ⇒ 平面距離を **|N| x |U-V0| に対する相対 ε** で 0 に丸め、coplanar 分岐へ落とす。
SRAVA="$1"
SO="${2:?module .so not given}"
SELFXV="${3:-0}"
D="${SRAVA_CACHE_DIR:?SRAVA_CACHE_DIR not set}"
# ★★ #3522: ハングの番犬 (共通・常時 ON)。詳細は test/srava_hangwatch.sh。
. "$(dirname "$0")/srava_hangwatch.sh"

# ★ 角度は「厳密に表せそうな」ものと、そうでないものを混ぜる。
#   #3537 が通るか落ちるかは *回転行列の成分が double でどうなるか*で決まっていて、
#   角度が「きれい」かどうかではなかった (45 度が通っていたのは cos45 == sin45 で
#   項がたまたま打ち消し合ったため。90 度は cos(pi/2) が 6.123e-17 で厳密ではない)。
ANGLES="7 11 23 30 37 45 53 60 73 90 101 120 137 150 180 211 233 270 299 330 347"

run() { rm -rf "$D-$1"; SRAVA_CACHE_DIR="$D-$1" SRAVA_SOURCE="module(\"$SO\",{priority:99});module(\"geomutils.so\",{});
$2" "$SRAVA" 2>&1; }

n=0
fail=0

# ---- ① 妥当な立体は、どう回しても valid = 1 ----
#   ★ revolve の立体を使う。これが **この穴を踏む唯一の形** (蓋に中心頂点が無い)。
SRC='var t = revolve(translate(rect(1,2),[3,0]), 360);
print("VAL", valid(t));'
for a in $ANGLES; do
	SRC="$SRC
print(\"VAL\", valid(rotate(t,\"x\",$a)));"
done
# ★ 1 軸だけだと「x 軸まわりだけ直った」を見逃す。合成回転も入れる
SRC="$SRC
print(\"VAL\", valid(rotate(rotate(t,\"y\",29),\"z\",47)));
print(\"VAL\", valid(rotate(rotate(rotate(t,\"x\",13),\"y\",29),\"z\",47)));"
OUT=$(run r1 "$SRC")
i=0
for v in $(echo "$OUT" | sed -n 's/^VAL //p'); do
	i=$((i+1))
	[ "$v" = "1" ] || { echo "FAIL: 妥当な立体の valid が $v (${i} 番目・期待 1)"; fail=1; }
	n=$((n+1))
done
[ "$i" -ge 24 ] || { echo "FAIL: valid の出力が $i 件しかない (期待 24 件以上)"; echo "$OUT"; fail=1; }

# ---- ② 自己交差した立体は、どう回しても valid = 0 ----
#   ★★ ①だけでは「述語が常に 0 を返す」版が満点で通る。**反対側を必ず置く**。
if [ "$SELFXV" != "-" ]; then
	SX='tube_ruled([[[0,0,0],0.8],[[10,0,0],0.8],[[10,0,2],0.8],[[0,0,2],0.8],[[0,0,4],0.8],[[5,0,4],0.8],[[5,0,-2],0.8]], 12)'
	SRC="var s = $SX;
print(\"VAL\", valid(s));"
	for a in 7 30 45 90 137 233; do
		SRC="$SRC
print(\"VAL\", valid(rotate(s,\"x\",$a)));"
	done
	OUT=$(run r2 "$SRC")
	i=0
	for v in $(echo "$OUT" | sed -n 's/^VAL //p'); do
		i=$((i+1))
		[ "$v" = "$SELFXV" ] || { echo "FAIL: 自己交差した立体の valid が $v (${i} 番目・期待 $SELFXV)"; fail=1; }
		n=$((n+1))
	done
	[ "$i" -ge 7 ] || { echo "FAIL: 自己交差側の出力が $i 件しかない"; echo "$OUT"; fail=1; }
fi

# ---- ③ 回しても形は変わらない (述語だけの話であることを固定する) ----
#   ⚠ valid が直っても体積が動いていたら、直したのは別のものになる
OUT=$(run r3 'var t = revolve(translate(rect(1,2),[3,0]), 360);
print("VAL", volume(t));
print("VAL", volume(rotate(t,"x",30)));
print("VAL", nverts(rotate(t,"x",30)));
print("VAL", nfaces(rotate(t,"x",30)));
print("VAL", nverts(t));
print("VAL", nfaces(t));')
# ⚠⚠ **値の分割だけ glob を止める** (2026-09-22)。
#   `set -- $(…)` の $( ) は **クォートできない** (単語分割が要る) のでパス名展開に掛かる。
#   比べる値は `[1,1,0]` 形 = glob の **文字クラス**なので、カレント (ビルド dir) に `1` という
#   名前のファイルが 1 つあるだけで `1` に化け、**「値が違う」という、それらしい文言で赤くなる**
#   (2026-09-21 に実際に踏んだ。⚠ PIG_TIMING は *ファイルパス* を取る env なので
#   `PIG_TIMING=1` と打つと `1` というファイルができる)。
#   ★★ **実行と分割を分ける**のが肝。`set -f; set -- $(cmd)` と囲うと -f が $( ) の中まで効き、
#     cmd がシェル関数だと *その中の glob* まで殺す (実測: count_tus → objs_of の
#     `CMakeFiles/*/link.txt` が読めず「全 0 本」になった)。⇒ 先に変数へ取ってから分割する。
_SPLIT_=$(echo "$OUT" | sed -n 's/^VAL //p')
set -f
set -- $_SPLIT_
set +f
awk -v a="$1" -v b="$2" 'BEGIN{d=a-b;if(d<0)d=-d;s=(a<0?-a:a);if(s<1)s=1;exit !(a!="" && d/s<=1e-12)}' \
	|| { echo "FAIL: 回すと体積が変わった ($1 → $2)"; fail=1; }
n=$((n+1))
[ "$3" = "$5" ] && [ "$4" = "$6" ] || { echo "FAIL: 回すと頂点/面の数が変わった ($5/$6 → $3/$4)"; fail=1; }
n=$((n+1))

# ---- ④ ★★ 接して非多様体になった立体は valid = 0 (2 つの直しの **かみ合わせ**) ----
#   模型は 2 球の xor = 交線の円で接する三日月 2 つ。溶接すると **4 回使われる辺が 108 本**
#   あるので、閉じた向きづけ可能な曲面ではない ⇒ valid = 0 が正しい。
#   ⚠⚠ ここは #3537 を直すまで **別の欠陥に隠れて**いた:
#     ・直す前は self_intersects が三角形対を *誤検出* して valid=0 になっていた
#       (厳密な有理数で確かめると、その対は交差していない = 答えだけ合っていた)
#     ・誤検出を止めた瞬間に valid=1 になり、is_closed が **生の頂点番号**で辺を数えて
#       いて継ぎ目を見ていなかったことが初めて出た (重複頂点 106)
#   ⇒ 溶接して数えるよう直し、manifold の valid も ② を共通定義に委ねるようにした。
#   ★ この検査が無いと、片方を戻したときに **もう片方が肩代わりして緑のまま**になる。
#   ⚠ cgal は接する立体の融合そのものを断る (boolean failed) ので、その場合は飛ばす。
#
#   ★★ #3543 (2026-09-15): **occt では長らく「正しい答えが誤った根拠で出て」いた**。
#     OCCT 7.8.1 の xor は *形そのものが壊れて*いて (体積が union の 25.0200 になる =
#     レンズの空洞が埋まる)、BRepAlgoAPI_Check がその破綻を咎めて 0 を返していた。
#     7.9.3 で xor が直った瞬間に体積は厳密値 21.7655924 になり、valid が **1 に化けた**
#     (mac で赤くなったのはこれ)。同じ機体で 2 版の検定器に 2 つの形を食わせると、
#     **答えは形だけで決まり版には依らない** = 検定器は元から非多様体を見ていなかった。
#     ⇒ occt 側に ② の多様体性 (稜の使われ回数) を足した。
#   ⇒ **④ は occt では負の対照にならない** (7.8.1 では形が壊れているので、多様体性の検査を
#     外しても 0 のまま緑になる)。版に依らない対照は ⑤ に置く。
OUT=$(run r4 'var a = sphere(1.5); var b = translate(sphere(1.5),[1,1,1]);
var x = (a --- b) ||| (b --- a);
print("VAL", valid(x));')
if echo "$OUT" | grep -q "boolean failed"; then
	echo "  note: $SO は接する xor を作れない (boolean failed) ので ④ は飛ばす"
else
	v=$(echo "$OUT" | sed -n 's/^VAL //p' | head -1)
	[ "$v" = "0" ] || { echo "FAIL: 接する xor (非多様体) の valid が $v (期待 0)"; fail=1; }
	n=$((n+1))
fi

# ---- ⑤ ★★ 稜だけで接する 2 立体は valid = 0 (#3543 の **負の対照**) ----
#   模型 = 単位立方体 2 つを [1,1,0] だけずらした和集合。重なりが無いので体積は **厳密に 2**、
#   共有するのは **稜 1 本だけ** ⇒ その稜を 4 面が使う = 非多様体。
#   ★★ ④ と違ってこれは **どの OCCT でも同じ形になる** (ブールが壊れていない)。しかも
#     OCCT 自身の BRepAlgoAPI_Check は **1 (妥当) と答える** ⇒ 0 を出せるのは
#     共通定義 ② の多様体性の検査だけ = **それを外すと必ず赤くなる**本物の負の対照。
#   ⚠ 稜でなく **頂点だけ**で接する形 ([1,1,1] ずらし) は、共通定義 ② が稜で数える以上
#     valid = 1 が正しい (manifold も 1)。ここでそれを 0 にしてはいけない。
#   ⚠ cgal は接する立体の融合を断る (boolean failed) ので、その場合は飛ばす。
OUT=$(run r5 'var e = box(1,1,1) ||| translate(box(1,1,1),[1,1,0]);
print("VAL", valid(e));
print("VOL", volume(e));
print("VAL", valid(box(1,1,1) ||| translate(box(1,1,1),[1,1,1])));')
if echo "$OUT" | grep -q "boolean failed"; then
	echo "  note: $SO は稜だけで接する和集合を作れない (boolean failed) ので ⑤ は飛ばす"
else
	_SPLIT_=$(echo "$OUT" | sed -n 's/^VAL //p')
	set -f
	set -- $_SPLIT_
	set +f
	[ "$1" = "0" ] || { echo "FAIL: 稜だけで接する 2 立体 (非多様体) の valid が $1 (期待 0)"; fail=1; }
	n=$((n+1))
	[ "$2" = "1" ] || { echo "FAIL: 頂点だけで接する 2 立体の valid が $2 (期待 1 — ② は稜で数える)"; fail=1; }
	n=$((n+1))
	# ★ 形が壊れて 0 になっているのではないことを固定する (重なりが無いので体積は厳密に 2)
	vol=$(echo "$OUT" | sed -n 's/^VOL //p' | head -1)
	awk -v g="$vol" 'BEGIN{d=g-2.0;if(d<0)d=-d;exit !(g!="" && d<1e-9)}' \
		|| { echo "FAIL: 稜だけで接する 2 立体の体積が $vol (厳密 2 — 形が壊れている)"; fail=1; }
	n=$((n+1))
fi

rm -rf "$D-r1" "$D-r2" "$D-r3" "$D-r4" "$D-r5"
[ "$fail" = "0" ] || { echo "FAIL: 上記のとおり"; exit 1; }
echo "SELFX-OK $n checks ($SO)"
