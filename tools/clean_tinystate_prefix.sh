#!/usr/bin/env bash
#
# clean_tinystate_prefix.sh
#   /usr/local を「tinyState v2.0.0 だけ」のクリーンな状態にする。
#   旧世代の残骸 (ts2/c, gt2, _ts2, version.pl, tscpp2.bak 等) を消してから
#   develop-v2 の build を新規 install し直す。
#
#   実行:  sudo bash clean_tinystate_prefix.sh
#
#   ★ tinyState が所有するパスだけを明示列挙して消す。
#     srava / srava_agent / nsys* / python3.13 / lib/cmake 配下の他パッケージ等には触れない。
#
set -euo pipefail

# tinyState develop-v2 の build ディレクトリ。別環境では TINYSTATE_BUILD で上書き可。
BUILD="${TINYSTATE_BUILD:-/home/joshua/proj/claude/gs/tinyState/develop-v2/build}"
MANIFEST="$BUILD/install_manifest.txt"

# ---- preflight -------------------------------------------------------------
if [ "$(id -u)" -ne 0 ]; then
  echo "ERROR: root で実行してください:  sudo bash $0" >&2
  exit 1
fi
if [ ! -f "$MANIFEST" ]; then
  echo "ERROR: install_manifest.txt が無い ($MANIFEST)。" >&2
  echo "       先に develop-v2 を configure/build しておくこと。中止(何も消していません)。" >&2
  exit 1
fi

# ---- 消す対象 (tinyState 所有物のみ・明示列挙) -------------------------------
# include: v2 が出すツリー全部 + v2 で廃止された gt2 も掃除
INCLUDE_TREES=(
  /usr/local/include/ts2
  /usr/local/include/std2
  /usr/local/include/mth2
  /usr/local/include/_ts2
  /usr/local/include/gt2        # v2 で廃止 (再 install では復活しない)
)
# lib: 静的ライブラリ + tinyState の cmake package (lib/cmake/ 自体は残す)
LIB_ITEMS=(
  /usr/local/lib/libtinyState2.a
  /usr/local/lib/libtinyState2Math.a
  /usr/local/lib/cmake/tinyState
)
# bin: v2 が出すツール + 旧世代の遺物 (version.pl, tscpp2.bak)。
#      ★ srava / srava_agent / nsys / nsys-ui は列挙しない = 絶対に消さない
BIN_ITEMS=(
  /usr/local/bin/tscpp2
  /usr/local/bin/tslink2
  /usr/local/bin/tsjslink2
  /usr/local/bin/tinyState2
  /usr/local/bin/gtt2
  /usr/local/bin/tspig2
  /usr/local/bin/jscopy2
  /usr/local/bin/tscpp2.bak     # 旧手動バックアップ (stale)
  /usr/local/bin/version.pl     # v1 の SVN 版数スクリプト (v2 で廃止)
)

ALL_ITEMS=( "${INCLUDE_TREES[@]}" "${LIB_ITEMS[@]}" "${BIN_ITEMS[@]}" )

# ---- 安全ガード: /usr/local 配下 かつ 保護名でない ことを二重確認 -----------
PROTECTED='^(/usr/local/bin/(srava|srava_agent|nsys|nsys-ui)|/usr/local/lib/python3\.13|/usr/local/lib/cmake/?$)'
for p in "${ALL_ITEMS[@]}"; do
  case "$p" in
    /usr/local/*) : ;;
    *) echo "ABORT: $p が /usr/local 配下でない。中止。" >&2; exit 1 ;;
  esac
  if [[ "$p" =~ $PROTECTED ]]; then
    echo "ABORT: 保護対象 $p を消そうとした。中止。" >&2; exit 1
  fi
done

# ---- backup (存在する物だけ tar.gz へ退避) ---------------------------------
STAMP="$(date +%Y%m%d_%H%M%S)"
BACKUP="/tmp/tinystate_prefix_backup_${STAMP}.tar.gz"
EXISTING=()
for p in "${ALL_ITEMS[@]}"; do [ -e "$p" ] && EXISTING+=( "$p" ); done
if [ "${#EXISTING[@]}" -gt 0 ]; then
  echo "== backup → $BACKUP =="
  tar czf "$BACKUP" "${EXISTING[@]}" 2>/dev/null || true
  echo "   (${#EXISTING[@]} パスを退避。復旧は  sudo tar xzf $BACKUP -C / )"
fi

# ---- remove ----------------------------------------------------------------
echo "== 旧 tinyState 成果物を削除 =="
for p in "${ALL_ITEMS[@]}"; do
  if [ -e "$p" ]; then echo "  rm  $p"; rm -rf -- "$p"; fi
done

# ---- reinstall (v2.0.0 を新規に) -------------------------------------------
echo "== cmake --install (develop-v2 build) =="
cmake --install "$BUILD"

# ---- verify ----------------------------------------------------------------
echo "== 検証 =="
fail=0
[ -e /usr/local/include/ts2/c ]  && { echo "  NG: ts2/c がまだ残っている"; fail=1; } || echo "  OK: ts2/c 無し (廃止ツリー消えた)"
[ -e /usr/local/include/gt2 ]    && { echo "  NG: gt2 がまだ残っている";   fail=1; } || echo "  OK: gt2 無し"
[ -f /usr/local/lib/libtinyState2.a ]     && echo "  OK: libtinyState2.a あり"     || { echo "  NG: libtinyState2.a 無し"; fail=1; }
[ -f /usr/local/lib/libtinyState2Math.a ] && echo "  OK: libtinyState2Math.a あり" || { echo "  NG: libtinyState2Math.a 無し"; fail=1; }
[ -f /usr/local/include/ts2/c++/ts_types.h ] && echo "  OK: ts2/c++/ts_types.h あり" || { echo "  NG: ts2/c++/ts_types.h 無し"; fail=1; }
[ -x /usr/local/bin/tscpp2 ]     && echo "  OK: tscpp2 あり ($(tscpp2 --version 2>/dev/null | head -1 || echo 'ver取得不可'))" || { echo "  NG: tscpp2 無し"; fail=1; }
# 保護対象が生きているか一応確認
for p in /usr/local/bin/srava /usr/local/bin/srava_agent; do
  [ -e "$p" ] && echo "  OK(保護): $p 健在" || echo "  注意: $p が元々無い (今回の削除対象ではない)"
done
echo "== version.pl 更新の TS_REVISION =="; grep -H TS_VERSION /usr/local/include/std2/tinyState_config.h 2>/dev/null | head -1 || true

if [ "$fail" -ne 0 ]; then echo "!! 検証で NG あり。$BACKUP から戻せます。" >&2; exit 1; fi
echo "== 完了: /usr/local は tinyState v2.0.0 のみのクリーン状態 =="
