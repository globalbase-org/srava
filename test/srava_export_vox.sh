#!/bin/sh
# export_vox (voxel 化 → vox.h5) の routing 回帰。$1 = srava 実行体。env: SRAVA_AGENT, SRAVA_CACHE_DIR。
#
# ★ 2026-08-19 に新設。それまで export_vox には ctest が 1 本も無く、**可変長 op が型ディスパッチを
#   名前で迂回している**ことが誰にも見えていなかった (迂回の結果、入力 mesh の home module へ配送され、
#   manifold 優先だと "no module can execute op 'export_vox'" で落ちていた)。
#   いまは sig の可変長表記 "(cg-mesh3d...)->ref" で解決するので、以下 4 例すべてが cgal で走る。
SRAVA="$1"
D="${SRAVA_CACHE_DIR:?SRAVA_CACHE_DIR not set}"
OUT="$D-out"
rm -rf "$D" "$OUT"; mkdir -p "$OUT"

REG='regions:[{name:"a",side:"inside"},{name:"b",side:"inside"}]'

# $1=ラベル $2=h5 の名前 $3=srava ソース
try() {
	rm -rf "$D"
	# ★ #3452: 起動時 eager-load 撤去に伴い、cgal(既定カーネル)の明示ロードが要る。
	MSG=$(SRAVA_CACHE_DIR="$D" SRAVA_SOURCE="module(\"cgal.so\",{}); $3" "$SRAVA" 2>&1)
	if [ ! -s "$OUT/$2" ]; then
		echo "VOX_FAIL: $1 で $2 が書かれていない: $MSG"; exit 0
	fi
	echo "  ok $1 ($(wc -c < "$OUT/$2") bytes)"
}

# ① cgal (既定カーネル) で単一 mesh。
try "単一 mesh" a.h5 "export_vox(\"$OUT/a.h5\", {dx:0.5}, box(2,2,2));"
# ② 複数 mesh (docs の正規の使い方: 領域ごとに mesh を渡す)。★可変長 sig が効いていることの確認。
try "複数 mesh" b.h5 "export_vox(\"$OUT/b.h5\", {dx:0.5, $REG}, box(2,2,2), box(1,1,4));"
# ③ manifold を既定にしても cgal へ振れること (cgal の sig が (mf-mesh3d...) を申告しているため)。
#    ★ここが 2026-08-19 以前は落ちていた: 型でなく入力の home module へ配送していたので manifold へ
#      行き、manifold は export_vox を実装していないのでエラーになっていた。
try "mf 入力" c.h5 "module(\"manifold.so\",{priority:99}); export_vox(\"$OUT/c.h5\", {dx:0.5}, box(2,2,2));"
# ④ 型の混在 (cg の箱 + mf の箱)。可変長の繰り返し位置がモジュール単位の型集合であることの確認。
try "型混在" d.h5 "module(\"manifold.so\",{priority:99});
      export_vox(\"$OUT/d.h5\", {dx:0.5, $REG}, cast(\"cg-mesh3d\", box(2,2,2)), box(1,1,4));"

echo "EXPORT-VOX-OK"
