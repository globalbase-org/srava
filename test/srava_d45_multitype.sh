#!/bin/sh
# srava_d45_multitype.sh — ⑤ conv body-list の **多型共存** 検証 (P4 修正: 単一 conv スロット→型別リスト)。
#
# 同一の mf mesh (box) を、2 つの in-proc モジュールが **異なる自型** として同時消費する:
#   d4_nfaces(m) → m の cache を d4-mesh3d へ変換 (conv エントリ #1)
#   d5_nfaces(m) → 同じ m の cache を d5-mesh3d へ変換 (conv エントリ #2)
# 両消費者は m に依存するので m が揃った後に (in-proc で) 並行ディスパッチされ得る。旧・単一 conv スロット
# だと convType/convBody/convHelper を潰し合う (異型同時要求で競合) が、型別 body-list なら 2 エントリが
# 共存し両変換が成立する。
#
# 検証点:
#   (1) 両変換が成立し d4_nfaces == d5_nfaces == 12 (manifold box)。
#   (2) SRAVA_DBG_CONV に **2 本の異なる [CONV] 行** (MFM3->d4-mesh3d と MFM3->d5-mesh3d) が出る
#       ★ 2026-08-19: 変換元は **形式 (4CC)** で名乗る (以前は 4CC を型名へ引き直して出していたが、
#       形式は複数モジュールが共有しうるので先勝ちで引いた型名は嘘になる)。
#       = body-list に型別エントリが 2 つ共存 (単一スロットなら片方が消えるか競合する)。
#
# 引数: $1=srava (build-bench in-place・兄弟 .so を auto-load)。SRAVA_AGENT は env。
set -u
SRAVA="$1"
BOGUS_AGENT=/nonexistent/srava_agent_bogus   # 偽 agent = manifold/d4/d5 が全て in-proc の証明

SRC='
module("manifold.so", {priority:99, exec_default:"thread"});
module("d4.so", {});
module("d5.so", {});
var m = box(2,2,2);
var a = d4_nfaces(m);
var b = d5_nfaces(m);
print("D45", a, b);'

CACHE=$(mktemp -d /tmp/srava-d45.XXXXXX) || { echo "D45_FAIL: mktemp"; exit 1; }
trap 'rm -rf "$CACHE"' EXIT

# (1) 両変換の結果 (in-proc・偽 agent)
OUT=$(SRAVA_AGENT="$BOGUS_AGENT" SRAVA_CACHE_DIR="$CACHE/a" SRAVA_SOURCE="$SRC" "$SRAVA" 2>&1 | grep "^D45")
if [ "$OUT" != "D45 12 12" ]; then
	echo "D45_FAIL: got [$OUT] want [D45 12 12]"; exit 0
fi

# (2) 多型共存: 2 本の異なる [CONV] 行 (同じ形式 MFM3 から d4-mesh3d と d5-mesh3d の両方) が出ること。
CONV=$(SRAVA_AGENT="$BOGUS_AGENT" SRAVA_DBG_CONV=1 SRAVA_CACHE_DIR="$CACHE/b" SRAVA_SOURCE="$SRC" \
       "$SRAVA" 2>&1 | grep '^\[CONV\]' | sort -u)
N=$(printf '%s\n' "$CONV" | grep -c '^\[CONV\]')
if [ "$N" != "2" ] \
   || ! printf '%s\n' "$CONV" | grep -q 'MFM3 -> d4-mesh3d' \
   || ! printf '%s\n' "$CONV" | grep -q 'MFM3 -> d5-mesh3d'; then
	echo "D45_FAIL: expected 2 distinct conv entries (d4+d5), got: $CONV"; exit 0
fi

echo "D45_OK"
