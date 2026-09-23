#!/bin/sh
# ★★ #3527 段 6: sig に現れる **型名の綴り** を登録済みの型名と突き合わせる。$1 = srava。
#
# ---- なぜ要るか ----
# sig は **ただの文字列**なので、型名を綴り間違えてもコンパイルは通る。通ったうえで
# ルータがその行に一度もヒットせず、**死んだ行**が黙って残る。
#
#     gg-mesh3d (geogram)  対  gu-mesh3d (geomutils)   ← 1 文字違い
#     nf-mesh3d (SNC)      対  nfb-mesh3d (境界形式)
#     vd-grid3d            対  vd-grid                 ← 後者は存在しない
#
# ---- ⚠⚠ この検査が **何を捕まえ、何を捕まえないか** (実測 2026-09-17) ----
#
#     ① 存在しない綴り     gu-mesh3d -> guu-mesh3d   ★ **捕まる**
#     ② 3d 落ち            vd-grid3d -> vd-grid      ★ **捕まる**
#     ③ 実在する別の型へ   gg-mesh3d -> gu-mesh3d    ✗ **捕まらない**
#
# ★ ③が捕まらないのは当然で、この検査は「その名前が *登録されているか*」しか見ていない。
#   **どちらも実在する名前**なので集合として区別がつかない。1 文字違いの取り違えそのものは
#   *routing の結果* を見る検査 (test/routing_dependency.txt / 各カーネルの値の検定) の担当。
#   ⇒ ここが守るのは「**誰も名乗っていない型を sig が指している**」= その行が死んでいる状態。
#   ⚠ 守備範囲を書かずに「綴り検査がある」とだけ言うと、③まで守られていると誤解される。
. "$(dirname "$0")/srava_hangwatch.sh"

SRAVA="${1:?srava not given}"
TOOL="$(dirname "$0")/../tools/audit_sig_typenames.py"

OUT=$(python3 "$TOOL" "$SRAVA" --check 2>&1)
RC=$?
printf '%s\n' "$OUT" | sed 's/^/      /'
if [ "$RC" != "0" ]; then
	echo "FAIL: sig に未登録の型名がある"
	exit 1
fi
echo "SIGTYPES_OK"
