#!/bin/sh
# カーネル間の結果一致テストの **雛形** (#3432 P0-b)。
# $1 = srava 実行体。$2 = モデル名。$3 = 比較するモジュール (.so 名)。$4 = 許容相対誤差 (0 = 完全一致)。
# env: SRAVA_AGENT, SRAVA_CACHE_DIR。
#
# ★ なぜ雛形にするか (#3432):
#   カーネルを増やすたびに「同じモデルを両方で評価して突き合わせる」テストを手で書いていた
#   (sphere_kernel_agree / tube_kernel_agree / nef の agree モード)。それぞれ別の書き方・別の
#   許容誤差になっていて、新しいカーネルを足すたびに写経が要る。ここに集約して、
#   **CMake の表に 1 行足すだけ**で新カーネルの一致検証が付くようにする。
#
# 基準は常に **cgal** (厳密カーネル)。cgal 自身を相手にすることはない。
#
# ★ 許容誤差の方針 (#3432 の指示):
#   - 厳密カーネル同士 (cgal ↔ nef) は原理的に同じ値になるはずだが、体積の積み方
#     (corefinement の三角形分割 vs SNC の facet 分割) が違うので **最終丸めで下位桁がずれうる**。
#     → 極小の相対誤差 (1e-12) で見る。leaf (sphere 等) は分割が同一なので 0 (完全一致) を要求できる。
#   - float カーネル (manifold) との比較は **体積 / 面積の相対誤差** で見る。bit 一致を要求しない
#     (cgal は厳密有理数で積んで最後に 1 回丸める / manifold は double で積む)。
#   - 0 を指定したときだけ **文字列の完全一致** を要求する (共通生成器を使っている leaf の回帰)。
#
# ⚠ cold cache 必須。基準側とカーネル側で **キャッシュ dir を必ず分ける**
#   (同じ dir を使うと 2 回目が HIT して「同じ値が出て当然」の無意味なテストになる)。
SRAVA="${1:?srava binary not given}"
MODEL="${2:?model not given}"
SO="${3:?module .so not given}"
TOL="${4:-0}"
D="${SRAVA_CACHE_DIR:?SRAVA_CACHE_DIR not set}"

# ---- モデル定義: それぞれ "VAL <数>" を 1 行以上出す srava プログラム ----
#   カーネルを跨いで同じ値になるべきものだけを置く (面数や頂点数は分割規約が違うので入れない)。
case "$MODEL" in
box)          SRC='print("VAL", volume(box(2,2,3)));' ;;
sphere)       SRC='print("VAL", volume(sphere(5,32)));' ;;
icosphere)    SRC='print("VAL", volume(icosphere(5,2)));' ;;
union)        SRC='var s = sphere(1.5,24); print("VAL", volume(s ||| translate(s,[1,1,1])));' ;;
difference)   SRC='print("VAL", volume(box(2,2,2) --- sphere(1.2,24)));' ;;
intersection) SRC='print("VAL", volume(box(2,2,2) &&& sphere(1.2,24)));' ;;
# 掃引管 (共通ヘッダ src/h/common/tube.h)。曲がり + 可変半径 + r=0 の尖り端を 1 式で踏む。
tube3d)       SRC='print("VAL", volume(tube([[[0,0,0],0],[[2,0,0],0.5],[[2,3,1],0.4],[[0,4,2],0.2]], 16)));' ;;
tube2d)       SRC='print("VAL", area(tube([[[0,0],3],[[20,5],2],[[35,-8],4]])));' ;;
*)            echo "FAIL: unknown model '$MODEL'"; exit 1 ;;
esac

run() {   # run <module 注入行> <cache dir>
	rm -rf "$2"
	SRAVA_CACHE_DIR="$2" SRAVA_SOURCE="$1 $SRC" "$SRAVA" 2>&1 | sed -n 's/^VAL //p'
}

REF=$(run 'module("cgal.so",{priority:99});' "$D-ref")
GOT=$(run "module(\"$SO\",{priority:99});"   "$D-$MODEL")

if [ -z "$REF" ]; then echo "FAIL: cgal (基準) が $MODEL で値を出さない"; exit 1; fi
if [ -z "$GOT" ]; then
	echo "FAIL: $SO が $MODEL で値を出さない"
	rm -rf "$D-dbg"
	SRAVA_CACHE_DIR="$D-dbg" SRAVA_SOURCE="module(\"$SO\",{priority:99}); $SRC" "$SRAVA" 2>&1 | head -10
	exit 1
fi

echo "cgal      : $REF"
echo "$SO : $GOT"

if [ "$TOL" = "0" ]; then
	# 完全一致 (共通生成器を共有している leaf の回帰。ずれたら生成器の共有が壊れた合図)
	if [ "$REF" = "$GOT" ]; then echo "AGREE $MODEL $SO (exact)"; else echo "MISMATCH $MODEL $SO"; exit 1; fi
else
	ok=$(awk -v a="$REF" -v b="$GOT" -v t="$TOL" 'BEGIN{
		na=split(a,A," "); nb=split(b,B," ");
		if (na != nb) { print 0; exit }
		for (i=1;i<=na;i++) {
			d=A[i]-B[i]; if(d<0)d=-d; s=(A[i]<0?-A[i]:A[i]); if(s<1)s=1;
			if (d > t*s) { print 0; exit }
		}
		print 1 }')
	if [ "$ok" = "1" ]; then echo "AGREE $MODEL $SO (rel<=$TOL)"; else echo "MISMATCH $MODEL $SO"; exit 1; fi
fi
