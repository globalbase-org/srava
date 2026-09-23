#!/bin/sh
# 点群型 (pt-cloud2d / pt-cloud3d) の回帰 (#3528)。
#
# $1 = srava 実行体 / env: SRAVA_AGENT, SRAVA_CACHE_DIR。
#
# ---- 何を検定しているか ------------------------------------------------------------
# ① 閉形式    格子 n×n の点群 → nverts == n² / centroid == 格子の中心 / bbox == 格子の範囲。
#             ★ 値が手で書けるので、往復やキャッシュを疑う前に「作れているか」だけを切り分けられる。
# ② 往復      points3d(...) → export(".xyz") → import → 3 つの計測が一致。
# ③ 法線の往復 法線つきで書いて読み戻し、**もう一度書いたファイルが 1 バイト違わない**こと。
#             ★ 点群の法線を覗く op はまだ無いので、往復させて **ファイルで**見る。
#               A == B なら「法線の値」と「法線ありの印」が cache を越えて保たれている。
#               ⚠ ここが効かないと、法線を落とした点群が Poisson へ渡って静かに嘘の形になる。
# ④ 規模      10 万点を import して nverts が返る。★ これが本題 — inline 値配列の道は
#             O(N^1.9) で 2 万点/1 秒が実用上限 (#3528 の 1 節) なので、この規模は
#             **型が無ければ入口にすら立てない**。
# ⑤ 明示エラー area / volume が断る (点群では定義できない) / 法線の混在が断る /
#             零ベクトルの法線が断る。
# ⑥ 2D の書き出し (#3582 ・ ひさ判断 2026-09-22) **2D も .xyz に書ける**。
#             列数は .xyz の約束どおり 2 / 3 / 6:
#               法線なし 2D → 2 列 "x y"      法線あり 2D → 6 列 "x y 0 nx ny 0"
#             ⚠⚠ **往復で型が変わる** (pt-cloud2d → pt-cloud3d)。.xyz に「2D である」ことを
#               書く場所が無いため。⇒ *受け入れたうえで明示的に検定する* (黙って起きない)。
#             ★ 法線つき 2D を 6 列にするのは **情報を落とさない**ため (4 列は読む側が受けない)。
#             ★ area・volume は points.so が **宣言していない**ことで断っている。routing が
#               「この op をこの入力型で実行できるモジュールが無い」と受理する型まで添えて言う。
LC_ALL=C
export LC_ALL
SRAVA="$1"
D="${SRAVA_CACHE_DIR:?SRAVA_CACHE_DIR not set}"
W="$D-work"
# ★★ #3522: ハングの番犬 (共通)。詳細は test/srava_hangwatch.sh。
. "$(dirname "$0")/srava_hangwatch.sh"
. "$(dirname "$0")/srava_filecmp.sh"

rm -rf "$W"
mkdir -p "$W" || exit 1

run() {   # run <cache-suffix> <source>  → 出力全部
	rm -rf "$D-$1"
	SRAVA_CACHE_DIR="$D-$1" SRAVA_SOURCE="module(\"points.so\",{});
$2" "$SRAVA" 2>&1
}
n=0
chk() {   # chk <name> <got> <expected>
	[ "$2" = "$3" ] || { echo "POINTS_FAIL: $1 が [$2] (期待 [$3])"; exit 1; }
	n=$((n+1))
}
chk_err() {   # chk_err <name> <出力> <期待する部分文字列>
	case "$2" in
	*"$3"*) n=$((n+1)) ;;
	*) echo "POINTS_FAIL: $1 がエラーにならない/文言が違う: $2" ; exit 1 ;;
	esac
}

# 3×3 の格子 (z=0)。centroid=[1,1,0] ・ bbox=[[0,0,0],[2,2,0]] ・ nverts=9
GRID='points3d([[0,0,0],[1,0,0],[2,0,0],[0,1,0],[1,1,0],[2,1,0],[0,2,0],[1,2,0],[2,2,0]])'

# ---- ① 閉形式 ---------------------------------------------------------------------
O=$(run g1 "var p = $GRID;
print(\"V\", nverts(p));
print(\"V\", centroid(p));
print(\"V\", bbox(p));
print(\"V\", valid(p));")
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
chk "格子の点数"   "$1" "9"
chk "格子の重心"   "$2" "[1,1,0]"
chk "格子の AABB"  "$3" "[[0,0,0],[2,2,0]]"
chk "格子は valid" "$4" "1"

# 空の点群: 値としては作れるが valid=0 (共通定義の ①)
O=$(run g2 "print(\"V\", valid(points3d([])));")
chk "空の点群は valid=0" "$(echo "$O" | sed -n 's/^V //p')" "0"

# ---- ② 往復 -----------------------------------------------------------------------
A="$W/a.xyz"
O=$(run r1 "var p = $GRID;
export(\"$A\", p);
var q = import(\"$A\");
print(\"V\", nverts(q));
print(\"V\", centroid(q));
print(\"V\", bbox(q));")
_SPLIT_=$(echo "$O" | sed -n 's/^V //p' | tr -d ' ')
set -f
set -- $_SPLIT_
set +f
chk "往復後の点数"  "$1" "9"
chk "往復後の重心"  "$2" "[1,1,0]"
chk "往復後の AABB" "$3" "[[0,0,0],[2,2,0]]"
chk "往復のファイルは 3 列" "$(awk 'NR==1{print NF}' "$A")" "3"

# ---- ③ 法線の往復 -----------------------------------------------------------------
NA="$W/n_a.xyz"
NB="$W/n_b.xyz"
O=$(run r2 "var p = points3d([[[0,0,0],[0,0,1]],[[1,0,0],[0,1,0]],[[0,1,0],[-1,0,0]]]);
export(\"$NA\", p);
var q = import(\"$NA\");
export(\"$NB\", q);
print(\"V\", nverts(q));")
chk "法線つきの点数"       "$(echo "$O" | sed -n 's/^V //p')" "3"
chk "法線つきは 6 列"      "$(awk 'NR==1{print NF}' "$NA")" "6"
file_same "$NA" "$NB"; _rc=$?
if [ $_rc -eq 0 ]; then n=$((n+1)); elif [ $_rc -ge 2 ]; then
	echo "POINTS_FAIL: 法線の往復を比較できなかった (rc=$_rc ・ 2=比較手段が無い / 3=ファイルが読めない)"; exit 1
else
	echo "POINTS_FAIL: 法線の往復でファイルが変わった"; file_show_diff "$NA" "$NB"; exit 1
fi

# ---- ④ 規模 -----------------------------------------------------------------------
BIG="$W/big.xyz"
NBIG=100000
awk -v n=$NBIG 'BEGIN{ for(i=0;i<n;i++) printf "%.6f %.6f %.6f\n", i%317, (i*7)%211, i/n }' > "$BIG"
T0=$(date +%s)
O=$(run big "print(\"V\", nverts(import(\"$BIG\")));")
T1=$(date +%s)
chk "10 万点の import" "$(echo "$O" | sed -n 's/^V //p')" "$NBIG"
echo "POINTS_SCALE: $NBIG 点の import + nverts = $((T1-T0)) 秒 (値配列の道は 2 万点で 1 秒)"

# ---- ⑤ 明示エラー -----------------------------------------------------------------
chk_err "area(p)"   "$(run e1 "print(\"V\", area($GRID));")"   "no module can execute op 'area'"
chk_err "volume(p)" "$(run e2 "print(\"V\", volume($GRID));")" "no module can execute op 'volume'"
# ⚠ 混在は **どちら向きでも**断る (先に法線つき → 裸 / 先に裸 → 法線つき)。
chk_err "混在 (法線つき→裸)" \
	"$(run e3 "print(\"V\", nverts(points3d([[[0,0,0],[0,0,1]],[1,0,0]])));")" \
	"mixing the two forms is not allowed"
chk_err "混在 (裸→法線つき)" \
	"$(run e3b "print(\"V\", nverts(points3d([[0,0,0],[[1,0,0],[0,0,1]]])));")" \
	"mixing the two forms is not allowed"
chk_err "零ベクトルの法線" \
	"$(run e4 "print(\"V\", nverts(points3d([[[0,0,0],[0,0,0]]])));")" \
	"is the zero vector"
# ---- ⑥ 2D の書き出し (#3582) -------------------------------------------------------
# ★ 2026-09-22 まで「2D は .xyz に書けない」が仕様だった (往復で型が変わるため)。
#   ひさ判断で **書けるようにした** ⇒ 往復で 3D になることを *検定で明示する*。
S2="$W/two.xyz"
O=$(run x1 "export(\"$S2\", points2d([[1,2],[3,4]]));
var q = import(\"$S2\");
print(\"V\", type_of(q));
print(\"V\", nverts(q));
print(\"V\", vert(q,0));
print(\"V\", bbox(q));")
_SPLIT_=$(echo "$O" | sed -n 's/^V //p' | tr -d ' ')
set -f
set -- $_SPLIT_
set +f
chk "2D の列数"            "$(awk 'NR==1{print NF}' "$S2")" "2"
chk "2D の行数"            "$(wc -l < "$S2" | tr -d ' ')"   "2"
# ⚠⚠ **往復で 3D になる**。これは仕様 — 隠さずここで名指しして押さえる。
chk "往復で型が 3D になる" "$1" "pt-cloud3d"
chk "往復の点数"           "$2" "2"
chk "往復の座標 (z=0)"     "$3" "[1,2,0]"
chk "往復の AABB"          "$4" "[[1,2,0],[3,4,0]]"

# ★ 法線つき 2D は **6 列** (z=0 / nz=0)。4 列は読む側が受けないので、情報を落とさずに
#   書ける形はこれしかない (ひさ判断 2026-09-22)。
S2N="$W/two_n.xyz"
SQ=0.7071067811865476
O=$(run x2 "export(\"$S2N\", points2d([[[1,2],[$SQ,$SQ]],[[3,4],[0,1]]]));
var q = import(\"$S2N\");
print(\"V\", type_of(q));
print(\"V\", nverts(q));")
_SPLIT_=$(echo "$O" | sed -n 's/^V //p' | tr -d ' ')
set -f
set -- $_SPLIT_
set +f
chk "法線つき 2D の列数"   "$(awk 'NR==1{print NF}' "$S2N")" "6"
chk "法線つき 2D の往復型" "$1" "pt-cloud3d"
chk "法線つき 2D の点数"   "$2" "2"
# z と nz が 0 で埋まっていること (3 列目と 6 列目)。
chk "z が 0"               "$(awk 'NR==1{print $3}' "$S2N")" "0"
chk "nz が 0"              "$(awk 'NR==1{print $6}' "$S2N")" "0"
# 法線の値がそのまま残っていること (数として比べる)。
chk "法線 nx が残る" \
	"$(awk 'NR==1{ d=$4-0.7071067811865476; print (d*d < 1e-18) ? "ok" : "ng" }' "$S2N")" "ok"
chk "2 点目の法線 (0,1)" \
	"$(awk 'NR==2{ print (($4*$4 < 1e-18) && (($5-1)^2 < 1e-18)) ? "ok" : "ng" }' "$S2N")" "ok"

# ★ 退化: 空の 2D 点群は **0 バイト** / 1 点だけの 2D。
E2="$W/empty2.xyz"
O=$(run x3 "export(\"$E2\", points2d([]));
print(\"V\", nverts(import(\"$E2\")));")
chk "空の 2D は 0 バイト"  "$(wc -c < "$E2" | tr -d ' ')" "0"
chk "空の 2D の往復"       "$(echo "$O" | sed -n 's/^V //p' | tr -d ' ')" "0"
P1="$W/one2.xyz"
O=$(run x4 "export(\"$P1\", points2d([[5,6]]));
print(\"V\", vert(import(\"$P1\"),0));")
chk "1 点の 2D の列数"     "$(awk 'NR==1{print NF}' "$P1")" "2"
# ⚠ 往復は 3D なので **3 成分**で戻る (ここも「型が変わる」ことの現れ)。
chk "1 点の 2D の往復"     "$(echo "$O" | sed -n 's/^V //p' | tr -d ' ')" "[5,6,0]"

# ★★ 回帰: **3D の書き出しは 1 バイトも変わらない**。② ③ が往復とバイト一致を見ているので、
#   ここは *列数* だけを押さえる (3 列 / 法線つきは 6 列)。⚠ 2D を足したときに 3D の枝が
#   一緒に動いていないことの番。
R3="$W/reg3.xyz"
run x5 "export(\"$R3\", points3d([[1,2,3],[4,5,6]]));" > /dev/null
chk "3D は 3 列のまま"     "$(awk 'NR==1{print NF}' "$R3")" "3"
R3N="$W/reg3n.xyz"
run x6 "export(\"$R3N\", points3d([[[1,2,3],[0,0,1]]]));" > /dev/null
chk "法線つき 3D は 6 列"  "$(awk 'NR==1{print NF}' "$R3N")" "6"

echo "POINTS_OK ($n checks)"
