#!/bin/sh
# ★★★ #3544 段 3: **occt の 2D 図面の入出力** (DXF / SVG)。
#
# $1 = srava 実行体 / $2 = occt の .so / $3 = cgal の .so (対照用・無ければ対照だけ飛ばす)
# $4 = occt_mf の .so / $5 = manifold の .so (⑨の経路の検査用・無ければ⑨だけ飛ばす)
# env: SRAVA_AGENT, SRAVA_CACHE_DIR。
#
# ---- 何を固定するか ------------------------------------------------------------------
# ★★ 受け入れ条件は「**円を書いて読み戻すと円のまま**」(#3544 の計画・段 3)。
#   ⚠ これは *折れ線に落ちていない* ことの検査なので、**折れ線に落ちた場合の値**を
#     並べて見せないと「1 本でした」が何を意味するのか決まらない。
#     ⇒ 同じ円を cgal の書き手 (折れ線しか書けない) で出して **64 本**になることを
#       陽性対照として並べる。1 対 64 の差がこの段の成果そのもの。
#
# ★ 語彙は書き手と読み手で 1:1 (LINE / CIRCLE / ARC / ELLIPSE / SPLINE / LWPOLYLINE /
#   POLYLINE)。⚠ 片方だけ広げると往復で黙って欠ける ⇒ 4 種すべてを往復させる。
SRAVA="$1"
SO="${2:?occt module .so not given}"
CGSO="${3:-}"
OCMSO="${4:-}"
MFSO="${5:-}"
D="${SRAVA_CACHE_DIR:?SRAVA_CACHE_DIR not set}"
# ★★ #3522: ハングの番犬 (共通・常時 ON)。詳細は test/srava_hangwatch.sh。
. "$(dirname "$0")/srava_hangwatch.sh"

W="$D-files"
rm -rf "$W"; mkdir -p "$W"
fails=0
skips=""
run() {   # run <cache-suffix> <source>
	rm -rf "$D-$1"
	SRAVA_CACHE_DIR="$D-$1" SRAVA_SOURCE="module(\"$SO\",{priority:99});
$2" "$SRAVA" 2>&1
}
run2() {  # run2 <cache-suffix> <source>   occt + cgal を両方積む (対照用)
	rm -rf "$D-$1"
	SRAVA_CACHE_DIR="$D-$1" SRAVA_SOURCE="module(\"$SO\",{priority:99});
module(\"$CGSO\");
$2" "$SRAVA" 2>&1
}
run4() {  # run4 <cache-suffix> <source>   occt + occt_mf + manifold + cgal を積む (⑨用)
	rm -rf "$D-$1"
	SRAVA_CACHE_DIR="$D-$1" SRAVA_SOURCE="module(\"$SO\",{priority:99});
module(\"$OCMSO\");
module(\"$MFSO\");
module(\"$CGSO\");
$2" "$SRAVA" 2>&1
}
vals() { echo "$1" | sed -n 's/^V //p'; }
chks() {
	if [ "$2" = "$3" ]; then echo "  ok  $1"
	else echo "FAIL: $1 が $2 (期待 $3)"; fails=$((fails+1)); fi
}
chkin() {
	case "$2" in
	*"$3"*) echo "  ok  $1" ;;
	*) echo "FAIL: $1 に \"$3\" が無い: $2"; fails=$((fails+1)) ;;
	esac
}
# ★ 座標は %.12g で書くので **bit 一致では比べない** (往復で下位桁が落ちる)。
#   ⇒ 数として 1e-9 で比べる。[[negative-zero-slips-comparison]] と同じ用心。
chknum() {
	ok=$(echo "$2|$3" | tr -d '[]' | awk -F'|' '{
		na = split($1, a, ","); nb = split($2, b, ",")
		if ( na != nb || na == 0 ) { print 0; exit }
		for ( i = 1 ; i <= na ; ++i ) { d = a[i] - b[i]; if ( d < 0 ) d = -d; if ( d > 1e-9 ) { print 0; exit } }
		print 1 }')
	if [ "$ok" = "1" ]; then echo "  ok  $1"
	else echo "FAIL: $1 が $2 (期待 $3)"; fails=$((fails+1)); fi
}

# ---- ①★★ 円は **円のまま** 往復する ---------------------------------------------------
#   球の陰線処理 = 半径 1.5 の円 1 本。DXF では CIRCLE 実体 1 つになる。
O=$(run d1 "var S = hlr(sphere(1.5),[0,0,-1],[0,1,0]);
export(\"$W/sphere.dxf\", S, \"mm\");
print(\"V\", nedges(import(\"$W/sphere.dxf\")));
print(\"V\", bbox(import(\"$W/sphere.dxf\")));")
# ⚠⚠ **値の分割だけ glob を止める** (2026-09-22)。
#   `set -- $(…)` の $( ) は **クォートできない** (単語分割が要る) のでパス名展開に掛かる。
#   比べる値は `[1,1,0]` 形 = glob の **文字クラス**なので、カレント (ビルド dir) に `1` という
#   名前のファイルが 1 つあるだけで `1` に化け、**「値が違う」という、それらしい文言で赤くなる**
#   (2026-09-21 に実際に踏んだ。⚠ PIG_TIMING は *ファイルパス* を取る env なので
#   `PIG_TIMING=1` と打つと `1` というファイルができる)。
#   ★★ **実行と分割を分ける**のが肝。`set -f; set -- $(cmd)` と囲うと -f が $( ) の中まで効き、
#     cmd がシェル関数だと *その中の glob* まで殺す (実測: count_tus → objs_of の
#     `CMakeFiles/*/link.txt` が読めず「全 0 本」になった)。⇒ 先に変数へ取ってから分割する。
_SPLIT_=$(vals "$O")
set -f
set -- $_SPLIT_
set +f
chks   "①★ 円を書いて読み戻すと **1 本のまま**" "$1" "1"
chknum "① 半径 1.5 の円のまま"                  "$2" "[[-1.5,-1.5],[1.5,1.5]]"
chkin  "① DXF に CIRCLE 実体が在る" "$(cat "$W/sphere.dxf")" "CIRCLE"

# ---- ②★ 陽性対照 — **わざと折れ線へ落とすと差が出る** ----------------------------------
#   ⚠ ①の「1 本」は、折れ線に落ちたときの値を知らないと意味が決まらない。
#     cgal の書き手は折れ線しか書けないので、同じ円が 64 本になる。
if [ -z "$CGSO" ]; then
	skips="$skips ②陽性対照(cgal.so 無し)"
else
	O=$(run2 d2 "export(\"$W/cg_circ.dxf\", \"cgal\"::circle(1,64), \"mm\");
print(\"V\", nedges(import(\"$W/cg_circ.dxf\")));")
	chks "②★ 同じ円を折れ線で書くと 64 本になる (対照)" "$(vals "$O")" "64"
fi

# ---- ③ 4 種の曲線がすべて往復する -------------------------------------------------------
#   ★ 実測で出るのは Line / Circle / Ellipse / BSpline の 4 種だけ (#3544 段 2 の実測)。
#     ⇒ 立方体 = LINE ・ 斜めの円柱 = ELLIPSE + LINE ・ トーラス = SPLINE。
O=$(run d3 "var B = hlr(box(2,2,2),[-1,-1,-1]);
export(\"$W/cube.dxf\", B, \"mm\");
print(\"V\", nedges(import(\"$W/cube.dxf\")));
var C = hlr(cylinder(1,3),[-1,-1,-1],[1,-1,0]);
export(\"$W/cyl.dxf\", C, \"mm\");
print(\"V\", nedges(C));
print(\"V\", nedges(import(\"$W/cyl.dxf\")));
print(\"V\", bbox(C));
print(\"V\", bbox(import(\"$W/cyl.dxf\")));
var T = hlr(torus(3,1),[-1,-1,-1],[1,-1,0]);
export(\"$W/torus.dxf\", T, \"mm\");
print(\"V\", nedges(T));
print(\"V\", nedges(import(\"$W/torus.dxf\")));
print(\"V\", bbox(T));
print(\"V\", bbox(import(\"$W/torus.dxf\")));")
_SPLIT_=$(vals "$O")
set -f
set -- $_SPLIT_
set +f
chks   "③ 立方体 (LINE) は 9 本のまま" "$1" "9"
chks   "③ 円柱 (ELLIPSE + LINE) は本数が変わらない" "$3" "$2"
chknum "③ 円柱の図面は往復しても同じ大きさ"        "$5" "$4"
chks   "③ トーラス (SPLINE) は本数が変わらない"    "$7" "$6"
chknum "③ トーラスの図面は往復しても同じ大きさ"    "$9" "$8"
chkin  "③ 円柱の DXF に ELLIPSE 実体が在る" "$(cat "$W/cyl.dxf")" "ELLIPSE"
chkin  "③ トーラスの DXF に SPLINE 実体が在る" "$(cat "$W/torus.dxf")" "SPLINE"

# ---- ④ 2D 領域 (面) も書ける ------------------------------------------------------------
#   ⚠ 2026-09-17 まで occt の 2D は **1 形式も書けなかった** (export が 3D しか d_cast して
#     いなかった)。⇒ 面を持つ 2D でも円が円のまま出ることを見る。
O=$(run d4 "export(\"$W/circ2d.dxf\", circle(1), \"mm\");
print(\"V\", nedges(import(\"$W/circ2d.dxf\")));
export(\"$W/rect2d.dxf\", rect(2,3), \"mm\");
print(\"V\", nedges(import(\"$W/rect2d.dxf\")));")
_SPLIT_=$(vals "$O")
set -f
set -- $_SPLIT_
set +f
chks "④ 2D の円は 1 本で書ける" "$1" "1"
chks "④ 2D の矩形は 4 本"       "$2" "4"

# ---- ⑤ SVG も書ける (⚠ B-spline は折れ線になる — 形式の限界) ---------------------------
#   ★ 円弧は SVG の A で厳密に書ける。⇒ 円は <path ... A ...> になり polyline にならない。
O=$(run d5 "export(\"$W/sphere.svg\", hlr(sphere(1.5),[0,0,-1],[0,1,0]), \"mm\");
print(\"V\", 1);")
chkin "⑤ SVG の円は円弧 (A) で書かれる"       "$(cat "$W/sphere.svg")" " A "
chkin "⑤ SVG の円は polyline に落ちていない"  "$(cat "$W/sphere.svg" | tr -d '\n')" "path"
chkin "⑤ 単位を渡すと物理サイズが付く"        "$(cat "$W/sphere.svg")" "width=\"3mm\""

# ---- ⑥ 断るときは **理由を言う** ---------------------------------------------------------
#   ★ 図面にできない 2D (z=0 の外) は断る。⚠ 「知らない拡張子」と区別がつく文言であること。
O=$(run d6 "export(\"$W/tilt.dxf\", rotate(rect(2,3),\"x\",90), \"mm\");")
chkin "⑥ 平面の外の 2D は理由つきで断る" "$O" "not on the z=0 plane"
chkin "⑥ 代わりにどうするかまで言う"     "$O" "project_flatten"
#   ★ 置き場所を持つ .dxf (OCS) は **読まずに断る** — 黙って z=0 へ潰さない。
if [ -z "$CGSO" ]; then
	skips="$skips ⑥OCS(cgal.so 無し)"
else
	O=$(run2 d6b "export(\"$W/cg_tilt.dxf\", \"cgal\"::rotate(\"cgal\"::rect(2,3),\"x\",90), \"mm\");
print(\"V\", nedges(import(\"$W/cg_tilt.dxf\")));")
	chkin "⑥ 置かれた .dxf は理由つきで断る" "$O" "z=0 plane only"
fi

# ---- ⑦ 単位 — DXF は $INSUNITS を **書く**が読みでは使わない -----------------------------
#   ⚠ 単位で座標を換算する約束を片方のカーネルだけが持つと、同じ .dxf が読み手次第で
#     別の大きさになる。⇒ cgal に揃えて「書くが読まない」。
chkin  "⑦ INSUNITS を書く (mm = 4)" "$(cat "$W/sphere.dxf")" "\$INSUNITS"
O=$(run d7 "export(\"$W/u_in.dxf\", circle(1), \"in\");
print(\"V\", bbox(import(\"$W/u_in.dxf\")));")
chknum "⑦ 単位を変えても座標は動かない (読みで換算しない)" "$(vals "$O")" "[[-1,-1],[1,1]]"

# ---- ⑧ 旧 POLYLINE (VERTEX 並び) も読める ------------------------------------------------
#   ⚠ この語彙は **こちらの書き手が出さない**ので、往復では 1 度も通らない。
#     「読める」と申告しているのに当たる例が無い状態を作らない ⇒ 手書きの固定ファイルで見る。
#     [[zero-claims-need-a-hit]] と同じ理由。
#   ★ 罠: 「code 0 は実体の切れ目」と素直に書くと、POLYLINE が **頂点 0 個で確定**して
#     黙って消える (VERTEX が別の実体として来るため)。⇒ 三角形 = 3 本が出れば通っている。
cat > "$W/old.dxf" <<'DXF'
0
SECTION
2
ENTITIES
0
POLYLINE
8
0
70
1
0
VERTEX
8
0
10
0
20
0
0
VERTEX
8
0
10
2
20
0
0
VERTEX
8
0
10
0
20
1
0
SEQEND
0
ENDSEC
0
EOF
DXF
O=$(run d8 "print(\"V\", nedges(import(\"$W/old.dxf\")));
print(\"V\", bbox(import(\"$W/old.dxf\")));")
_SPLIT_=$(vals "$O")
set -f
set -- $_SPLIT_
set +f
chks   "⑧ 旧 POLYLINE + VERTEX は 3 本になる (閉じた三角形)" "$1" "3"
chknum "⑧ 旧 POLYLINE の座標が読めている"                    "$2" "[[0,0],[2,1]]"

# ---- ⑨★★ 「円が cgal で欲しいなら occt を通す」道が **本当に通る** -----------------------
#   ★★ ひさ判断 (2026-09-17 ・ #3551): cgal の読み手は曲線を折れ線へ落とさない。
#     「折れ線の世界の cgal が、そうじゃない入力にこだわるのは変。円が cgal で欲しいならば、
#       occt を通して cgal へ持っていくのが正解」
#     ⇒ *断る* のが正しいのなら、**断った先の道が通っていること**まで見ないと片手落ち。
#   ★ 刻み方の約束は polygonize(2d, defl) が既に持っている ⇒ 読み手側で 2 つ目を決めない。
#   ⚠ 2026-09-17 まで **この道は通らなかった**: DXF から読んだ図面は面 0 枚なので
#     polygonize が「曲面に載っている」と *事実と違う* 理由で断っていた。
if [ -z "$OCMSO" ] || [ -z "$MFSO" ] || [ -z "$CGSO" ]; then
	skips="$skips ⑨occt→cgal の道"
else
	O=$(run4 d9 "export(\"$W/c9.dxf\", circle(1), \"mm\");
print(\"V\", area(polygonize(circle(1), 0.001)));
print(\"V\", area(polygonize(import(\"$W/c9.dxf\"), 0.001)));
print(\"V\", area(cast(\"cg-cross2d\", polygonize(import(\"$W/c9.dxf\"), 0.001))));")
	_SPLIT_=$(vals "$O")
	set -f
	set -- $_SPLIT_
	set +f
	chks "⑨★ DXF から読んだ円が polygonize を通る (元と同じ値)" "$2" "$1"
	chks "⑨★ そのまま cgal まで届く"                            "$3" "$2"
	#   ⚠ 開いた線だけの図面は **断る** — mf-cross2d に線の置き場所が無い (既に在る穴)。
	#     ★ 黙って落とすと *図面が欠けた* ように見えるので、理由を言って断る。
	O=$(run4 d9b "print(\"V\", area(polygonize(hlr(box(2,2,2),[-1,-1,-1]), 0.001)));")
	chkin "⑨ 開いた線だけの図面は理由つきで断る" "$O" "can hold regions but not lines"
	#   ★ 閉じた輪 (球の輪郭) は領域になる — *明示的に polygonize を書いたときだけ*。
	#     ⚠ 図面そのものは線のまま (area(hlr(...)) は 0)。
	O=$(run4 d9c "print(\"V\", area(hlr(sphere(1.5),[0,0,-1],[0,1,0])));
print(\"V\", area(polygonize(hlr(sphere(1.5),[0,0,-1],[0,1,0]), 0.001)));")
	_SPLIT_=$(vals "$O")
	set -f
	set -- $_SPLIT_
	set +f
	chks   "⑨ 図面そのものは線のまま (面積 0)" "$1" "0"
	chknum "⑨ polygonize と書いたときだけ領域になる (≒ πr²)" "$2" "7.06545909630680"
fi

# ---- ⑩★★ #3551: cgal は **曲線を黙って落とさず断る** ・ 案内どおりにすれば通る ------------
#   ★★ ひさ判断 (案 z)。折れ線の世界の cgal が曲線に合わせるのではなく、断って
#     「occt を通せ」と言う。⇒ **断り方** と **その案内が本当に通ること** を対で見る。
#   ⚠ 当たる例は「混ざったとき」— 曲線しか無いファイルは元から読めず落ちていたので、
#     *折れ線と曲線が混ざったファイル* でないと黙る経路に入らない。
if [ -z "$CGSO" ]; then
	skips="$skips ⑩cgal の断り方"
else
	cat > "$W/mix.dxf" <<'DXF'
0
SECTION
2
ENTITIES
0
LWPOLYLINE
8
0
90
4
70
1
10
0
20
0
10
10
20
0
10
10
20
10
10
0
20
10
0
CIRCLE
8
0
10
30
20
5
30
0
40
2
0
ENDSEC
0
EOF
DXF
	O=$(run2 d10 "print(\"V\", area(\"cgal\"::import(\"$W/mix.dxf\")));")
	chkin "⑩★ 円が混ざった DXF を cgal は断る (黙って正方形だけ返さない)" "$O" "cgal cannot read"
	chkin "⑩ 断り方に **次の一手** が書いてある"                          "$O" "polygonize"
	#   ⚠ 幾何でない実体 (TEXT 等) は **引き続き無視**する — そこまで断ると第三者の .dxf が
	#     ほとんど読めなくなる。★ 断るのは「図形の一部なのに落ちるもの」だけ。
	cat > "$W/withtext.dxf" <<'DXF'
0
SECTION
2
ENTITIES
0
TEXT
8
0
10
1
20
1
40
2
1
hello
0
LWPOLYLINE
8
0
90
4
70
1
10
0
20
0
10
2
20
0
10
2
20
3
10
0
20
3
0
ENDSEC
0
EOF
DXF
	O=$(run2 d10b "print(\"V\", area(\"cgal\"::import(\"$W/withtext.dxf\")));")
	chks "⑩ TEXT は無視して読める (幾何でない実体は断らない)" "$(vals "$O")" "6"
	#   ★★ ガイド (開いた線) の往復 — ⚠ 直す前は **開いた線が閉じた領域に化けて面積が増えて**
	#     いた (16 → 20.5)。落ちるのではなく *違う答え* になるので、こちらのほうが悪い。
	O=$(run2 d10c "var G = \"cgal\"::combine(\"cgal\"::rect(4,4), \"cgal\"::line([[0,0],[3,3],[1,4]]));
export(\"$W/g.dxf\", G, \"mm\");
export(\"$W/g.svg\", G, \"mm\");
print(\"V\", area(G));
print(\"V\", nverts(G));
print(\"V\", area(\"cgal\"::import(\"$W/g.dxf\")));
print(\"V\", nverts(\"cgal\"::import(\"$W/g.dxf\")));
print(\"V\", area(\"cgal\"::import(\"$W/g.svg\")));
print(\"V\", nverts(\"cgal\"::import(\"$W/g.svg\")));")
	_SPLIT_=$(vals "$O")
	set -f
	set -- $_SPLIT_
	set +f
	chks "⑩★ DXF: ガイドが領域に化けない (面積が変わらない)" "$3" "$1"
	chks "⑩ DXF: ガイドの点が残る"                           "$4" "$2"
	chks "⑩★ SVG: ガイドが消えない (面積)"                   "$5" "$1"
	chks "⑩ SVG: ガイドの点が残る"                           "$6" "$2"
fi

if [ -n "$skips" ]; then echo "  ⚠ 対照が積めないので飛ばした:$skips"; fi
if [ "$fails" = "0" ]; then echo "OCCT-DRAWING-OK ($SO)$( [ -n "$skips" ] && echo " ⚠ skipped:$skips" )"
else echo "OCCT-DRAWING-FAIL ($fails fail)"; fi
exit "$fails"
