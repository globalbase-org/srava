#!/bin/sh
# ★★ #3534: **project_flatten** — 空間に置かれた 2D を z=0 へ落とす (world の (x,y) を取り z を捨てる)。
#
#   $1 = srava 実行体 / $2 = モジュール名 (.so)
#   $3 = "oc" なら occt (曲線の境界は近似・型が変わらない) / それ以外はメッシュ系
#
# ---- ★★ なぜ「実形のまま寝かせる」ではないか (ひさ 2026-09-14) ----
# 「実形のまま寝かせる」は **原理的に正準化できない** — 同じ図形の表裏は図形自体からは決まらず、
# 法線から枠を決める *連続な* 規約も存在しない (毛玉の定理)。どんな規約にも「わずかに傾けた
# だけで結果が跳ぶ場所」が必ずできる。⇒ **world 幾何だけで決まる射影**にした。
# 実形が欲しい利用者は *先に transform で XY と平行にしてから* これを当てる。
#
# ---- ★★ この検査の芯は ⑤ の「経路非依存」 ----
# 取り下げた案 A (枠の局所座標をそのまま (x,y) にする) は **同じ world 図形でも式の書き方で
# 結果が変わる**。rotate("x",180) と rotate("x",90) x 2 は world では同じ図形なのに、
# 前者は局所座標が y 反転・後者は枠が裏返る。⇒ ここが割れるかどうかが 2 つの案を分ける。
# ⚠ 他の項 (面積・冪等・エラー) は **案 A でも全部通る** ので、⑤ が無いと検査にならない。
SRAVA="$1"
SO="${2:?module .so not given}"
KIND="$3"
D="${SRAVA_CACHE_DIR:?SRAVA_CACHE_DIR not set}"
# ★★ #3522: ハングの番犬 (共通・常時 ON)。詳細は test/srava_hangwatch.sh。
. "$(dirname "$0")/srava_hangwatch.sh"

fail=0
n=0
run() { rm -rf "$D-$1"; SRAVA_CACHE_DIR="$D-$1" SRAVA_SOURCE="module(\"$SO\",{priority:99});
$2" "$SRAVA" 2>&1; }

# chk <説明> <実測> <期待> [相対許容 (既定 1e-12)]
chk() {
	t="${4:-1e-12}"
	awk -v a="$2" -v b="$3" -v t="$t" 'BEGIN{ if(a==""){exit 1} d=a-b; if(d<0)d=-d;
	      r=(b<0?-b:b); if(r<1e-12)r=1; exit !(d/r < t) }' \
		|| { echo "FAIL: $1 が $2 (期待 $3 ± 相対 $t)"; fail=1; }
	n=$((n+1))
}

MODEL='var U = rect(2,3);'

# ---- ① 平面が XY と平行なら **等長** (面積は厳密に不変) ----
#   ★ 2x3 = 6。素の 2D (既に z=0) も受ける = **冪等**の一部。
OUT=$(run p1 "$MODEL
print(\"VAL\", area(project_flatten(U)));
print(\"VAL\", area(project_flatten(rotate(U,\"x\",180))));
print(\"VAL\", area(project_flatten(translate(rotate(U,\"x\",180),[3,4,5]))));")
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
chk "素の 2D の面積"            "$1" 6
chk "180 度回した面積 (等長)"    "$2" 6
chk "持ち上げても面積 (等長)"    "$3" 6

# ---- ② 傾いていれば **cosθ で縮む** (影として正しい) ----
#   45 度なら 6*cos45 = 4.2426406871192848。⚠ 面積だけでは置き場所を見ていないので ④ と併用。
OUT=$(run p2 "$MODEL
print(\"VAL\", area(project_flatten(rotate(U,\"x\",45))));
print(\"VAL\", area(project_flatten(rotate(U,\"x\",60))));
print(\"VAL\", area(project_flatten(translate(rotate(U,\"x\",45),[5,1,2]))));")
_SPLIT_=$(echo "$OUT" | sed -n 's/^VAL //p')
set -f
set -- $_SPLIT_
set +f
chk "45 度の影の面積" "$1" 4.2426406871192848 1e-9
chk "60 度の影の面積" "$2" 3.0                1e-9
chk "平行移動しても影の面積は同じ" "$3" 4.2426406871192848 1e-9

# ---- ③ **冪等** — 一度落としたものをもう一度落としても動かない ----
OUT=$(run p3 "$MODEL
print(\"VAL\", area(project_flatten(project_flatten(rotate(U,\"x\",45)))));")
chk "冪等 (2 回当てても同じ)" "$(echo "$OUT" | sed -n 's/^VAL //p' | head -1)" \
	4.2426406871192848 1e-9

# ---- ④ 面積だけでは置き場所を見ていないので **bbox を併せて見る** ----
#   ⚠ #3526 で 3 回踏んだ型。45 度に倒すと y が cos45 倍になる: [0,2] x [0,3*cos45]
#     (rect は y∈[0,3] にあるので y' = y*cos45 ⇒ **正の側**のまま縮む)
#   ⚠ occt の bbox は形の tolerance の分だけ膨らむ (素の rect でも ±1e-7) ので許容を緩める。
BT=1e-9
[ "$KIND" = "oc" ] && BT=1e-6
OUT=$(run p4 "$MODEL
var F = project_flatten(rotate(U,\"x\",45));
print(\"VAL\", bbox(F)[0][0]); print(\"VAL\", bbox(F)[0][1]);
print(\"VAL\", bbox(F)[1][0]); print(\"VAL\", bbox(F)[1][1]);")
_SPLIT_=$(echo "$OUT" | sed -n 's/^VAL //p')
set -f
set -- $_SPLIT_
set +f
chk "45 度の bbox xmin" "$1" 0.0                 "$BT"
chk "45 度の bbox ymin" "$2" 0.0                 "$BT"
chk "45 度の bbox xmax" "$3" 2.0                 "$BT"
chk "45 度の bbox ymax" "$4" 2.1213203435596424  "$BT"

# ---- ⑤ ★★ **経路非依存** — ここがこの検査の芯 (上の見出しを参照) ----
#   rotate("x",180) と rotate("x",90) x 2 は world では同じ図形。案 A なら割れる。
OUT=$(run p5 "$MODEL
var A = project_flatten(rotate(U,\"x\",180));
var B = project_flatten(rotate(rotate(U,\"x\",90),\"x\",90));
print(\"VAL\", bbox(A)[0][0]); print(\"VAL\", bbox(A)[0][1]);
print(\"VAL\", bbox(A)[1][0]); print(\"VAL\", bbox(A)[1][1]);
print(\"VAL\", bbox(B)[0][0]); print(\"VAL\", bbox(B)[0][1]);
print(\"VAL\", bbox(B)[1][0]); print(\"VAL\", bbox(B)[1][1]);")
_SPLIT_=$(echo "$OUT" | sed -n 's/^VAL //p')
set -f
set -- $_SPLIT_
set +f
i=0
for k in 1 2 3 4; do
	a=$(eval echo \$$k); b=$(eval echo \$$((k+4)))
	awk -v a="$a" -v b="$b" 'BEGIN{ if(a==""||b==""){exit 1} d=a-b; if(d<0)d=-d; exit !(d < 1e-9) }' \
		|| { echo "FAIL: 経路依存 — 180 度と 90 度 x2 で bbox の成分 $k が違う ($a / $b)"; fail=1; }
	n=$((n+1))
done
#   ★ 値そのものも見る (両方が同じだけ壊れていても通ってしまうため)
chk "180 度の bbox ymin" "$2" -3.0 "$BT"
chk "180 度の bbox ymax" "$4"  0.0 "$BT"

# ---- ⑥ 平面が world +Z を含むと影が線に潰れる ⇒ **明示エラー** ----
#   ⚠ 黙って面積 0 を返さないこと。★ extrude の既存検査と同じ判定式 (法線の z 成分)。
OUT=$(run p6 "$MODEL
print(\"VAL\", area(project_flatten(rotate(U,\"x\",90))));")
echo "$OUT" | grep -q "shadow on z=0 collapses to a line" \
	|| { echo "FAIL: 平面が +Z を含むのに明示エラーにならない"; echo "$OUT"; fail=1; }
n=$((n+1))
#   ★ **負の対照** — 89 度なら通る (90 度だけを特別扱いしていないことの裏づけ)
OUT=$(run p6b "$MODEL
print(\"VAL\", area(project_flatten(rotate(U,\"x\",89))));")
chk "89 度は通る (影の面積)" "$(echo "$OUT" | sed -n 's/^VAL //p' | head -1)" \
	0.10471443862370158 1e-6

# ---- ⑦ 出力の **型** ----
#   ★ メッシュ系は cross2d へ落ちる = SVG へ書けるようになる (#3533 規約④ に対する唯一の出口)。
#   ⚠ occt は #3533 で「z=0 の簡易表現」が無くなったので **型が変わらない** (oc-face3d のまま)。
if [ "$KIND" = "oc" ]; then
	echo "  note: occt は project_flatten で型が変わらない (oc-face3d のまま・#3533)"
else
	#   ⚠⚠ **カーネル名で振り分けない** — 「SVG を書けるか」を *素の 2D* で先に測る
	#     (manifold は 2D の SVG/DXF を そもそも持たない。2026-09-15 に実測)。
	#     ★ これが無いと「書けないカーネル」で毎回赤くなるか、逆に skip を広く書いて
	#       **書けるはずのカーネルの退行を見逃す**。負の対照を先に取る。
	rm -f "$D-p7a.svg"
	OUT=$(run p7a "$MODEL export(\"$D-p7a.svg\", U, \"mm\");")
	if [ ! -f "$D-p7a.svg" ]; then
		echo "  skip SVG ($SO は 2D の SVG を持たない — 素の rect でも書けない)"
	else
		rm -f "$D-p7.svg"
		OUT=$(run p7 "$MODEL export(\"$D-p7.svg\", project_flatten(rotate(U,\"x\",45)), \"mm\");")
		[ -f "$D-p7.svg" ] \
			|| { echo "FAIL: 素の 2D は SVG に書けるのに、落としたあとが書けない"; echo "$OUT"; fail=1; }
		n=$((n+1))
		#   ★ **負の対照** — 落とす前 (face3d) は #3533 規約④ で断られること。
		#     これが通ってしまうなら、そもそも project_flatten が要らないことになる。
		rm -f "$D-p7b.svg"
		OUT=$(run p7b "$MODEL export(\"$D-p7b.svg\", rotate(U,\"x\",45), \"mm\");")
		[ ! -f "$D-p7b.svg" ] \
			|| { echo "FAIL: 傾いた face3d が SVG に書けてしまう (#3533 規約④ が効いていない)"; fail=1; }
		n=$((n+1))
	fi
	rm -f "$D-p7.svg" "$D-p7a.svg" "$D-p7b.svg"
fi

rm -rf "$D-p1" "$D-p2" "$D-p3" "$D-p4" "$D-p5" "$D-p6" "$D-p6b" "$D-p7" "$D-p7a" "$D-p7b"
[ "$fail" = "0" ] || { echo "FAIL: 上記のとおり"; exit 1; }
echo "PROJFLAT-OK $n checks ($SO)"
