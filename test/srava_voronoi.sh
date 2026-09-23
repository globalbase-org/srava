#!/bin/sh
# Voronoi 図 (#3525) の回帰。cgal・2D。
#
# $1 = srava 実行体 / env: SRAVA_AGENT, SRAVA_CACHE_DIR。
#
# ---- 何を検定しているか ------------------------------------------------------------
# ① 閉形式    サイトを (i+0.5, j+0.5) に置き、箱を [0,n]x[0,n] にすると答えが手で書ける:
#             セルは 1x1 の正方形なので area == 1 ・**centroid == サイトそのもの** ・和 == n*n。
#             ★ 箱の位置が要点 — 格子の外側に半間隔ぶん取らないと境界セルが非対称に切れる。
#             ⚠ centroid == サイト を **一般の約束にしてはいけない**。成り立つのは図が CVT
#               (重心 Voronoi 分割) のときだけで、正方格子がたまたまその不動点。
#               一般の配置では重心はサイトからずれ、*そのずれを消す反復が Lloyd 法*。
# ② 定義そのもの  一般の配置では ① が使えないので、**定義から出る不等式**で見る:
#             セルは必ず凸なので重心はセルの内側にある ⇒ *セル i の重心はどのサイトよりも
#             pts[i] に近い*。★ これは **索引の約束の検定**でもある — セルが 1 つでも
#             ずれていれば落ちる (実測: 索引を 1 つずらすと 40/40 が違反になる)。
# ③ 片の索引  nparts == サイト数 ・ part(v,i) が 2D 領域として使える (area/centroid/extrude)。
#             ★ 「nparts = その値が構造として持っている片の数」は 3D で既にそうだった約束
#               (nef = marked volume の数 / cgal = 符号つき体積が正のシェルの数) で、
#               2D をそれに揃えたもの。⇒ セルが互いに接していても N のまま。
# ⑤ delaunay  **voronoi の前段ではない独立した op**。定義から出る検定は「三角形の面積の和 =
#             凸包の面積」。★ これは「片の測度の和 = 全体の測度」(#3527) の検定でもあり、
#             *分け方で答えが変わらない*ことを見ている ⇒ 一致は厳密。
#             ⚠ 三角形の **番号は実装依存** なので「i 番目」は検定しない (#3527)。
# ⑧ 3D        **voronoi の 3D**。⇒ これを素直に入れられることが、voronoi を delaunay から
#             切り離した目的 (#3525 の 5 節)。閉形式は 2D と同じ形。
# ⑨ 相互検定  ★★ サイトが同一平面なら **3D セル = 2D セル x 高さ** (恒等式)。独立に書かれた
#             2 実装 (自前の Sutherland-Hodgman / PMP::clip) を突き合わせる。厳密一致で見る。
# ⑪ 後方互換  part(cg-mesh3d) は **nef が持っていた行**を cgal が priority で引き取る。
#             ⇒ 普通のメッシュで nef と同じ値になること・nef にしか出せない形 (空洞) は
#               明示エラーになることを固定する。
# ⑫ 3D の delaunay  四面体の体積の和 = 凸包の体積 (厳密)。★ 順序つき片リストが入ったことで
#             「四面体は新しい表現クラスが要る」という見送り材料が消えた。
#             ⚠ **同一平面はエラーにしない** — 次元 2 の正しい三角形分割を平たい片として返す。
#               そのかわり **閉じていない値の volume は断る** (PMP::volume は落ちずに嘘を返す)。
# ⑥ 経路依存  centroid / area が **頂点の書き出し順・片への分け方**に依らないこと。
#             ⚠ どちらも #3525 の途中で見つけて直した。同じ図形に 2 つの答えが出る病気
#               (#3529 / #3533 と同型) で、*落ちないので気づけない*。
#             ⚠⚠ 直し方は 2 段だった: ① 厳密なまま積む ② **落とすところで CGAL::exact() を通す**。
#               ②が要るのは EPECK の FT が Lazy_exact_nt で、@to_double@ が *区間近似* を返し
#               正しく丸められないため。①だけだと *積む式の形* でまだ 1〜2 ulp 動く。
#               ⇒ 期待値は **厳密値を外 (有理数) から持ってくる**。2 経路の突き合わせだけでは
#                 「両方が同じだけずれている」を見逃す。
# ④ 明示エラー 重複サイト / 箱の外のサイト / 潰れた箱 / 箱の省略 / 範囲外の索引。
#             ★ どれも *黙って違う答えを返す* 道 — 重複を潰すと以降の索引がずれ、
#               暗黙の箱を使うと箱の大きさで面積が変わる。
LC_ALL=C
export LC_ALL
SRAVA="$1"
D="${SRAVA_CACHE_DIR:?SRAVA_CACHE_DIR not set}"
# ★★ #3522: ハングの番犬 (共通)。詳細は test/srava_hangwatch.sh。
. "$(dirname "$0")/srava_hangwatch.sh"

M='module("cgal.so",{});module("points.so",{});'
run() {   # run <cache-suffix> <source>  → 出力全部
	rm -rf "$D-$1"
	SRAVA_CACHE_DIR="$D-$1" SRAVA_SOURCE="$M
$2" "$SRAVA" 2>&1
}
n=0
chk() {   # chk <name> <got> <expected>
	[ "$2" = "$3" ] || { echo "VORONOI_FAIL: $1 が [$2] (期待 [$3])"; exit 1; }
	n=$((n+1))
}
chk_err() {   # chk_err <name> <出力> <期待する部分文字列>
	case "$2" in
	*"$3"*) n=$((n+1)) ;;
	*) echo "VORONOI_FAIL: $1 がエラーにならない/文言が違う: $2" ; exit 1 ;;
	esac
}

# ---- ① 閉形式: 2x2 の格子 -----------------------------------------------------------
G='points2d([[0.5,0.5],[1.5,0.5],[0.5,1.5],[1.5,1.5]])'
O=$(run g1 "var v = voronoi($G, [[0,0],[2,2]]);
print(\"V\", nparts(v));
print(\"V\", area(v));
print(\"V\", [area(part(v,0)),area(part(v,1)),area(part(v,2)),area(part(v,3))]);
print(\"V\", [centroid(part(v,0)),centroid(part(v,1)),centroid(part(v,2)),centroid(part(v,3))]);")
# ⚠⚠ **値の分割だけ glob を止める** (2026-09-22)。
#   `set -- $(…)` の $( ) は **クォートできない** (単語分割が要る) のでパス名展開に掛かる。
#   比べる値は `[1,1,0]` 形 = glob の **文字クラス**なので、カレント (ビルド dir) に `1` という
#   名前のファイルが 1 つあるだけで `1` に化け、**「値が違う」という、それらしい文言で赤くなる**
#   (2026-09-21 に実際に踏んだ。⚠ PIG_TIMING は *ファイルパス* を取る env なので
#   `PIG_TIMING=1` と打つと `1` というファイルができる)。
#   ★★ **実行と分割を分ける**のが肝。`set -f; set -- $(cmd)` と囲うと -f が $( ) の中まで効き、
#     cmd がシェル関数だと *その中の glob* まで殺す (実測: count_tus → objs_of の
#     `CMakeFiles/*/link.txt` が読めず「全 0 本」になった)。⇒ 先に変数へ取ってから分割する。
_SPLIT_=$(echo "$O" | sed -n 's/^V //p' | tr -d ' ')
set -f
set -- $_SPLIT_
set +f
chk "セルの数 = サイト数" "$1" "4"
chk "面積の和 = 箱の面積" "$2" "4"
chk "各セルの面積"        "$3" "[1,1,1,1]"
# ★★ 索引の約束がここに出る — k 番目のセルの重心が k 番目のサイトそのもの。**厳密に**一致。
chk "各セルの重心 = サイト" "$4" "[[0.5,0.5],[1.5,0.5],[0.5,1.5],[1.5,1.5]]"

# ★ 隅の与え方は問わない (対角の 2 点であればよい)。
O=$(run g2 "print(\"V\", area(voronoi($G, [[2,2],[0,0]])));")
chk "箱の隅は逆順でもよい" "$(echo "$O" | sed -n 's/^V //p' | tr -d ' ')" "4"

# ---- ② 定義そのもの: 一般の配置 -------------------------------------------------------
# ⚠ ここでは閉形式が使えない。セル i の重心が **どのサイトよりも pts[i] に近い** ことだけを見る。
P='[[1.3,0.7],[4.4,2.1],[7.9,1.2],[2.2,5.6],[6.1,4.4],[8.8,6.3],[1.1,8.2],[4.9,7.7],[7.2,9.1],[5.5,5.0]]'
O=$(run r1 "var v = voronoi(points2d($P), [[0,0],[10,10]]);
print(\"V\", nparts(v));
print(\"C\", centroid(part(v,0))); print(\"C\", centroid(part(v,1)));
print(\"C\", centroid(part(v,2))); print(\"C\", centroid(part(v,3)));
print(\"C\", centroid(part(v,4))); print(\"C\", centroid(part(v,5)));
print(\"C\", centroid(part(v,6))); print(\"C\", centroid(part(v,7)));
print(\"C\", centroid(part(v,8))); print(\"C\", centroid(part(v,9)));")
chk "一般の配置でもセル数 = サイト数" "$(echo "$O" | sed -n 's/^V //p' | tr -d ' ')" "10"
BAD=$(echo "$O" | sed -n 's/^C //p' | tr -d ' []' | awk -F, -v P="$P" '
BEGIN{ gsub(/[\[\] ]/,"",P); m=split(P,a,","); for(k=1;k<=m;k+=2){ ns++; sx[ns]=a[k]+0; sy[ns]=a[k+1]+0 } }
{ i=NR; best=0; bd=0;
  for(j=1;j<=ns;j++){ d=(($1)-sx[j])^2+(($2)-sy[j])^2; if(best==0||d<bd){bd=d;best=j} }
  if(best!=i) bad++ }
END{ print bad+0 }')
chk "セル i の重心は pts[i] が最も近い" "$BAD" "0"
# ★ この検定が **本当に効く**ことを、当たる例で示す (索引を 1 つずらすと全件違反になる)。
BADSHIFT=$(echo "$O" | sed -n 's/^C //p' | tr -d ' []' | awk -F, -v P="$P" '
BEGIN{ gsub(/[\[\] ]/,"",P); m=split(P,a,","); for(k=1;k<=m;k+=2){ ns++; sx[ns]=a[k]+0; sy[ns]=a[k+1]+0 } }
{ cx[NR]=$1; cy[NR]=$2; nc=NR }
END{ for(i=1;i<=nc;i++){ s=(i % nc)+1; best=0; bd=0;
       for(j=1;j<=ns;j++){ d=(cx[s]-sx[j])^2+(cy[s]-sy[j])^2; if(best==0||d<bd){bd=d;best=j} }
       if(best!=i) bad++ }
     print bad+0 }')
chk "★ 索引を 1 つずらすと全件違反になる (検定が効いている証拠)" "$BADSHIFT" "10"

# ---- ③ 片は 2D 領域として普通に使える -------------------------------------------------
O=$(run u1 "var v = voronoi($G, [[0,0],[2,2]]);
print(\"V\", nverts(part(v,0)));
print(\"V\", valid(part(v,0)));
print(\"V\", volume(extrude(part(v,0), 3)));
print(\"V\", area(union(part(v,0), part(v,1))));")
_SPLIT_=$(echo "$O" | sed -n 's/^V //p' | tr -d ' ')
set -f
set -- $_SPLIT_
set +f
chk "セルは 4 頂点の正方形" "$1" "4"
chk "セルは valid"          "$2" "1"
chk "セルを押し出せる"      "$3" "3"
chk "セルどうしを合体できる" "$4" "2"

# ---- ④ 明示エラー ---------------------------------------------------------------------
chk_err "重複サイト" \
	"$(run e1 "print(area(voronoi(points2d([[0.5,0.5],[0.5,0.5]]), [[0,0],[2,2]])));")" \
	"are the same point"
# ⚠ -0.0 は 0.0 と等しいので、書き方だけ違う重複もここで捕まる ([[negative-zero-slips-comparison]])。
chk_err "重複サイト (-0.0 と 0.0)" \
	"$(run e1b "print(area(voronoi(points2d([[0,0],[-0.0,0]]), [[-1,-1],[2,2]])));")" \
	"are the same point"
chk_err "箱の外のサイト" \
	"$(run e2 "print(area(voronoi(points2d([[0.5,0.5],[9,9]]), [[0,0],[2,2]])));")" \
	"is outside the clip box"
chk_err "潰れた箱" \
	"$(run e3 "print(area(voronoi(points2d([[0.5,0.5]]), [[0,0],[0,2]])));")" \
	"clip box is degenerate"
# ★ 箱は省略できない (暗黙の箱を使うと箱の大きさで面積が黙って変わる)。
#   ★ #3570 段4: 個数は行の成立条件になったので、文言は「どれも受けない」の列挙形。
chk_err "箱の省略" \
	"$(run e4 "print(area(voronoi(points2d([[0.5,0.5]]))));")" \
	"no candidate takes 1 argument(s)"
chk_err "範囲外の索引" \
	"$(run e5 "print(area(part(voronoi(points2d([[0.5,0.5]]), [[0,0],[2,2]]), 3)));")" \
	"out of range"
# 3D の点群は受けない (sig が 2D だけを宣言している)。
chk_err "3D の点群" \
	"$(run e6 "print(area(voronoi(points3d([[0,0,0],[1,1,1]]), [[0,0],[2,2]])));")" \
	"voronoi"

# ---- ⑤ delaunay: **voronoi の前段ではない独立した op** -------------------------------
# ★ 定義から出る検定: Delaunay 分割は **凸包をちょうど埋める** ⇒ 三角形の面積の和 = 凸包の面積。
#   ⚠ これは「片の測度の和 = 全体の測度」(#3527) の検定でもある — *分け方で答えが変わらない*。
#     ⇒ 一致は **厳密** (#3525 で面積を厳密なまま積むようにしたので tolerance は要らない)。
O=$(run d1 'var p = points2d([[0,0],[2,0],[2,2],[0,2],[1,1]]);
print("V", nparts(delaunay(p)));
print("V", area(delaunay(p)));
print("V", area(hull(p)));
print("V", nverts(part(delaunay(p),0)));')
_SPLIT_=$(echo "$O" | sed -n 's/^V //p' | tr -d ' ')
set -f
set -- $_SPLIT_
set +f
chk "正方形 + 中心 → 三角形 4 枚" "$1" "4"
chk "三角形の面積の和"            "$2" "4"
chk "= 凸包の面積"                "$3" "4"
chk "片は三角形 (3 頂点)"         "$4" "3"
# ★ 一般の配置でも **厳密に**一致すること (ここが浮動小数だと 1e-15 ずれる — 実測した)。
O=$(run d2 'var q = points2d([[0.2,0.1],[3.7,0.4],[3.1,2.9],[0.6,3.3],[1.9,1.5],[2.8,1.1],[1.1,2.4]]);
print("V", area(delaunay(q)));
print("V", area(hull(q)));')
_SPLIT_=$(echo "$O" | sed -n 's/^V //p' | tr -d ' ')
set -f
set -- $_SPLIT_
set +f
chk "一般の配置でも 三角形の和 = 凸包 (厳密)" "$1" "$2"
# ⚠ 退化は明示エラー (次元が 1 に落ちて面が 0 枚 — 黙って空の 2D を返さない)。
chk_err "1 直線上の点" \
	"$(run d3 'print(nparts(delaunay(points2d([[0,0],[1,0],[2,0],[3,0]]))));')" \
	"all on one line"
chk_err "点が 3 つ未満" \
	"$(run d4 'print(nparts(delaunay(points2d([[0,0],[1,0]]))));')" \
	"at least 3 points"

# ---- ⑥ 片の測度の和 = 全体の測度 (#3527) ---------------------------------------------
# ★ 同じ四辺形を「1 枚として」と「三角形 2 枚に分けて」測る。⚠ 期待値は **厳密値を外から
#   持ってくる** (8.545 = 有理数で計算した値)。カーネル同士や 2 つの経路を突き合わせるだけだと
#   *両方が同じだけずれている* ときに気づけない。
O=$(run p1 'var W = polygon([[0.2,0.1],[3.7,0.4],[3.1,2.9],[0.6,3.3]]);
print("V", area(W));
print("V", area(combine(polygon([[0.2,0.1],[3.7,0.4],[3.1,2.9]]),
                        polygon([[0.2,0.1],[3.1,2.9],[0.6,3.3]]))));')
_SPLIT_=$(echo "$O" | sed -n 's/^V //p' | tr -d ' ')
set -f
set -- $_SPLIT_
set +f
chk "四辺形を 1 枚として"     "$1" "8.5449999999999999"
chk "= 三角形 2 枚に分けて"   "$2" "8.5449999999999999"

# ★ voronoi のセルは箱をちょうど覆う ⇒ 面積の和は **厳密に**箱の面積。
#   ⚠ 以前は片ごとに double へ落として足していたので 100.00000000000001 になっていた。
O=$(run s1 'print("V", area(voronoi(points2d([[1.3,0.7],[4.4,2.1],[7.9,1.2],[2.2,5.6],[6.1,4.4],
                                              [8.8,6.3],[1.1,8.2],[4.9,7.7],[7.2,9.1],[5.5,5.0]]),
                                    [[0,0],[10,10]])));')
chk "セルの面積の和 = 箱の面積 (厳密)" "$(echo "$O" | sed -n 's/^V //p' | tr -d ' ')" "100"

# ---- ⑦ centroid は頂点の書き出し順に依らない (#3525 で直した経路依存) -------------------
# ⚠ 以前は各項を double にして /6 してから足しており、**同じ正方形が 2 つの答え**を返していた。
O=$(run c1 "print(\"V\", centroid(polygon([[1,1],[2,1],[2,2],[1,2]])));
print(\"V\", centroid(polygon([[2,1],[2,2],[1,2],[1,1]])));
print(\"V\", centroid(polygon([[2,2],[1,2],[1,1],[2,1]])));
print(\"V\", centroid(polygon([[1,2],[1,1],[2,1],[2,2]])));")
_SPLIT_=$(echo "$O" | sed -n 's/^V //p' | tr -d ' ')
set -f
set -- $_SPLIT_
set +f
chk "重心 (書き出し順 1)" "$1" "[1.5,1.5]"
chk "重心 (書き出し順 2)" "$2" "[1.5,1.5]"
chk "重心 (書き出し順 3)" "$3" "[1.5,1.5]"
chk "重心 (書き出し順 4)" "$4" "[1.5,1.5]"

# ---- ⑧ 3D の voronoi ------------------------------------------------------------------
# ★★ **これを素直に入れられることが、voronoi を delaunay から切り離した目的** (#3525 の 5 節)。
#   危険 (四面体という新しい表現クラス・最悪 O(N^2) のセル数) は delaunay 側に閉じ込められており、
#   切り取りで作る voronoi はその向こう側に居ない。
# ★ 閉形式は 2D と同じ形: サイトを (i+.5, j+.5, k+.5) に置けば セルは 1x1x1 の立方体。
G3='points3d([[0.5,0.5,0.5],[1.5,0.5,0.5],[0.5,1.5,0.5],[1.5,1.5,0.5],
              [0.5,0.5,1.5],[1.5,0.5,1.5],[0.5,1.5,1.5],[1.5,1.5,1.5]])'
O=$(run t1 "var v = voronoi($G3, [[0,0,0],[2,2,2]]);
print(\"V\", nparts(v));
print(\"V\", volume(v));
print(\"V\", [volume(part(v,0)),volume(part(v,3)),volume(part(v,7))]);
print(\"V\", [centroid(part(v,0)),centroid(part(v,7))]);")
_SPLIT_=$(echo "$O" | sed -n 's/^V //p' | tr -d ' ')
set -f
set -- $_SPLIT_
set +f
chk "3D: セルの数 = サイト数"   "$1" "8"
chk "3D: 体積の和 = 箱の体積"   "$2" "8"
chk "3D: 各セルの体積"          "$3" "[1,1,1]"
chk "3D: 各セルの重心 = サイト" "$4" "[[0.5,0.5,0.5],[1.5,1.5,1.5]]"

# ---- ⑨ ★★ 2D 実装と 3D 実装の相互検定 (#3525 の 3 節③) ---------------------------------
# サイトが同一平面にあるとき、3D の距離比較で qz^2 が相殺されるので **3D セル = 2D セル x 高さ**。
# 近似ではなく **恒等式**。⇒ 独立に書かれた 2 つの実装 (2D = 自前の Sutherland-Hodgman /
# 3D = PMP::clip) が同じ答えを出すかを見る。★ しかも *最も壊れやすい退化した経路*を通る。
# ⚠ 一致は **厳密** に見る (tolerance を置くと、片方が壊れかけていても気づけない)。
P2='[[1.3,0.7],[4.4,2.1],[7.9,1.2],[2.2,5.6],[6.1,4.4],[8.8,6.3],[1.1,8.2],[4.9,7.7],[7.2,9.1],[5.5,5.0]]'
P3='[[1.3,0.7,0.5],[4.4,2.1,0.5],[7.9,1.2,0.5],[2.2,5.6,0.5],[6.1,4.4,0.5],[8.8,6.3,0.5],
     [1.1,8.2,0.5],[4.9,7.7,0.5],[7.2,9.1,0.5],[5.5,5.0,0.5]]'
O=$(run x1 "var v2 = voronoi(points2d($P2), [[0,0],[10,10]]);
var v3 = voronoi(points3d($P3), [[0,0,0],[10,10,1]]);
print(\"A\", area(part(v2,0)));   print(\"B\", volume(part(v3,0)));
print(\"A\", area(part(v2,4)));   print(\"B\", volume(part(v3,4)));
print(\"A\", area(part(v2,9)));   print(\"B\", volume(part(v3,9)));
print(\"C\", nparts(v3));         print(\"C\", volume(v3));")
A=$(echo "$O" | sed -n 's/^A //p' | tr -d ' ' | tr '\n' ' ')
B=$(echo "$O" | sed -n 's/^B //p' | tr -d ' ' | tr '\n' ' ')
chk "★ 2D のセルの面積 == 3D のセルの体積 (高さ 1・厳密)" "$A" "$B"
[ -n "$A" ] || { echo "VORONOI_FAIL: 相互検定の値が取れていない"; exit 1; }
_SPLIT_=$(echo "$O" | sed -n 's/^C //p' | tr -d ' ')
set -f
set -- $_SPLIT_
set +f
chk "3D: 一般の配置でもセル数 = サイト数" "$1" "10"
chk "3D: 体積の和 = 箱の体積"             "$2" "100"

# ---- ⑩ 3D の明示エラー ------------------------------------------------------------------
chk_err "3D: 箱の外のサイト" \
	"$(run y1 'print(volume(voronoi(points3d([[0.5,0.5,0.5],[9,9,9]]), [[0,0,0],[2,2,2]])));')" \
	"is outside the clip box"
chk_err "3D: 重複サイト" \
	"$(run y2 'print(volume(voronoi(points3d([[0.5,0.5,0.5],[0.5,0.5,0.5]]), [[0,0,0],[2,2,2]])));')" \
	"are the same point"
chk_err "3D: 箱の隅の次元が足りない" \
	"$(run y3 'print(volume(voronoi(points3d([[0.5,0.5,0.5]]), [[0,0],[2,2]])));')" \
	"the point cloud is 3D"

# ---- ⑪ part(cg-mesh3d) の後方互換 ------------------------------------------------------
# ⚠⚠ この行は **nef が既に持っていた** ("(cg-mesh3d)->nfb")。priority が cgal 20 > nef 5 なので
#   routing がこちらへ移る ⇒ *片リストを持たない普通のメッシュで nef と同じ答えになる*ことを固定する。
#   ★ 実測した基準値 (nef が返していた値): box(2,3,4) → 24 ・ 離れた 2 箱 → 1 と 1。
O=$(run z1 'print("V", volume(part(box(2,3,4),0)));
print("V", nparts(combine(box(1,1,1), translate(box(1,1,1),[5,0,0]))));
print("V", [volume(part(combine(box(1,1,1), translate(box(1,1,1),[5,0,0])),0)),
            volume(part(combine(box(1,1,1), translate(box(1,1,1),[5,0,0])),1))]);')
_SPLIT_=$(echo "$O" | sed -n 's/^V //p' | tr -d ' ')
set -f
set -- $_SPLIT_
set +f
chk "普通のメッシュの part (nef と同じ値)" "$1" "24"
chk "離れた 2 箱の nparts"                 "$2" "2"
chk "離れた 2 箱の part"                   "$3" "[1,1]"
# ★★ 2026-09-17 に振る舞いが変わった。以前は「空洞は面の連結成分で分けられない」と
#   **断って**いたが、それは *実装の都合* (PMP::connected_components しか見ていなかった) で、
#   CGAL 本体は volume_connected_components で塊を直接くれる。⇒ 解けるようにした。
# ★ 中空の箱は **塊 1 個**で、その塊の境界は 外殻と空洞の **両方**。
#   ⇒ part(m,0) は **m と同じもの** (56)。⚠ 殻 1 枚 (64) を返したら *体積が黙って増える*。
O=$(run z2 'print("V", nparts(difference(box(4,4,4), translate(box(2,2,2),[1,1,1]))));
print("V", volume(part(difference(box(4,4,4), translate(box(2,2,2),[1,1,1])),0)));')
_SPLIT_=$(echo "$O" | sed -n 's/^V //p' | tr -d ' ')
set -f
set -- $_SPLIT_
set +f
chk "空洞つきの nparts"                    "$1" "1"
chk "空洞つきの part (= 元と同じ 56)"      "$2" "56"

# ---- ⑫ 3D の delaunay -----------------------------------------------------------------
# ★ 「四面体は srava に無い新しい表現クラスが要る」という見送り材料は **順序つき片リストで消えた**
#   ⇒ 四面体は 1 つのメッシュの片として並べるだけで済む (#3525 の 5 節の材料が 1 つ減った)。
# ★ 定義から出る検定は 2D と同じ形: **四面体の体積の和 = 凸包の体積** (厳密)。
O=$(run w1 'print("V", nparts(delaunay(points3d([[0,0,0],[1,0,0],[0,1,0],[0,0,1]]))));
print("V", volume(delaunay(points3d([[0,0,0],[1,0,0],[0,1,0],[0,0,1]]))));
print("V", nfaces(part(delaunay(points3d([[0,0,0],[1,0,0],[0,1,0],[0,0,1]])),0)));
var q = points3d([[0,0,0],[2,0,0],[2,2,0],[0,2,0],[0,0,2],[2,0,2],[2,2,2],[0,2,2],[1,1,1]]);
print("V", volume(delaunay(q)));
print("V", volume(hull(q)));')
_SPLIT_=$(echo "$O" | sed -n 's/^V //p' | tr -d ' ')
set -f
set -- $_SPLIT_
set +f
chk "四面体 1 つ"                 "$1" "1"
chk "四面体の体積"                "$2" "0.16666666666666666"
chk "片は四面体 (4 面)"           "$3" "4"
chk "四面体の体積の和"            "$4" "8"
chk "= 凸包の体積 (厳密)"         "$5" "8"

# ⚠ **同一平面はエラーにしない** (#3525 が明記している判断)。次元が 2 に落ちても
#   *正しい三角形分割が存在する* ので、それを平たい片として返す。
#   ⚠ 「四面体が 0 個だから失敗」と読むと正しく構築された値を誤診する ⇒ dimension() を先に見る。
O=$(run w2 'var f = delaunay(points3d([[0,0,5],[2,0,5],[2,2,5],[0,2,5],[1,1,5]]));
print("V", nparts(f));
print("V", area(f));
print("V", nfaces(part(f,0)));
print("V", valid(part(f,0)));')
_SPLIT_=$(echo "$O" | sed -n 's/^V //p' | tr -d ' ')
set -f
set -- $_SPLIT_
set +f
chk "同一平面: 三角形 4 枚"       "$1" "4"
chk "同一平面: 面積は正しく出る"  "$2" "4"
chk "同一平面: 片は三角形 1 枚"   "$3" "1"
chk "同一平面: 片は閉じていない"  "$4" "0"
# ⚠⚠ **閉じていない値に体積は無い**。PMP::volume は落ちずに数を返すので、門が無いと
#   *意味の無い値* (実測 6.666…) が黙って下流へ流れる ⇒ genus と同じ門を置いてある。
chk_err "同一平面の体積は断る" \
	"$(run w3 'print(volume(delaunay(points3d([[0,0,5],[2,0,5],[2,2,5],[0,2,5],[1,1,5]]))));')" \
	"is not closed"
# ★ ただし **空集合は 0** (ブールの結果が空になるのは普通のこと) — 門に掛けない。
chk "空集合の体積は 0" \
	"$(run w4 'print("V", volume(intersection(box(1,1,1), translate(box(1,1,1),[9,0,0]))));' \
	   | sed -n 's/^V //p' | tr -d ' ')" "0"
chk_err "3D: 1 直線上の点" \
	"$(run w5 'print(nparts(delaunay(points3d([[0,0,0],[1,0,0],[2,0,0],[3,0,0]]))));')" \
	"all on one line"

echo "VORONOI_OK ($n checks)"
