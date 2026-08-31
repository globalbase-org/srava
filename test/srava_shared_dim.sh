#!/bin/sh
# srava_shared_dim.sh — rev4「同一形式を 2 カーネルが次元分担で I/O」実演 (§9.4/§9.7 Q-E)。
#
# d2 (2D 専用) と d3 (3D 専用) が **同じ op 名 `dcount`** を、それぞれ自分の次元型 (d2-shape2d /
# d3-mesh3d) で申告する。dcount は 2 owner なので op-owner routing は発火せず、decide_executor が
# **入力型 (次元)** で正しいカーネルへ振る = rev4 の動機だった (kernel×次元) 2 軸問題の型ディスパッチ解決。
#
# CGAL/Manifold なしの隔離 dir (planner + srava_agent + d2.so + d3.so だけ) で実証する。
#
# 引数: $1=srava  $2=srava_agent  $3=d2.so  $4=d3.so
set -u
SRAVA="$1"
AGENT="$2"
D2SO="$3"
D3SO="$4"

WORK=$(mktemp -d /tmp/srava-shared-dim.XXXXXX) || { echo "SHARED_DIM_FAIL: mktemp"; exit 1; }
trap 'rm -rf "$WORK"' EXIT

# ★ Windows 可搬 (2026-08-13): basename を保存して copy (exe=.exe・モジュール=.dll のまま)。
#   PE には RPATH が無いので共有 libpig.dll も exe と同 dir へ (srava_d3_standalone.sh と同じ)。
SRAVA_BIN=$(basename "$SRAVA"); AGENT_BIN=$(basename "$AGENT")
D2_BIN=$(basename "$D2SO"); D3_BIN=$(basename "$D3SO")
cp "$SRAVA" "$WORK/$SRAVA_BIN" || { echo "SHARED_DIM_FAIL: cp srava";       exit 1; }
cp "$AGENT" "$WORK/$AGENT_BIN" || { echo "SHARED_DIM_FAIL: cp srava_agent"; exit 1; }
cp "$D2SO"  "$WORK/$D2_BIN"    || { echo "SHARED_DIM_FAIL: cp d2 module";   exit 1; }
cp "$D3SO"  "$WORK/$D3_BIN"    || { echo "SHARED_DIM_FAIL: cp d3 module";   exit 1; }
# ⚠ 共有ライブラリの命名は環境で違う: MinGW=libpig*.dll / Cygwin=cygpig*.dll。
# 片方だけ見ると、当たらない側で pig を持ち込めず srava が起動できない。
for dll in "$(dirname "$SRAVA")"/libpig*.dll "$(dirname "$SRAVA")"/cygpig*.dll; do
	[ -f "$dll" ] && cp "$dll" "$WORK/"
done

# 隔離 dir に d2 / d3 以外のカーネルモジュールが無いこと (libpig*.dll は非モジュールなので除外)。
if ls "$WORK"/*.so "$WORK"/*.dll 2>/dev/null | grep -vE '/((d2|d3)\.(so|dll)|(lib|cyg)pig[^/]*\.dll)$' | grep -q .; then
	echo "SHARED_DIM_FAIL: unexpected module in isolated dir: $(ls "$WORK"/*.so "$WORK"/*.dll 2>/dev/null)"; exit 1
fi

CACHE="$WORK/cache"
mkdir -p "$CACHE"

# 同一 op `dcount` が入力の次元で振り分けられる:
#   dcount(d3_cube(1))  → d3 (3D・頂点数 8)
#   dcount(d2_square(1))→ d2 (2D・点数   4)
OUT=$(cd "$WORK" && SRAVA_AGENT="$WORK/$AGENT_BIN" SRAVA_CACHE_DIR="$CACHE" SRAVA_SOURCE='
module("d2.so", {});
module("d3.so", {});
var c3 = dcount(d3_cube(1));
var c2 = dcount(d2_square(1));
var ok = 0;
if (c3 == 8) { if (c2 == 4) { ok = 1; } }
print("SD", ok, c3, c2);' "$WORK/$SRAVA_BIN" 2>&1)

if echo "$OUT" | grep -q "^SD 1 8 4$"; then
	echo "SHARED_DIM_OK"
else
	echo "SHARED_DIM_FAIL: $OUT"
fi
