#!/bin/sh
# ★ #3452: 起動時 eager-load 撤去に伴い、box() 等の実行に実カーネルの明示ロードが要る。
export SRAVA_MODULE_ALL=1
# include "module/all.sra" の解決に要る(cmake ENVIRONMENT が SRAVA_PATH を設定していないため)。
SRAVA_PATH="$(cd "$(dirname "$0")/../lib" && pwd)"
export SRAVA_PATH
# SRAVA_CACHE_RETAIN の回帰テスト: 終了時クリーンアップの保持方針が切り替わること。
#   warm で used キャッシュを作り、その複製を未使用 hash(ffff…)= stray(別プログラムの残骸を模す)として置く。
#   stray の扱いが方針で変わることを確認:
#     RETAIN=all       → 残る(完了は消さない)
#     RETAIN=過去日     → 残る(stray の mtime=今 はその日より新しい)
#     RETAIN=未来日     → 消える(今 はその日より古い扱い → 全未使用が cutoff 以前)
#     既定(未設定)      → 消える(即削除)
#   used キャッシュはどのモードでも残る。touch を使わず日付 cutoff で新旧を作るので移植性がある。
# 引数 $1 = srava 実行体パス。env: SRAVA_AGENT, SRAVA_CACHE_DIR。
set -e
SRAVA="$1"
D="${SRAVA_CACHE_DIR:?SRAVA_CACHE_DIR not set}"
STRAY="$D/ffffffffffffffff.cache"

warm() {                                       # used キャッシュを作り stray を(再)設置
	"$SRAVA" >/dev/null 2>&1
	one=$(ls "$D"/*.cache | grep -v ffffffffffffffff | head -1)
	cp "$one" "$STRAY"
}
fail() { echo "FAIL: $1"; exit 1; }

rm -rf "$D"; warm
SRAVA_CACHE_RETAIN=all "$SRAVA" >/dev/null 2>&1
[ -e "$STRAY" ] || fail "RETAIN=all: stray was removed"
[ -e "$one" ]   || fail "RETAIN=all: used cache was removed"

rm -rf "$D"; warm
SRAVA_CACHE_RETAIN=2000-01-01 "$SRAVA" >/dev/null 2>&1
[ -e "$STRAY" ] || fail "RETAIN=past-date: recent stray was removed"

rm -rf "$D"; warm
SRAVA_CACHE_RETAIN=2099-01-01 "$SRAVA" >/dev/null 2>&1
[ ! -e "$STRAY" ] || fail "RETAIN=future-date: old stray was not removed"
[ -e "$one" ]     || fail "RETAIN=future-date: used cache was removed"

rm -rf "$D"; warm
"$SRAVA" >/dev/null 2>&1
[ ! -e "$STRAY" ] || fail "default(immediate): stray was not removed"

# in-code 設定: env(SRAVA_CACHE_RETAIN)を未設定にしたまま、プログラムが CACHE_RETAIN="all" を
# 代入 → CACHE_DIR と同様コードから方針を上書きできる(env より優先)。stray は残るはず。
# 出力シンク(export)を付けて終了コードを決定的(0)にする(結果未使用だと teardown で非0 になり得る)。
rm -rf "$D"; warm
SRAVA_SOURCE='CACHE_RETAIN = "all"; export("/tmp/srava_retain_incode.stl", box(2,2,2) ||| box(1,1,1));' "$SRAVA" >/dev/null 2>&1
[ -e "$STRAY" ] || fail "in-code CACHE_RETAIN=all: stray was removed"

echo "RETAIN_OK"
