#!/bin/sh
# srava_d3_standalone.sh — rev4 Phase D-3 の最終形実証。
#
# planner (srava) + generic agent host (srava_agent) + d3.so **だけ**を隔離 dir にコピーして走らせ、
# CGAL/Manifold なしで mesh 出力カーネルが完走することを確認する = 「planner + 第3カーネル 1 個だけで
# 走る (エージェント非依存)」。ホスト 2 種はどちらもカーネル中立で、幾何は d3.so のみが供給する。
#
# planner の load_search_path ① は /proc/self/exe の dir を読む → 隔離 dir に d3.so だけ置けば
# ロードされるのは d3.so 1 個のみ。srava_agent も resolve_module_so で自 dir の d3.so を引く。
#
# 引数: $1=srava  $2=srava_agent  $3=d3.so
set -u
SRAVA="$1"
AGENT="$2"
D3SO="$3"

WORK=$(mktemp -d /tmp/srava-d3-standalone.XXXXXX) || { echo "D3_STANDALONE_FAIL: mktemp"; exit 1; }
trap 'rm -rf "$WORK"' EXIT

# ★ Windows 可搬 (2026-08-13): basename を保存して copy する (exe は .exe・モジュールは .dll のまま)。
#   旧 `cp "$D3SO" "$WORK/d3.so"` は Windows で「ローダは .dll を探すのに置いたのは .so」で全滅していた。
SRAVA_BIN=$(basename "$SRAVA"); AGENT_BIN=$(basename "$AGENT"); D3_BIN=$(basename "$D3SO")
cp "$SRAVA" "$WORK/$SRAVA_BIN" || { echo "D3_STANDALONE_FAIL: cp srava";       exit 1; }
cp "$AGENT" "$WORK/$AGENT_BIN" || { echo "D3_STANDALONE_FAIL: cp srava_agent"; exit 1; }
cp "$D3SO"  "$WORK/$D3_BIN"    || { echo "D3_STANDALONE_FAIL: cp d3 module";   exit 1; }
# ★ PE には RPATH が無い: 共有 libpig.dll は exe と同 dir 必須 (ELF は build RPATH で解決 = copy 不要)。
for dll in "$(dirname "$SRAVA")"/libpig*.dll; do [ -f "$dll" ] && cp "$dll" "$WORK/"; done

# 隔離 dir に d3 以外のカーネルモジュールが無いことを確認 (cgal/manifold を持ち込んでいない)。
# libpig*.dll はモジュールではない (SRAVA_MODULE_SYM 無し) ので除外。
if ls "$WORK"/*.so "$WORK"/*.dll 2>/dev/null | grep -vE '/(d3\.(so|dll)|libpig[^/]*\.dll)$' | grep -q .; then
	echo "D3_STANDALONE_FAIL: unexpected module in isolated dir: $(ls "$WORK"/*.so "$WORK"/*.dll 2>/dev/null)"; exit 1
fi

CACHE="$WORK/cache"
mkdir -p "$CACHE"

# d3 だけで mesh 往復: d3_cube(s)→mesh・d3_merge→連結(16v/24f)・d3_nfaces/d3_nverts→値。
# priority opt-in 不要 (d3 が唯一のカーネル = 既定 + d3 op は d3 owner へ op-owner routing)。
OUT=$(cd "$WORK" && SRAVA_AGENT="$WORK/$AGENT_BIN" SRAVA_CACHE_DIR="$CACHE" SRAVA_SOURCE='
var m = d3_merge(d3_cube(1), d3_cube(2));
var ok = 0;
if (d3_nfaces(m) == 24) { if (d3_nverts(m) == 16) { ok = 1; } }
print("D3S", ok);' "$WORK/$SRAVA_BIN" 2>&1)

if echo "$OUT" | grep -q "^D3S 1$"; then
	echo "D3_STANDALONE_OK"
else
	echo "D3_STANDALONE_FAIL: $OUT"
fi
