#!/bin/sh
# srava_d4_xconv.sh — ⑤ cross-module 型変換の実証 (P4)。
#
# **2 個の in-proc(THREAD) mesh モジュールが跨る型変換** を初めて exercise する:
#   - manifold (in-proc) が box(2,2,2) を作る → mf-mesh3d (4CC MFM3)。
#   - d4 (in-proc) の d4_nfaces/d4_nverts が **自型 d4-mesh3d としてそれを消費** する。
#     d4 op の sig は foreign 入力型 (mf-mesh3d) を明示列挙しているので decide_executor が d4 へ振り、
#     d4 agent の get_body([d4-mesh3d]) が MFM3 file を d4-mf-upgrade reader で **変換読み** する
#     (converted 経路)。mfMesh の MFM3 codec framing は d4Mesh と同一なので同じ decode で読める。
#
# 検証点:
#   (1) 変換読みが成立し、面数/頂点数が manifold box と一致する (下の期待値)。
#   (2) dedup: 同一 mf cache を 2 op (d4_nfaces + d4_nverts) が消費しても **変換は 1 回** だけ
#       (SRAVA_DBG_CONV の [CONV] 行が 1 本)。単一 conv スロット + single-flight の効果。
#
# 引数: $1=srava (build-bench の in-place。兄弟 .so を auto-load する)。SRAVA_AGENT は env。
set -u
SRAVA="$1"

# manifold box(2,2,2) の三角形数/頂点数 (Manifold の GetMeshGL64 が返す立方体の値)。
EXP_F=12
EXP_V=8

SRC='
module("manifold.so", {priority:99, exec_default:"thread"});
module("d4.so", {});
var m = box(2,2,2);
var f = d4_nfaces(m);
var v = d4_nverts(m);
print("D4X", f, v);'

CACHE=$(mktemp -d /tmp/srava-d4-xconv.XXXXXX) || { echo "D4X_FAIL: mktemp"; exit 1; }
trap 'rm -rf "$CACHE"' EXIT

# ★ 偽 SRAVA_AGENT を撃ち込む: agent プロセスが spawn されたら実行に失敗する = manifold も d4 も
#   **in-proc(THREAD) で走っている** ことの証明 (d3/pipe と同じ定石)。両者が in-proc = ⑤ の核心。
BOGUS_AGENT=/nonexistent/srava_agent_bogus

# (1) 変換読みの結果 (in-proc・偽 agent)
OUT=$(SRAVA_AGENT="$BOGUS_AGENT" SRAVA_CACHE_DIR="$CACHE/a" SRAVA_SOURCE="$SRC" "$SRAVA" 2>&1 | grep "^D4X")
if [ "$OUT" != "D4X $EXP_F $EXP_V" ]; then
	echo "D4X_FAIL: got [$OUT] want [D4X $EXP_F $EXP_V] (in-proc cross-module 変換)"; exit 0
fi

# (2) dedup: 変換回数を計装行で数える (共有 mf cache 1 つ → [CONV] 1 行が期待)。
NCONV=$(SRAVA_AGENT="$BOGUS_AGENT" SRAVA_DBG_CONV=1 SRAVA_CACHE_DIR="$CACHE/b" SRAVA_SOURCE="$SRC" \
        "$SRAVA" 2>&1 | grep -c '^\[CONV\]')
if [ "$NCONV" != "1" ]; then
	echo "D4X_FAIL: dedup expected 1 conversion, got $NCONV"; exit 0
fi

echo "D4X_OK"
