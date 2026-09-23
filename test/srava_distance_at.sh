#!/bin/sh
# distance_at(m, [x,y,z]) — **点から境界までの最短距離** の回帰 (#3514)。
#
# $1 = srava 実行体 / $2 = モジュール名 (.so) / $3 = 許容相対誤差
# $4 = openvdb だけに要る dx (",0.02" の形で生成 op の末尾へ連結)
# $5 = "sphere" なら解析球での検定も行う (B-rep のカーネルだけ) / "band" なら狭帯域の検定
# env: SRAVA_AGENT, SRAVA_CACHE_DIR。
#
# ---- なぜ主役が **箱** なのか ------------------------------------------------------
# ★★ 球は入れない (srava_meshprops.sh と同じ判断)。occt は解析球なので |d - r| が厳密に出るが、
#   メッシュ系は **内接多面体**なので構造的に小さい値になる (半径 2・seg=128 で中心から
#   1.99805 = 真値 2 より 0.2% 小さい)。許容を緩めて両方通すのは **偽のテスト**になる。
#   ⇒ 全カーネルで厳密に一致する形 = **軸平行の箱**を主役にする。箱なら面が平面なので
#     メッシュ表現でも B-rep でも同じ立体で、距離は手で書ける。
# ★ 球は「解析曲面のまま測れる」ことの検定として occt だけで行う ($5="sphere")。
#
# ---- 約束 --------------------------------------------------------------------------
# ★ **符号なし** (内側でも正)。4 カーネルで揃えてある。
#   ⚠ openvdb の距離場は本当は符号を持っているが、op の約束を揃えるために絶対値にしている。
# ⚠⚠ occt では **立体をそのまま BRepExtrema に渡してはいけない** — TopoDS_Solid は
#   「中身の詰まった領域」なので内側の点との距離が **0** になる (2026-09-13 実測)。
#   実装は Face を集めた compound に対して測っている。③ がその回帰。
SRAVA="$1"
SO="${2:?module .so not given}"
TOL="${3:-1e-9}"
DX="$4"
MODE="$5"
D="${SRAVA_CACHE_DIR:?SRAVA_CACHE_DIR not set}"

# ★★ #3522: ハングの番犬 (共通)。詳細は test/srava_hangwatch.sh。
. "$(dirname "$0")/srava_hangwatch.sh"

if [ -n "$DX" ]; then A=",$DX"; else A=""; fi

run() {   # run <cache-suffix> <source>  → 出力全部
	rm -rf "$D-$1"
	SRAVA_CACHE_DIR="$D-$1" SRAVA_SOURCE="module(\"$SO\",{priority:99});
$2" "$SRAVA" 2>&1
}
n=0
chk() {   # chk <name> <got> <expected>
	ok=$(awk -v g="$2" -v e="$3" -v t="$TOL" 'BEGIN{
		if(g==""){print 0; exit} d=g-e; if(d<0)d=-d; s=(e<0?-e:e); if(s<1)s=1;
		print (d/s <= t) ? 1 : 0 }')
	[ "$ok" = "1" ] || { echo "FAIL: $1 が $2 (期待 $3 ± 相対 $TOL)"; exit 1; }
	n=$((n+1))
}

if [ "$MODE" = "band" ]; then
	# ---- 狭帯域のカーネル (openvdb): 界面の近くだけ測れる ----------------------
	#   ★ 距離が **場そのもの**なので探索が要らない。⚠ 帯 (既定 3 ボクセル) の外は
	#     background に飽和していて「遠い」しか分からない ⇒ 明示エラーになること。
	O=$(run b1 "
var B = box(2,2,2$A);
print(\"VAL\", distance_at(B,[0.03,1,1]));    // 面 x=0 の内側 0.03
print(\"VAL\", distance_at(B,[-0.03,1,1]));   // 面 x=0 の外側 0.03
print(\"VAL\", distance_at(B,[1,1,2.04]));    // 面 z=2 の外側 0.04")
	# ⚠⚠ **値の分割だけ glob を止める** (2026-09-22)。
	#   `set -- $(…)` の $( ) は **クォートできない** (単語分割が要る) のでパス名展開に掛かる。
	#   比べる値は `[1,1,0]` 形 = glob の **文字クラス**なので、カレント (ビルド dir) に `1` という
	#   名前のファイルが 1 つあるだけで `1` に化け、**「値が違う」という、それらしい文言で赤くなる**
	#   (2026-09-21 に実際に踏んだ。⚠ PIG_TIMING は *ファイルパス* を取る env なので
	#   `PIG_TIMING=1` と打つと `1` というファイルができる)。
	#   ★★ **実行と分割を分ける**のが肝。`set -f; set -- $(cmd)` と囲うと -f が $( ) の中まで効き、
	#     cmd がシェル関数だと *その中の glob* まで殺す (実測: count_tus → objs_of の
	#     `CMakeFiles/*/link.txt` が読めず「全 0 本」になった)。⇒ 先に変数へ取ってから分割する。
	_SPLIT_=$(echo "$O" | sed -n 's/^VAL //p')
	set -f
	set -- $_SPLIT_
	set +f
	chk "面の内側 0.03" "$1" 0.03
	chk "面の外側 0.03" "$2" 0.03
	chk "面の外側 0.04" "$3" 0.04
	#   ⚠⚠ 飽和値を黙って返さないこと。ここが効かないと「遠い点の距離」が一定値で出る。
	echo "$(run b2 "print(\"VAL\", distance_at(box(2,2,2$A),[9,9,9]));")" \
	    | grep -q "outside the narrow band" || {
		echo "FAIL: 狭帯域の外で飽和値を返した (明示エラーになっていない)"; exit 1; }
	n=$((n+1))
	echo "DISTANCEAT-OK $SO ($n checks)"
	exit 0
fi

# ---- ① 箱 [0,2]^3 — 全カーネルで **厳密**に一致する形 --------------------------
#   面までの最短距離は手で書ける。★ 角の外側は稜線/頂点までの距離になるので、
#     「面を無限平面として測っていないか」もここで落ちる。
O=$(run a1 "
var B = box(2,2,2$A);
print(\"VAL\", distance_at(B,[4,1,1]));      // +x の面から 2
print(\"VAL\", distance_at(B,[1,1,5]));      // +z の面から 3
print(\"VAL\", distance_at(B,[-0.5,1,1]));   // -x の面から 0.5
print(\"VAL\", distance_at(B,[1,1,1]));      // ★ 内側 (中心) → 面まで 1
print(\"VAL\", distance_at(B,[0,1,1]));      // 面のうえ → 0
print(\"VAL\", distance_at(B,[3,3,3]));      // ★ 角の外 → 頂点 (2,2,2) まで sqrt(3)
print(\"VAL\", distance_at(B,[3,3,1]));      // ★ 稜の外 → 稜まで sqrt(2)")
_SPLIT_=$(echo "$O" | sed -n 's/^VAL //p')
set -f
set -- $_SPLIT_
set +f
chk "+x の面から 2"      "$1" 2
chk "+z の面から 3"      "$2" 3
chk "-x の面から 0.5"    "$3" 0.5
chk "★ 内側 (中心) 1"   "$4" 1
chk "面のうえ 0"         "$5" 0
chk "★ 角の外 sqrt(3)"  "$6" 1.7320508075688772
chk "★ 稜の外 sqrt(2)"  "$7" 1.4142135623730951

# ---- ② 引数の検査: 点でないものは明示エラー ------------------------------------
echo "$(run a2 "print(\"VAL\", distance_at(box(2,2,2$A), 5));")" | grep -q "needs a point" || {
	echo "FAIL: 点でない引数が通った"; exit 1; }
n=$((n+1))

# ---- ③ ★ 解析曲面のまま測れること (B-rep のカーネルだけ) ------------------------
#   ⚠⚠ ここは occt の実装の罠の回帰でもある: 立体をそのまま BRepExtrema に渡すと
#     **内側が 0** になる。半径 r の球の中心が r で出ることがその検査。
if [ "$MODE" = "sphere" ]; then
	O=$(run a3 '
var S = sphere(2);
print("VAL", distance_at(S,[5,0,0]));                                            // |5-2| = 3
print("VAL", distance_at(S,[1,0,0]));                                            // |1-2| = 1
print("VAL", distance_at(S,[0,0,0]));                                            // ★ 中心 → 2 (0 なら立体を渡している)
print("VAL", distance_at(S,[2.886751345948129,2.886751345948129,2.886751345948129]));  // 対角 d=5 → 3')
	_SPLIT_=$(echo "$O" | sed -n 's/^VAL //p')
	set -f
	set -- $_SPLIT_
	set +f
	chk "球 外 |5-2|"     "$1" 3
	chk "球 内 |1-2|"     "$2" 1
	chk "★ 球 中心 → r"  "$3" 2
	chk "球 対角 |5-2|"   "$4" 3
fi

echo "DISTANCEAT-OK $SO ($n checks)"
