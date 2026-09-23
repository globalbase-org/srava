#!/bin/sh
# export_vox (voxel 化 → vox.h5) の routing 回帰。$1 = srava 実行体。env: SRAVA_AGENT, SRAVA_CACHE_DIR。
#
# ★ 2026-08-19 に新設。それまで export_vox には ctest が 1 本も無く、**可変長 op が型ディスパッチを
#   名前で迂回している**ことが誰にも見えていなかった (迂回の結果、入力 mesh の home module へ配送され、
#   manifold 優先だと "no module can execute op 'export_vox'" で落ちていた)。
#   いまは sig の可変長表記 "(cg-mesh3d...)->ref" で解決する。
# ★ #3468 (2026-09-01): export_vox の提供元が cgal.so → **openvdb_cg.so** へ移った。
#   幾何は openvdb_cg が申告する cgMesh::WIRE (libsrava_cg の実体) が読むので、受理する型は
#   従来どおり cg/mf/gg。以下 4 例すべてが openvdb_cg で走る。
SRAVA="$1"
D="${SRAVA_CACHE_DIR:?SRAVA_CACHE_DIR not set}"
# ★★ #3522: ハングの番犬 (共通・常時 ON)。詳細は test/srava_hangwatch.sh。
. "$(dirname "$0")/srava_hangwatch.sh"
OUT="$D-out"
rm -rf "$D" "$OUT"; mkdir -p "$OUT"

REG='regions:[{name:"a",side:"inside"},{name:"b",side:"inside"}]'

# $1=ラベル $2=h5 の名前 $3=srava ソース
try() {
	rm -rf "$D"
	# ★ #3452: 起動時 eager-load 撤去に伴い、cgal(既定カーネル)の明示ロードが要る。
	# ★ #3468: export_vox は openvdb_cg.so が提供するので、そちらも要る。
	MSG=$(SRAVA_CACHE_DIR="$D" \
	      SRAVA_SOURCE="module(\"cgal.so\",{}); module(\"openvdb_cg.so\",{}); $3" "$SRAVA" 2>&1)
	if [ ! -s "$OUT/$2" ]; then
		echo "VOX_FAIL: $1 で $2 が書かれていない: $MSG"; exit 0
	fi
	echo "  ok $1 ($(wc -c < "$OUT/$2") bytes)"
}

# ① cgal (既定カーネル) で単一 mesh。
try "単一 mesh" a.h5 "export_vox(\"$OUT/a.h5\", {dx:0.5}, box(2,2,2));"
# ② 複数 mesh (docs の正規の使い方: 領域ごとに mesh を渡す)。★可変長 sig が効いていることの確認。
try "複数 mesh" b.h5 "export_vox(\"$OUT/b.h5\", {dx:0.5, $REG}, box(2,2,2), box(1,1,4));"
# ③ manifold を既定にしても openvdb_cg へ振れること (その sig が (mf-mesh3d...) を申告しているため)。
#    ★ここが 2026-08-19 以前は落ちていた: 型でなく入力の home module へ配送していたので manifold へ
#      行き、manifold は export_vox を実装していないのでエラーになっていた。
# ★ #3468 の確認も兼ねる: openvdb_cg は priority 0 なので、**priority では絶対に選ばれない**。
#   ここが通る = 型 (sig) で振れている証拠。
try "mf 入力" c.h5 "module(\"manifold.so\",{priority:99}); export_vox(\"$OUT/c.h5\", {dx:0.5}, box(2,2,2));"
# ④ 型の混在 (cg の箱 + mf の箱)。可変長の繰り返し位置がモジュール単位の型集合であることの確認。
try "型混在" d.h5 "module(\"manifold.so\",{priority:99});
      export_vox(\"$OUT/d.h5\", {dx:0.5, $REG}, cast(\"cg-mesh3d\", box(2,2,2)), box(1,1,4));"

# ─────────────────────────────────────────────────────────────────────
# ★ #3469: vd-grid3d (OpenVDB の格子) も受ける = 1 つの h5 にカーネル混在レイヤ。
#   ⚠ マスク名は **4 文字以上**にする (strings へ落ちたときの保険。既定は 4 文字未満を捨てる)。
#
# ★★ 2026-09-06: マスクの検査を **h5ls -r で /masks/<name> を見る**形へ直した。
#   従来は `strings h5 | grep -qx <name>` だったが、これは *h5 のバイト配置に依存して外れる*。
#   実例 (macOS・OpenVDB 12.1.1): vd-grid だけの e.h5 で名前の直後のバイトがたまたま可読文字に
#   なり、strings の出力が "region0i" という 1 行になって `grep -qx region0` が空振りした
#   (h5ls では /masks/region0 が正しく在る = **製品は正しく、テストだけが落ちていた**)。
#   h5ls が無い環境では部分一致の grep へ落とす (-x をやめるだけでこの罠は消える)。
# ─────────────────────────────────────────────────────────────────────

# h5 にマスク <2> が在るか。h5ls があればそれで、無ければ strings の部分一致で。
has_mask() {   # $1=h5 パス $2=マスク名
	if command -v h5ls >/dev/null 2>&1; then
		h5ls -r "$1" 2>/dev/null | grep -q "^/masks/$2[[:space:]]"
	else
		strings "$1" | grep -q "$2"
	fi
}
MIX='regions:[{name:"skull",side:"inside"},{name:"brain",side:"inside"}]'

# $1=ラベル $2=h5 名 $3=ソース $4.. = h5 に在るべきマスク名
try_masks() {
	L="$1"; F="$2"; SRC="$3"; shift 3
	rm -rf "$D"
	MSG=$(SRAVA_CACHE_DIR="$D" \
	      SRAVA_SOURCE="module(\"cgal.so\",{}); module(\"manifold.so\",{}); module(\"openvdb.so\",{}); module(\"openvdb_cg.so\",{}); $SRC" "$SRAVA" 2>&1)
	if [ ! -s "$OUT/$F" ]; then echo "VOX_FAIL: $L で $F が書かれていない: $MSG"; exit 0; fi
	for nm in "$@"; do
		if ! has_mask "$OUT/$F" "$nm"; then
			echo "VOX_FAIL: $L の $F にマスク '$nm' が無い"; exit 0
		fi
	done
	echo "  ok $L ($(wc -c < "$OUT/$F") bytes・マスク $*)"
}

# ⑤ vd-grid だけ。★ openvdb は leaf 生成 op を持つが priority 1 なので既定にはならない。
#    "openvdb"::sphere で明示指名する (#3467)。
try_masks "vd-grid だけ" e.h5 \
  "export_vox(\"$OUT/e.h5\", {dx:0.2}, \"openvdb\"::sphere(1.5, 0.1));" region0

# ⑥ ★ **このチケットの主目的**: メッシュ (cgal) と vd-grid (openvdb) を 1 回で書く。
try_masks "メッシュ + vd-grid 混在" f.h5 \
  "export_vox(\"$OUT/f.h5\", {dx:0.2, $MIX}, box(2,2,2), \"openvdb\"::sphere(1.5, 0.1));" skull brain

# ⑦ dx が違う vd-grid を 2 つ。★ **エラーにしない** — ここは各入力を出力格子へ独立に
#    ラスタライズするだけで、#3463 の bool_from_args のようにツリーを直接合成しないため、
#    格子が揃っている必要が無い (起票時は明示エラーにする予定だったが要らなかった)。
try_masks "dx 違いの grid 2 つ" g.h5 \
  "export_vox(\"$OUT/g.h5\", {dx:0.2, $MIX}, \"openvdb\"::sphere(1.5, 0.1), \"openvdb\"::sphere(1.0, 0.05));" skull brain

# ─────────────────────────────────────────────────────────────────────
# ★ #3491 (2026-09-06): **中空の形**が h5 で中空のまま出ること。
#   ここまでの 8 ケースは box / sphere だけで **中空の形が 1 つも無かった**ので、
#   2 つの経路のどちらが空洞を落としても誰も気づけなかった。
#
#   export_vox は入力の種類で経路が分かれる (vcaExportVox.cpp 486 行):
#     (a) メッシュ入力  → voxelize_tris = CGAL の EFT による **厳密 z-パリティ**。
#                        meshToLevelSet を通らないので元から空洞に強い。
#     (b) vd-grid 入力 → level set の **符号**をセル中心で読むだけ。
#                        ⇒ 渡された格子が詰まっていれば h5 も詰まる。
#   (b) に voxelize で作った格子を渡すのが #3491 の影響経路だった。両方見る。
#
#   閉形式: 中空の殻 sphere(1.5) --- sphere(1.0) は V = 4/3*pi*(1.5^3-1.0^3) = 9.9483767。
#   ⚠ 壊れると「詰まった球」14.1372 になる。40% 違うので許容 3% でも余裕で切り分く。
# ─────────────────────────────────────────────────────────────────────

# h5 のマスクの体積 = 1 のセル数 * dx^3。h5dump で生バイトへ出して od で数える。
mask_volume() {   # $1=h5 パス $2=マスク名 $3=dx
	command -v h5dump >/dev/null 2>&1 || { echo SKIP; return; }
	B="$OUT/_mask.bin"; rm -f "$B"
	h5dump -d "/masks/$2" -b LE -o "$B" "$1" >/dev/null 2>&1 || { echo SKIP; return; }
	[ -s "$B" ] || { echo SKIP; return; }
	N=$(od -An -v -tu1 "$B" | tr -s ' ' '\n' | grep -c '^1$')
	rm -f "$B"
	awk -v n="$N" -v d="$3" 'BEGIN{ printf "%.6f", n*d*d*d }'
}

DXH=0.04
rm -rf "$D-hollow"; rm -f "$OUT/h_mesh.h5" "$OUT/h_grid.h5"
SRAVA_CACHE_DIR="$D-hollow" SRAVA_SOURCE="module(\"cgal.so\",{}); module(\"manifold.so\",{priority:99});
  module(\"openvdb.so\",{}); module(\"openvdb_mf.so\",{}); module(\"openvdb_cg.so\",{});
  var shell = sphere(1.5,96) --- sphere(1.0,96);
  export_vox(\"$OUT/h_mesh.h5\", {dx:$DXH}, shell);
  export_vox(\"$OUT/h_grid.h5\", {dx:$DXH}, voxelize(shell, $DXH));" "$SRAVA" >/dev/null 2>&1
for pair in "h_mesh.h5:メッシュ入力 (厳密 z-パリティ)" "h_grid.h5:vd-grid 入力 (voxelize した格子)"; do
	F="${pair%%:*}"; L="${pair#*:}"
	[ -s "$OUT/$F" ] || { echo "VOX_FAIL: 中空 $L で $F が書かれていない"; exit 0; }
	V=$(mask_volume "$OUT/$F" region0 "$DXH")
	if [ "$V" = "SKIP" ]; then echo "  skip 中空 $L (h5dump が無い)"; continue; fi
	ok=$(awk -v v="$V" 'BEGIN{ d=(v-9.9483767)/9.9483767; if(d<0)d=-d; print (d<0.03)?1:0 }')
	[ "$ok" = "1" ] || { echo "VOX_FAIL: 中空 $L のマスク体積が $V (期待 9.948377 +-3%)
	  ★ 14.14 前後なら **空洞が埋まっている** (詰まった球になっている)"; exit 0; }
	echo "  ok 中空 $L (マスク体積 $V)"
done

echo "EXPORT-VOX-OK"
