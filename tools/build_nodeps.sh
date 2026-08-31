#!/bin/sh
# 外部依存を持つモジュールを **全部 OFF** にしてビルドが通り、srava が動くことを確認する (#3431 P0-a)。
# $1 = ソースツリー (CMakeLists.txt のある dir)。$2 = 使い捨てのビルド dir。$3.. = 追加の cmake 引数。
#
# ★ なぜ要るか (#3431 の完了条件②「依存が無い環境でも、そのモジュールを外してビルドが通る」):
#   SRAVA_MODULE_{CGAL,MANIFOLD,NEF,PIPEPROX} の option は前からあったが、**OFF 構成を一度も
#   ビルドしていなかった**。option を足した当人以外が触ると、ガードの外に依存が漏れて
#   「CGAL の無い機械では configure すら通らない」に容易に戻る。P2 (OpenVDB) / P3 (geogram) /
#   P5 (OCCT) はいずれも重い外部依存を持ち込むので、その受け皿としてここを回せるようにしておく。
#
# ★ 見ているもの: configure + ビルドが通ることに加えて、**残ったモジュールだけで実際に走る**こと。
#   ビルドが通るだけでは「planner が起動時に落ちる」を見逃す (d3 は CGAL/Manifold 非依存の
#   最小 mesh モジュールなので、これが動けば host + モジュール機構は生きている)。
set -e
SRC="${1:?source dir not given}"
BLD="${2:?build dir not given}"
shift 2

rm -rf "$BLD"
cmake -S "$SRC" -B "$BLD" \
  -DSRAVA_MODULE_CGAL=OFF \
  -DSRAVA_MODULE_NEF=OFF \
  -DSRAVA_MODULE_MANIFOLD=OFF \
  -DSRAVA_MODULE_PIPEPROX=OFF \
  "$@" > "$BLD.configure.log" 2>&1 || {
	echo "FAIL: configure failed (依存なし構成)"; tail -30 "$BLD.configure.log"; exit 1; }

cmake --build "$BLD" -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)" \
  > "$BLD.build.log" 2>&1 || {
	echo "FAIL: build failed (依存なし構成)"; tail -40 "$BLD.build.log"; exit 1; }

# 外した .so が本当に無いこと (ガードが効いている証拠)。
for so in cgal manifold nef_snc nef_hybrid pipe_proximity ; do
	if [ -f "$BLD/$so.so" ]; then
		echo "FAIL: $so.so was built although its module option is OFF"; exit 1
	fi
done

# 残ったモジュール (d3 = CGAL/Manifold 非依存の最小 mesh モジュール) だけで実際に走る。
V=$(SRAVA_MODULE_PATH="$BLD" SRAVA_AGENT="$BLD/srava_agent" SRAVA_CACHE_DIR="$BLD/nodeps-cache" \
    SRAVA_SOURCE='module("d3.so",{}); print("NF", d3_nfaces(d3_cube(2)));' "$BLD/srava" 2>&1 | sed -n 's/^NF //p')
if [ -z "$V" ]; then
	echo "FAIL: 依存なしビルドの srava が d3 で値を返さない"
	SRAVA_MODULE_PATH="$BLD" SRAVA_AGENT="$BLD/srava_agent" SRAVA_CACHE_DIR="$BLD/nodeps-cache2" \
	  SRAVA_SOURCE='module("d3.so",{}); print("NF", d3_nfaces(d3_cube(2)));' "$BLD/srava" 2>&1 | head -10
	exit 1
fi

echo "NODEPS-OK d3_nfaces=$V"
