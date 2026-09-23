#!/bin/sh
# ★★ #3532: 格子から自由曲面 — surface_through (通過点) / surface_control (制御点)。
#
# $1 = srava 実行体 / $2 = モジュール名 (.so) / $3 = 許容誤差
# env: SRAVA_AGENT, SRAVA_CACHE_DIR。
#
# ---- 何を固定するか ----
# ★★ この 2 つは **同じ綴り (三重アレイ) を受け取り、点の意味だけが違う**。
#    ⇒ 取り違えたときに黙って別の面が出るので、**意味の違いそのもの**を検査する。
#      surface_through … 面が点を **通る**
#      surface_control … 面は点を通らない (通るのは四隅だけ)。点は面を引っぱる
#
# ---- 閉形式で測る ----
# ★ 平らな格子: 面積は格子の矩形の面積そのもの (2x2 の正方形 → 4 ・ 単位正方形 → 1)。
# ★★ bbox は **制御点の箱**である (ocShape.cpp の BRepBndLib::Add(..., Standard_False))。
#    ⚠⚠ #3546 で AddOptimal (真の箱) へ替えようとしたが **撤回した** — B-spline 面で箱を
#      **過小**に返し (0.9956 対 真値 1.0)、箱が面を含まなくなるため。許容差を 1e-3〜1e-12 と
#      締めても誤った極値へ収束する。⇒ poles の箱 (過大 = 安全側) のままにしてある。
#    ★ #3546 で入ったのは **2D の経路を 3D へ揃えた**こと (useTriangulation=false + SetGap(0.0))。
#      ⇒ 以前あった 1e-7 の端数が消え、下の期待値は **ちょうど**出るようになった。
#    B-spline / Bezier 面では poles から作られるので、曲面の外接箱より **広い**。
#    ⇒ surface_control では bbox が **与えた格子の箱そのもの**になる (閉形式)。
#      surface_through では通過のために poles が行き過ぎるので **箱が外へ膨らむ**。
#      ⇒ この差が「通る / 通らない」の観測になる。
#    ⚠ Bnd_Box は既定で 1e-7 の余裕を持つ (ocaBbox の注記どおり) ので、比較は許容差つきで行う。
SRAVA="$1"
SO="${2:?module .so not given}"
TOL="${3:-1e-6}"
D="${SRAVA_CACHE_DIR:?SRAVA_CACHE_DIR not set}"
# ★★ #3522: ハングの番犬 (共通・常時 ON)。詳細は test/srava_hangwatch.sh。
. "$(dirname "$0")/srava_hangwatch.sh"

NG=0

run() {   # run <cache-suffix> <source>
	rm -rf "$D-$1"
	SRAVA_CACHE_DIR="$D-$1" SRAVA_SOURCE="module(\"$SO\",{priority:99});
$2" "$SRAVA" 2>&1
}
val() { echo "$1" | sed -n 's/^VAL[ 	]*//p' | head -1; }

# near <名前> <実測> <真値>
near() {
	awk -v a="$2" -v b="$3" -v t="$TOL" -v n="$1" '
	  BEGIN{ d = a - b; if (d < 0) d = -d;
	         if (a == "" ) { printf "FAIL: %s が空\n", n; exit 1 }
	         if (d > t)    { printf "FAIL: %s = %s (真値 %s ・ 差 %g)\n", n, a, b, d; exit 1 }
	         printf "  ok %s = %s\n", n, a }' || NG=1
}

# ---- ① 平らな格子は閉形式で面積が決まる (2 つの op が **一致する** 唯一の場) ----
FLAT3='[[[0,0,0],[1,0,0],[2,0,0]],[[0,1,0],[1,1,0],[2,1,0]],[[0,2,0],[1,2,0],[2,2,0]]]'
near "surface_through(平ら 3x3) の面積" \
     "$(val "$(run t1 "print(\"VAL\", area(surface_through($FLAT3)));")")" 4
near "surface_control(平ら 3x3) の面積" \
     "$(val "$(run t2 "print(\"VAL\", area(surface_control($FLAT3)));")")" 4
near "surface_control(平ら 2x2) の面積" \
     "$(val "$(run t3 "print(\"VAL\", area(surface_control([[[0,0,0],[1,0,0]],[[0,1,0],[1,1,0]]])));")")" 1

# ---- ② ★★ 「通る / 通らない」— 中央だけ持ち上げた格子で bbox の z 上端を見る ----
#   外周 8 点が z=0・中央が z=1。制御点なら箱は格子の箱のまま (z 上端 = 1)。
#   通過点なら中央を通すために poles が上へ行き過ぎる (z 上端 > 1)。
BUMP='[[[0,0,0],[1,0,0],[2,0,0]],[[0,1,0],[1,1,1],[2,1,0]],[[0,2,0],[1,2,0],[2,2,0]]]'
ZC=$(val "$(run t4 "print(\"VAL\", bbox(surface_control($BUMP))[1][2]);")")
ZT=$(val "$(run t5 "print(\"VAL\", bbox(surface_through($BUMP))[1][2]);")")
near "surface_control の bbox z 上端 (= 与えた格子の箱)" "$ZC" 1
awk -v c="$ZC" -v t="$ZT" 'BEGIN{
	if (t == "" || c == "") { print "FAIL: bbox の z 上端が空"; exit 1 }
	if (t <= c + 1e-6) { printf "FAIL: 通過点の poles が膨らんでいない (through %s <= control %s)\n", t, c; exit 1 }
	printf "  ok 通過点は poles が外へ膨らむ (through %s > control %s)\n", t, c }' || NG=1

# ---- ③ 高さ場 (数値の二重アレイ) も同じ op が受ける ----
case "$(run t6 "print(surface_through([[0,0,0],[0,0,0],[0,0,0]]));")" in
	*oc-face3d*) echo "  ok 高さ場から oc-face3d ができる" ;;
	*) echo "FAIL: 高さ場が oc-face3d にならない"; NG=1 ;;
esac

# ---- ④ ★ 黙って通さないもの — 綴りの誤りは **どこが違うか**まで言う ----
chk_err() {   # chk_err <名前> <出力> <期待する断片>
	case "$2" in
		*"$3"*) echo "  ok $1" ;;
		*) echo "FAIL: $1 — 期待した文言が出ない ($3)"; echo "      出力: $2"; NG=1 ;;
	esac
}
chk_err "ragged は行番号と点数を言う" \
  "$(run e1 "print(area(surface_control([[[0,0,0],[1,0,0]],[[0,1,0]]])));")" \
  "row 1 has 1 points, row 0 has 2"
chk_err "mode の綴り違いは候補を言う" \
  "$(run e2 "print(area(surface_through([[[0,0,0],[1,0,0]],[[0,1,0],[1,1,0]]], \"interpolate\")));")" \
  'mode must be "fit" (default) or "interp"'
chk_err "点と高さの混在は断る" \
  "$(run e3 "print(area(surface_through([[[0,0,0],[1,0,0]],[0,1]])));")" \
  "do not mix"
chk_err "行が 1 本では断る" \
  "$(run e4 "print(area(surface_control([[[0,0,0],[1,0,0]]])));")" \
  ">= 2 rows"

# ---- ⑤ ★★ poles / set_poles — 「混在」を op にせず **合成**で書けることの検査 ----
#   ★ poles(surface_control(G)) は **G そのもの** (制御点として渡したのだから当然。往復が閉じる)
#   ⚠ 解析曲面 (球・平面…) は制御網を持たない ⇒ 断ることまで見る
POUT=$(val "$(run p1 "print(\"VAL\", poles(surface_control($BUMP)));")")
if [ "$POUT" = "$BUMP" ]; then echo "  ok poles(surface_control(G)) = G (往復が閉じる)"
else echo "FAIL: poles の往復が閉じない"; echo "      得た: $POUT"; echo "      期待: $BUMP"; NG=1; fi

chk_err "解析曲面には制御網が無いと言う" \
  "$(run p2 "print(poles(face(sphere(1),0)));")" \
  "no control net"

#   ★★ 合成そのもの: 当てる → 網を見る → 1 点だけ動かす → 差し替える。
#     bbox は制御網の箱なので、動かした値がそのまま z 上端に出る (閉形式)。
COMP=$(run p3 'var S = surface_control([[[0,0,0],[1,0,0],[2,0,0]],[[0,1,0],[1,1,1],[2,1,0]],[[0,2,0],[1,2,0],[2,2,0]]]);
var P = poles(S);
P[1][1][2] = 2;
print("VAL", bbox(set_poles(S, P))[1][2]);')
near "set_poles で持ち上げた制御点が箱に出る" "$(val "$COMP")" 2

chk_err "形の違う格子は断る" \
  "$(run p4 "print(area(set_poles(surface_control($BUMP), [[[0,0,0],[1,0,0]],[[0,1,0],[1,1,0]]])));")" \
  "the grid has 2 rows but the surface has 3"

[ "$NG" -eq 0 ] && echo "OCCT-SURFACE-OK"
exit "$NG"
